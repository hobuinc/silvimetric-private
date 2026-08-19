import json
import os
import copy
import xml.etree.ElementTree as ET

from math import floor
from typing import Sequence
from typing_extensions import Optional, Union, Literal
from datetime import datetime

import tiledb
import numpy as np
import pandas as pd

from .config import StorageConfig, ShatterConfig
from .metric import Metric, Attribute
from .bounds import Bounds


_GDAL_DATA_TYPES = {
    np.dtype(np.uint8): 'Byte',
    np.dtype(np.int8): 'Int8',
    np.dtype(np.uint16): 'UInt16',
    np.dtype(np.int16): 'Int16',
    np.dtype(np.uint32): 'UInt32',
    np.dtype(np.int32): 'Int32',
    np.dtype(np.uint64): 'UInt64',
    np.dtype(np.int64): 'Int64',
    np.dtype(np.float32): 'Float32',
    np.dtype(np.float64): 'Float64',
}


def ts_overlap(first: int, second: int):
    """
    Return true if the first and second timestamps share any values.

    :param first: First timestamp.
    :param second: Second timestamp.
    :return: _description_
    """
    if first[0] > second[1]:
        return False
    if first[1] < second[0]:
        return False
    return True


def ts_encompass(first, second):
    """
    Return true if the first timestamp completely encompasses the second.

    :param first: First timestamp.
    :param second: Second timestamp.
    :return: _description_
    """
    if second[0] >= first[0] and second[1] <= first[1]:
        return True
    else:
        return False


class Storage:
    """Handles storage of shattered data in a TileDB Database."""

    def __init__(self, config: StorageConfig, ctx: tiledb.Ctx = None):
        if not tiledb.object_type(config.tdb_dir, ctx=ctx) == 'array':
            raise Exception(
                f"Given database directory '{config.tdb_dir}' does not exist"
            )

        self.config: StorageConfig = config
        self._reader: tiledb.DenseArray = None

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_value, exc_tb):
        if self._reader is not None:
            self._reader.close()
        self._reader = None
        return

    def __getstate__(self):
        """Exclude the process-local TileDB reader from Dask serialization."""
        state = self.__dict__.copy()
        state['_reader'] = None
        return state

    @staticmethod
    def create(config: StorageConfig, ctx: tiledb.Ctx = None):
        """
        Creates TileDB storage.

        :param config: :class:`silvimetric.resources.config.StorageConfig`
        :param ctx: :class:`tiledb.Ctx`, defaults to None
        :raises ValueError: If missing requried dependency in Metrics.
        :return: :class:`silvimetric.resources.storage.Storage`
        """

        if ctx is None:
            ctx = tiledb.default_ctx()

        # adjust cell bounds if necessary
        config.root.adjust_alignment(config.resolution, config.alignment)

        xi = floor(
            (config.root.maxx - config.root.minx) / float(config.resolution)
        )
        yi = floor(
            (config.root.maxy - config.root.miny) / float(config.resolution)
        )

        # protect user from out of bounds errors
        xsize = min(config.xsize, xi+1)
        ysize = min(config.ysize, yi+1)
        if xsize < config.xsize:
            config.log.warning(f'X Tile size lowered to {xsize}')
        if ysize < config.ysize:
            config.log.warning(f'Y Tile size lowered to {ysize}')
        config.xsize = xsize
        config.ysize = ysize

        dim_row = tiledb.Dim(
            name='X',
            domain=(0, xi),
            dtype=np.uint64,
            tile=xsize,
            filters=tiledb.FilterList([tiledb.ZstdFilter(level = 7)]),
        )
        dim_col = tiledb.Dim(
            name='Y',
            domain=(0, yi),
            dtype=np.uint64,
            tile=ysize,
            filters=tiledb.FilterList([tiledb.ZstdFilter(level = 7)]),
        )
        domain = tiledb.Domain(dim_row, dim_col)

        count_att = tiledb.Attr(
            name='count',
            dtype=np.uint32,
            filters=tiledb.FilterList([tiledb.ZstdFilter(level = 7)]),
            fill=0,
        )
        proc_att = tiledb.Attr(
            name='shatter_process_num',
            dtype=np.uint16,
            filters=tiledb.FilterList([tiledb.ZstdFilter(level = 7)]),
            fill=0,
        )
        start_time_att = tiledb.Attr(
            name='start_time',
            # Store epoch days rather than TileDB DATETIME_DAY.  The latter is
            # not supported by GDAL's TileDB raster driver, whereas the
            # integer representation is exactly what date-range predicates
            # already use.
            dtype=np.int64,
            filters=tiledb.FilterList([tiledb.ZstdFilter(level = 7)]),
            fill=0,
        )
        end_time_att = tiledb.Attr(
            name='end_time',
            dtype=np.int64,
            filters=tiledb.FilterList([tiledb.ZstdFilter(level = 7)]),
            fill=0,
        )
        dim_atts = [attr.schema() for attr in config.attrs]

        metric_atts = [
            m.schema(a)
            for m in config.metrics
            for a in config.attrs
            if a in m.attributes or not m.attributes
        ]

        # Check that all attributes required for metric usage are available
        att_list = [a.name for a in config.attrs]
        required_atts = [
            d.name
            for m in config.metrics
            for d in m.dependencies
            if isinstance(d, Attribute)
        ]
        for ra in required_atts:
            if ra not in att_list:
                raise ValueError(f'Missing required dependency, {ra}.')

        # allows_duplicates lets us insert multiple values into each cell,
        # with each value representing a set of values from a shatter process
        # https://docs.tiledb.com/main/how-to/performance/performance-tips/summary-of-factors#allows-duplicates
        schema = tiledb.ArraySchema(
            domain=domain,
            attrs=[
                count_att,
                proc_att,
                start_time_att,
                end_time_att,
                *dim_atts,
                *metric_atts,
            ],
            offsets_filters=tiledb.FilterList(
                [
                    tiledb.PositiveDeltaFilter(),
                ]
            ),
        )
        schema.check()

        tiledb.DenseArray.create(config.tdb_dir, schema)
        with tiledb.DenseArray(config.tdb_dir, 'w') as writer:
            writer.meta['config'] = str(config)

        s = Storage(config)
        gdal_attrs = [
            count_att,
            proc_att,
            start_time_att,
            end_time_att,
            *dim_atts,
            *metric_atts,
        ]
        gdal_metadata = s.build_gdal_metadata(
            geotransform=(
                config.root.minx,
                config.resolution,
                0.0,
                config.root.maxy,
                0.0,
                -config.resolution,
            ),
            attrs=gdal_attrs,
        )
        # GDAL's TileDB dense-array reader expects the PAM XML in UINT8
        # metadata.  TileDB-Py stores a Python str as STRING_UTF8, so pass a
        # uint8 buffer explicitly for this GDAL-specific metadata item.
        s.save_metadata(
            '_gdal',
            np.frombuffer(gdal_metadata.encode('utf-8'), dtype=np.uint8),
        )
        s.save_metadata('dataset_type', 'raster')
        s.save_config()

        return s

    @staticmethod
    def from_db(tdb_dir: str, ctx: tiledb.Ctx = None):
        """
        Create Storage object from information stored in a database.

        :param tdb_dir: TileDB database directory.
        :param ctx: :class:`tiledb.Ctx`, defaults to None.
        :return: Returns the derived storage.
        """
        if ctx is None:
            ctx = Storage.get_tdb_context()

        reader = tiledb.open(tdb_dir, 'r', ctx=ctx)
        metadata = reader.meta
        s = metadata['config']
        config = StorageConfig.from_string(s)

        # in case a database has been copied somewhere else
        config.tdb_dir = tdb_dir
        storage = Storage(config, ctx=ctx)

        # set the metadata for storage object so we don't have to query again
        storage._reader = reader

        return storage

    @staticmethod
    def create_shard_group(config: StorageConfig, uri: str) -> None:
        """Create an immutable-publish TileDB group for spatial array shards."""
        if tiledb.object_type(uri) is not None:
            raise ValueError(f"Shard group URI '{uri}' already exists")
        tiledb.Group.create(uri)
        group_config = copy.deepcopy(config)
        group_config.tdb_dir = uri
        with tiledb.Group(uri, 'w') as group:
            group.meta['config'] = str(group_config)
        # POSIX VFS requires an existing parent for each published array;
        # object stores treat this as a harmless prefix marker.
        tiledb.VFS().create_dir(f'{uri}/shards')

    @staticmethod
    def shard_members(uri: str) -> list['Storage']:
        """Open every array member of a published spatial shard group."""
        ctx = Storage.get_tdb_context()
        if tiledb.object_type(uri, ctx=ctx) != 'group':
            raise ValueError(f"'{uri}' is not a TileDB shard group")
        with tiledb.Group(uri, 'r', ctx=ctx) as group:
            members = list(group)
        arrays = [
            member.uri for member in members if member.type == tiledb.Array
        ]
        if not arrays:
            raise ValueError(f"Shard group '{uri}' has no array members")
        return [Storage.from_db(array, ctx=ctx) for array in arrays]

    def build_gdal_metadata(
        self,
        geotransform: tuple[float, float, float, float, float, float],
        attrs: Sequence[tiledb.Attr],
    ) -> str:
        """Build the GDAL TileDB driver's PAM metadata document.

        The GDAL TileDB driver exposes a dense two-dimensional array's
        attributes positionally.  Keep every schema attribute in this document
        (including SilviMetric's bookkeeping and point-value attributes) so
        that the descriptions of the derived metric bands remain aligned with
        the actual TileDB schema.
        """
        root = ET.Element('PAMDataset')
        srs = ET.SubElement(
            root,
            'SRS',
            attrib={'dataAxisToSRSAxisMapping': '1,2'},
        )
        srs.text = self.config.crs.to_wkt(version='WKT1_GDAL')
        transform = ET.SubElement(root, 'GeoTransform')
        transform.text = ', '.join(str(value) for value in geotransform)

        metadata = ET.SubElement(root, 'Metadata')
        ET.SubElement(metadata, 'MDI', attrib={'key': 'AREA_OR_POINT'}).text = (
            'Area'
        )
        image_structure = ET.SubElement(
            root, 'Metadata', attrib={'domain': 'IMAGE_STRUCTURE'}
        )
        for key, value in (
            ('DATASET_TYPE', 'raster'),
            ('INTERLEAVE', 'ATTRIBUTES'),
            (
                'X_SIZE',
                str(
                    floor(
                        (self.config.root.maxx - self.config.root.minx)
                        / self.config.resolution
                    )
                ),
            ),
            (
                'Y_SIZE',
                str(
                    floor(
                        (self.config.root.maxy - self.config.root.miny)
                        / self.config.resolution
                    )
                ),
            ),
            ('NUM_BANDS', str(len(attrs))),
        ):
            ET.SubElement(image_structure, 'MDI', attrib={'key': key}).text = (
                value
            )

        for index, attr in enumerate(attrs, start=1):
            dtype = _GDAL_DATA_TYPES.get(np.dtype(attr.dtype))
            band = ET.SubElement(
                root, 'PAMRasterBand', attrib={'band': str(index)}
            )
            ET.SubElement(band, 'Description').text = attr.name
            if dtype is not None:
                ET.SubElement(band, 'DataType').text = dtype

        ET.indent(root, space='  ')
        return ET.tostring(root, encoding='unicode')

    def save_config(self) -> None:
        """
        Save StorageConfig to the Database
        """
        # build metadata, we'll only requery it if we can't find the desired
        # key later
        with self.open('w') as w:
            w.meta['config'] = str(self.config)
        if self._reader is not None:
            self._reader.reopen()

    def get_config(self) -> StorageConfig:
        """
        Get the StorageConfig currently in use by Storage.

        :return: StorageConfig representing this object.
        """
        meta_str = self.get_metadata('config')
        return StorageConfig.from_string(meta_str)

    def save_shatter_meta(self, config: ShatterConfig):
        """
        Save shatter metadata to the base TileDB metadata with the name
        convention `shatter_{proc_num}`
        """
        key = f'shatter_{config.time_slot}'
        data = json.dumps(config.to_json())
        self.save_metadata(key, data)

    def get_shatter_meta(self, time_slot: int):
        """
        Get shatter metadata from the base TileDB metadata with the name
        convention `shatter_{proc_num}`

        :return: :class:`silvimetric.resources.config.ShatterConfig`
        """
        key = f'shatter_{time_slot}'
        m = self.get_metadata(key)
        return ShatterConfig.from_string(m)

    def get_metadata(self, key: str) -> str:
        """
        Return metadata at given key. Check first for this key in the
        _meta member variable. If it's not there, we'll check the metadata
        in the db.

        :param key: Key to look for in metadata.
        :return: Metadata value found in storage.
        """
        # if meta hasn't been set up, do so
        reader = self.open('r')
        try:
            return reader.meta[key]
        except KeyError:
            reader.reopen()
            self._reader = reader
            return reader.meta[key]

    def save_metadata(self, key: str, data: Union[str, np.ndarray]) -> None:
        """
        Save metadata to storage.

        :param key: Metadata key to save to.
        :param data: Data to save to metadata.
        """
        # if writer isn't set up, do it now
        # propogate the key-value to both tiledb and the local copy
        with self.open('w') as w:
            w.meta[key] = data
        if self._reader is not None:
            self._reader.reopen()

    @staticmethod
    def get_tdb_context(_storage=None):
        cfg = tiledb.Config()
        cfg['vfs.s3.region'] = os.environ.get(
            'SILVIMETRIC_TILEDB_S3_REGION',
            os.environ.get(
                'AWS_REGION',
                os.environ.get('AWS_DEFAULT_REGION', 'us-west-2'),
            ),
        )
        no_sign_request = os.environ.get(
            'SILVIMETRIC_TILEDB_NO_SIGN_REQUEST', ''
        ).lower()
        if no_sign_request in {'1', 'true', 'yes', 'on'}:
            cfg['vfs.s3.no_sign_request'] = 'true'
        cfg['vfs.s3.connect_scale_factor'] = '25'
        cfg['vfs.s3.connect_max_retries'] = '10'
        # S3-backed extracts over a large shard group can legitimately require
        # more than TileDB-Py's default 100 resubmissions while it grows query
        # buffers.  Keep the policy configurable, but use a scale-safe default
        # so an otherwise complete shatter is not rejected during validation.
        incomplete_retries = os.environ.get(
            'SILVIMETRIC_TILEDB_MAX_INCOMPLETE_RETRIES', '1000'
        )
        try:
            incomplete_retry_count = int(incomplete_retries)
        except ValueError as error:
            raise ValueError(
                'SILVIMETRIC_TILEDB_MAX_INCOMPLETE_RETRIES must be a positive integer'
            ) from error
        if incomplete_retry_count < 1:
            raise ValueError(
                'SILVIMETRIC_TILEDB_MAX_INCOMPLETE_RETRIES must be a positive integer'
            )
        cfg['py.max_incomplete_retries'] = str(incomplete_retry_count)
        concurrency = os.environ.get('SILVIMETRIC_TILEDB_CONCURRENCY')
        if concurrency is not None:
            try:
                concurrency_level = int(concurrency)
            except ValueError as error:
                raise ValueError(
                    'SILVIMETRIC_TILEDB_CONCURRENCY must be a positive integer'
                ) from error
            if concurrency_level < 1:
                raise ValueError(
                    'SILVIMETRIC_TILEDB_CONCURRENCY must be a positive integer'
                )
            cfg['sm.compute_concurrency_level'] = str(concurrency_level)
            cfg['sm.io_concurrency_level'] = str(concurrency_level)
        # cfg['vfs.s3.max_parallel_ops'] = '1'
        ctx = tiledb.Ctx(cfg)
        return ctx

    def get_attributes(
        self, names: Optional[list[str]] = None
    ) -> list[Attribute]:
        """
        Find list of attribute names from storage config.

        :param names: List of Metric names to get.
        :return: List of attribute names.
        """
        if names is not None:
            return [a for a in self.config.attrs if a.name in names]

        return self.config.attrs

    def get_metrics(self, names: Optional[list[str]] = None) -> list[Metric]:
        """
        Find List of metric names from storage config

        :param names: List of Metric names to get.
        :return: List of metric names.
        """
        if names is not None:
            return [m for m in self.config.metrics if m.name in names]
        return self.config.metrics

    def get_derived_names(
        self,
        metrics: Optional[list[str, Metric]] = None,
        attributes: Optional[list[str, Attribute]] = None,
    ) -> list[str]:
        """
        Return names of TileDB Attribute names based on combination of
        Metrics and SilviMetric Attributes. If none are specified, grab
        all Metrics and Attributes from Storage Config.
        """
        if metrics is None:
            metrics = self.config.metrics
        if attributes is None:
            attributes = self.config.attrs

        # if no attributes are set in the metric, use all
        return [
            m.entry_name(a.name)
            for m in metrics
            for a in attributes
            if not m.attributes or a.name in [ma.name for ma in m.attributes]
        ]

    def open(self, mode: str = 'r', timestamp=None) -> tiledb.SparseArray:
        """
        Open stream for TileDB database in given mode and at given timestamp.

        :param mode: Mode to open TileDB stream in. Valid options are
            'w', 'r', 'm', 'd'., defaults to 'r'.
        :param timestamp: TileDB timestamp, a tuple of start and end datetime.
        :raises Exception: Incorrect Mode, only valid modes are 'w' and 'r'.
        :raises Exception: Path exists and is not a TileDB array.
        :raises Exception: Path does not exist.
        :yield: TileDB array context manager.
        """

        # tiledb and dask have bad interaction with opening an array if
        # other threads present
        ctx = self.get_tdb_context()

        # non-timestamped reader and writer are stored as member variables to
        # avoid opening and closing too many io objects.
        if timestamp is not None or mode != 'r':
            return tiledb.open(
                self.config.tdb_dir, mode, timestamp=timestamp, ctx=ctx
            )
        else:  # no timestamp and mode is 'r'
            if self._reader is None or not self._reader.isopen:
                self._reader = tiledb.open(
                    self.config.tdb_dir, 'r', ctx=ctx
                )

            self._reader.reopen()
            return self._reader

    def write(self, data_in: pd.DataFrame, dates: tuple[datetime, datetime]):
        """Write to TileDB Array."""

        data_in = data_in.rename(columns={'xi': 'X', 'yi': 'Y'})
        attr_dict = {f'{a.name}': a.dtype for a in self.config.attrs}
        xy_dict = {'X': data_in.X.dtype, 'Y': data_in.Y.dtype}
        metr_dict = {
            f'{m.entry_name(a.name)}': np.dtype(m.dtype)
            for m in self.config.metrics
            for a in self.config.attrs
            if a in m.attributes
        }
        dtype_dict = attr_dict | xy_dict | metr_dict

        varlen_types = {a.dtype for a in self.config.attrs}

        # so tiledb knows how to fill null spots
        fillna_dict = {
            f'{m.entry_name(a.name)}': m.nan_value
            for m in self.config.metrics
            for a in self.config.attrs
        }
        fillna_dict['count'] = 0
        fillna_dict['shatter_process_num'] = 0

        # TileDB can't handle null cell writes for variable length arrays, so
        # make sure that any index in the dense block that doesn't have a value
        # is fill with a designated null value
        xi_vals = data_in.X
        yi_vals = data_in.Y
        xrange = range(xi_vals.min(), xi_vals.max() + 1)
        yrange = range(yi_vals.min(), yi_vals.max() + 1)
        mi = pd.MultiIndex.from_product([xrange, yrange], names=['X', 'Y'])
        d = data_in.set_index(['X', 'Y'])

        listed = d.reindex(mi)
        isna = listed[self.config.attrs[0].name].isna()
        if isna.any():
            listed = listed.fillna(fillna_dict)
            for attr, attr_type in attr_dict.items():
                dtype = attr_type.subtype
                kind = np.dtype(dtype).kind
                if kind in ['i', 'f']:
                    nan_value = -9999
                elif kind == 'u':
                    nan_value = 0
                else:
                    nan_value = -9999
                listed.loc[isna, attr] = pd.Series(
                    [np.array([nan_value], dtype=dtype)] * isna.sum()
                ).values
            data_in = listed.reset_index()

        # Date ranges are represented as integer days since the Unix epoch.
        # This keeps the TileDB schema readable by GDAL's raster driver.
        data_in = data_in.assign(
            start_time=np.datetime64(dates[0], 'D').astype(np.int64)
        ).assign(end_time=np.datetime64(dates[1], 'D').astype(np.int64))

        tiledb.from_pandas(
            uri=self.config.tdb_dir,
            # ctx=ctx,
            sparse=False,
            dataframe=data_in,
            mode='append',
            column_types=dtype_dict,
            varlen_types=varlen_types,
            fillna=fillna_dict,
            fit_to_df=True,
        )

    def reserve_time_slot(self) -> int:
        """
        Increment time slot in database and reserve that spot for a new
        shatter process.

        :param config: Shatter config will be written as metadata to reserve
        time slot.

        :return: Time slot.
        """
        # make sure we're dealing with the latest config
        cfg = self.get_config()
        self.config = cfg
        time = self.config.next_time_slot
        self.config.next_time_slot = time + 1
        self.save_config()

        return time

    def get_history(
        self,
        dates: Optional[tuple[datetime, datetime]] = None,
        bounds: Optional[Bounds] = None,
        name: Optional[str] = None,
        concise: bool = False,
    ):
        """
        Retrieve history of the database at current point in time.

        :param dates: Query parameter, tuple of start and end datetimes.
        :param bounds: Query parameter, bounds to query by.
        :param name: Query paramter, shatter process uuid., by default None
        :param concise: Whether or not to give shortened version of history.
        :return: Returns list of array fragments that meet query parameters.
        """
        if bounds is None:
            bounds = self.config.root

        m = []
        for idx in range(1, self.config.next_time_slot):
            s = self.get_shatter_meta(idx)
            if s.bounds.disjoint(bounds):
                continue

            # filter name
            if name is not None and name != s.name:
                continue

            # filter dates
            start_ts = s.date[0].timestamp()
            end_ts = s.date[1].timestamp()

            if dates is not None:
                q_start_ts = dates[0].timestamp()
                q_end_ts = dates[0].timestamp()
                if not ts_overlap((q_start_ts, q_end_ts), (start_ts, end_ts)):
                    continue

            if concise:
                h = s.history_json()
            else:
                h = s.to_json()
            m.append(h)

        return m

    def mbrs(self, config: ShatterConfig):
        """
        Get minimum bounding rectangle of a given shatter process. If this
        process has been finished and consolidated the mbr will be much less
        granulated than if the fragments are still intact. Mbrs are represented
        as tuples in the form of ((minx, maxx), (miny, maxy))

        :param timestamp: TileDB timestamp, a tuple of start and end datetime.
        :param bounds: :class:`silvimetric.resources.bounds.Bounds`

        """
        from .extents import Extents

        ex = Extents.from_sub(self, config.bounds)
        af_all = self.get_fragments(config.timestamp, config.bounds)
        mbrs_list = tuple(af.nonempty_domain for af in af_all)
        mbrs = tuple(
            tuple(tuple(a.item() for a in mb) for mb in m)
            for m in mbrs_list
            if not ex.disjoint_by_mbr(m)
        )
        return mbrs

    def get_fragments(
        self,
        timestamp: tuple[int, int],
        bounds: Optional[Bounds] = None,
        encompass: bool = False,
    ) -> list[tiledb.FragmentInfo]:
        """
        Get TileDB array fragments from the time slot specified.

        :param timestamp: TileDB timestamp, a tuple of start and end datetime.
        :param bounds: Bounds of desired fragments.
        :param encompass: If true, timestamps and bounds of fragments must be
            fully encompassed.
        :return: Array fragments from time slot.
        """
        from .extents import Extents

        overlap_method = ts_encompass if encompass else ts_overlap

        af = tiledb.array_fragments(self.config.tdb_dir, include_mbrs=True)
        if bounds is not None:
            ex = Extents.from_sub(self, bounds)
        fragments = []
        for a in af:
            if not overlap_method(timestamp, a.timestamp_range):
                continue
            if bounds is not None:
                if a.mbrs:
                    if all(ex.disjoint_by_mbr(mbr) for mbr in a.mbrs):
                        continue
                elif a.nonempty_domain:
                    if ex.disjoint_by_mbr(a.nonempty_domain):
                        continue
            fragments.append(a)
        return fragments

    def delete(self, config: ShatterConfig) -> ShatterConfig:
        """
        Delete Shatter process and overwrite associated data from database.

        :param config: :class:`silvimetric.resources.config.ShatterConfig`.
        :return: Config of deleted Shatter process
        """

        self.config.log.debug(f'Deleting shatter process {config.name}...')
        # grab fragments that are *fully* encompassed by the bounds and
        # timestamp provided, and then overwrite that data with nulls.
        # Consolidate at the end.
        fragments = self.get_fragments(
            timestamp=config.timestamp, bounds=config.bounds, encompass=True
        )
        for fragment in fragments:
            xs, ys = fragment.nonempty_domain
            x1, x2 = xs
            y1, y2 = ys
            x2 = x2 + 1
            y2 = y2 + 1


            # grab index values and recreate bounds from them
            xrange = np.arange(x1, x2, dtype=np.int64)
            yrange = np.arange(y1, y2, dtype=np.int64)
            mi = pd.MultiIndex.from_product([xrange, yrange], names=['X', 'Y'])
            dtype_arr = np.array([],dtype=[
                ('xi', np.int64),
                ('yi', np.int64),
                ('count', np.int64),
                ('shatter_process_num', np.uint16),
                *[(a.name, np.dtype('O')) for a in self.get_attributes()],
                *[
                    (m.entry_name(a.name), m.dtype)
                    for m in self.get_metrics()
                    for a in self.get_attributes()
                    if a in m.attributes or not len(m.attributes)
                ]
            ])
            null_df = pd.DataFrame(dtype_arr)
            null_df = null_df.set_index(['xi','yi']).reindex(mi)
            null_df = null_df.reset_index()
            null_dt = np.datetime64(0, 'D')
            self.write(null_df, (null_dt, null_dt))


        r = self.open('r')
        sh_cfg = ShatterConfig.from_string(
            r.meta[f'shatter_{config.time_slot}']
        )
        sh_cfg.mbr = ()
        sh_cfg.finished = False
        sh_cfg.start_timestamp = None
        sh_cfg.end_timestamp = None

        with self.open('w') as w:
            w.meta[f'shatter_{config.time_slot}'] = json.dumps(sh_cfg.to_json())

        return sh_cfg

    ManageType = Union[
        Literal['fragments', 'fragment_meta', 'commits', 'array_meta']
    ]

    def vacuum(self, mode: ManageType = 'fragments'):
        c = tiledb.Config(
            {
                'sm.vacuum.mode': mode,
            }
        )
        tiledb.vacuum(self.config.tdb_dir, config=c)

    def consolidate(
        self,
        mode: Optional[ManageType] = 'fragments',
        timestamp: Optional[tuple[int, int]] = None,
    ) -> None:
        """
        Consolidate the fragments from a shatter process into one fragment.
        This makes the database perform better, but reduces the granularity of
        time traveling.

        :param mode: TileDB consolidation mode.
        :param timestamp: TileDB timestamp, a tuple of start and end datetime.
        """
        ts_start = timestamp[0] if timestamp is not None else 0
        ts_end_def = int(datetime.now().timestamp() * 1000)
        ts_end = timestamp[1] if timestamp is not None else ts_end_def
        c = tiledb.Config(
            {
                'sm.consolidation.mode': mode,
                'sm.consolidation.timestamp_start': ts_start,
                'sm.consolidation.timestamp_end': ts_end,
            }
        )
        try:
            tiledb.consolidate(self.config.tdb_dir, ctx=tiledb.Ctx(c), config=c)
        except Exception as e:
            self.config.log.warning(f'{e.args}')

    @staticmethod
    def create_stage(
        source: 'Storage', stage_tdb_dir: str
    ) -> 'Storage':
        """Create a local, schema-identical TileDB array for stage-and-push.

        A stage is intentionally a new array, rather than a collection of
        copied fragment directories.  TileDB commits and dense-array metadata
        remain valid when the complete array is later published with VFS.
        """
        if tiledb.object_type(stage_tdb_dir) is not None:
            raise ValueError(
                f"Stage TileDB URI '{stage_tdb_dir}' already exists; "
                'use a new empty stage path for every run.'
            )
        stage_config = copy.deepcopy(source.config)
        stage_config.tdb_dir = stage_tdb_dir
        return Storage.create(stage_config)

    def set_stage_state(self, state: str, **details) -> None:
        """Persist small, durable stage state alongside the staged array."""
        payload = {'state': state, **details}
        with self.open('w') as writer:
            writer.meta['stage_n_push'] = json.dumps(payload, sort_keys=True)

    def stage_fragment_summary(self) -> list[dict]:
        """Return auditable local fragment domains and on-disk byte sizes."""
        vfs = tiledb.VFS()
        fragments = tiledb.array_fragments(
            self.config.tdb_dir, include_mbrs=True
        )
        result = []
        for fragment in fragments:
            result.append(
                {
                    'uri': fragment.uri,
                    'bytes': int(vfs.dir_size(fragment.uri)),
                    'timestamp_range': list(fragment.timestamp_range),
                    'nonempty_domain': [
                        [int(value) for value in domain]
                        for domain in (fragment.nonempty_domain or [])
                    ],
                }
            )
        return result

    def stage_consolidation_plan(self, fragment_size_mb: int) -> list[dict]:
        """Build TileDB's explicit local consolidation plan.

        The resulting groups are retained in the run timing/manifest so a
        published staged array is explainable and repeatable.  The desired
        size is a target, not a guarantee: dense-array NED expansion and
        variable-length point attributes determine the actual final size.
        """
        target_bytes = int(fragment_size_mb) * 1024 * 1024
        with tiledb.open(self.config.tdb_dir, 'r') as array:
            plan = tiledb.consolidation_plan.ConsolidationPlan(
                tiledb.default_ctx(), array, target_bytes
            )
            return [
                {
                    'fragment_uris': plan[node]['fragment_uris'],
                    'fragment_count': plan[node]['num_fragments'],
                }
                for node in range(len(plan))
            ]

    def consolidate_stage_plan(self, fragment_size_mb: int) -> int:
        """Consolidate local stage fragments using a freshly computed plan.

        TileDB consolidation rewrites fragments.  A plan with more than one
        node is therefore stale after its first node is applied: a new merged
        fragment can overlap a later node's non-empty domain.  Rebuild the
        plan after every merge so each submitted node is valid for the array's
        current fragment set.
        """
        consolidated = 0
        while True:
            plan = self.stage_consolidation_plan(fragment_size_mb)
            node = next(
                (
                    candidate
                    for candidate in plan
                    if len(candidate['fragment_uris']) >= 2
                ),
                None,
            )
            if node is None:
                return consolidated
            fragment_uris = node['fragment_uris']
            # TileDB's Python API requires fragment directory names here.
            names = [os.path.basename(uri.rstrip('/')) for uri in fragment_uris]
            tiledb.consolidate(
                self.config.tdb_dir,
                fragment_uris=names,
            )
            consolidated += 1

    def publish_stage(self, publish_uri: str, time_slot: int = 1) -> dict:
        """Materialize a validated local stage as a committed S3 array.

        ``VFS.copy_dir`` can copy the files beneath a local TileDB array to
        object storage without writing the commit records which make those
        fragments visible to TileDB.  Instead, read the locally validated
        cells and perform one normal TileDB write at the immutable destination.
        This keeps the expensive intermediate fragments off S3 while making
        the published shard independently readable.
        """
        vfs = tiledb.VFS()
        if (
            tiledb.object_type(publish_uri) is not None
            or vfs.is_dir(publish_uri)
        ):
            raise ValueError(
                f"Publish URI '{publish_uri}' already exists; stage-and-push "
                'only publishes to a new immutable URI.'
            )

        with self.open('r') as stage:
            staged_data = stage.df[:, :]
        staged_data = staged_data[staged_data['count'] > 0].copy()
        if staged_data.empty:
            raise RuntimeError(
                f"Stage TileDB URI '{self.config.tdb_dir}' has no cells to "
                'publish.'
            )

        stage_history = self.get_shatter_meta(time_slot)
        published_config = copy.deepcopy(self.config)
        published_config.tdb_dir = publish_uri
        published = Storage.create(published_config)
        published.write(
            staged_data.drop(columns=['start_time', 'end_time']).rename(
                columns={'X': 'xi', 'Y': 'yi'}
            ),
            stage_history.date,
        )
        published_history = copy.deepcopy(stage_history)
        published_history.tdb_dir = publish_uri
        published_history.point_count = int(staged_data['count'].sum())
        published_history.finished = True
        published_history.end_timestamp = int(datetime.now().timestamp() * 1000)
        published.save_shatter_meta(published_history)

        fragment_count = len(tiledb.array_fragments(publish_uri))
        if fragment_count == 0:
            raise RuntimeError(
                f"Published TileDB URI '{publish_uri}' has no readable "
                'fragments after commit.'
            )
        result = {
            'publish_uri': publish_uri,
            'published_fragment_count': fragment_count,
            'published_bytes': int(vfs.dir_size(publish_uri)),
        }
        # A fresh metadata write follows successful fragment discovery, making
        # this a durable indication that the published shard is readable.
        published.set_stage_state('s3_validated', **result)
        return result
