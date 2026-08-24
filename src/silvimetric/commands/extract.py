from pathlib import Path
from datetime import date

from osgeo import gdal, osr
import dask
import numpy as np
import pandas as pd
import tiledb
from distributed.client import _get_global_client as get_client

from .. import Storage, Extents, ExtractConfig, Bounds

np_to_gdal_types = {
    np.dtype(np.byte).str: gdal.GDT_Byte,
    np.dtype(np.uint8).str: gdal.GDT_Byte,
    np.dtype(np.int8).str: gdal.GDT_Int8,
    np.dtype(np.uint16).str: gdal.GDT_UInt16,
    np.dtype(np.int16).str: gdal.GDT_Int16,
    np.dtype(np.uint32).str: gdal.GDT_UInt32,
    np.dtype(np.int32).str: gdal.GDT_Int32,
    np.dtype(np.uint64).str: gdal.GDT_UInt64,
    np.dtype(np.int64).str: gdal.GDT_Int64,
    np.dtype(np.float32).str: gdal.GDT_Float32,
    np.dtype(np.float64).str: gdal.GDT_Float64,
}


def write_tif(
    bounds: Bounds,
    data: np.ndarray,
    nan_val: float | int,
    name: str,
    dtype: np.dtype,
    config: ExtractConfig,
) -> None:
    """
    Write out a raster with GDAL

    :param xsize: Length of X plane.
    :param ysize: Length of Y plane.
    :param data: Data to write to raster.
    :param name: Name of raster to write.
    :param config: ExtractConfig.
    """
    osr.UseExceptions()
    path = Path(config.out_dir) / f'{name}.tif'
    crs = config.crs
    srs = osr.SpatialReference()
    srs.ImportFromWkt(crs.to_wkt())
    minx, _miny, _maxx, maxy = bounds.get()
    ysize, xsize = data.shape

    transform = [
        minx,
        config.resolution,
        0,
        maxy,
        0,
        -1 * config.resolution,
    ]

    driver = gdal.GetDriverByName('GTiff')
    gdal_type = np_to_gdal_types[dtype.str]
    tif = driver.Create(
        str(path),
        int(xsize),
        int(ysize),
        1,
        gdal_type,
    )
    tif.SetGeoTransform(transform)
    tif.SetProjection(srs.ExportToWkt())
    # ``transform`` describes the *outer* raster edges, not pixel centers:
    # pixel (0, 0) is centered one half-cell right/down from (minx, maxy).
    # State this explicitly so GDAL consumers do not reinterpret the
    # coordinates as PixelIsPoint (the convention used by FUSION's /gridxy
    # command-line cell-center arguments).
    tif.SetMetadataItem('AREA_OR_POINT', 'Area')
    tif.GetRasterBand(1).SetNoDataValue(nan_val)
    tif.GetRasterBand(1).WriteArray(data)
    tif.FlushCache()
    tif = None


def get_data(
    config: ExtractConfig, storage: Storage, extents: Extents
) -> pd.DataFrame:
    """
    Handle cells that have overlapping data. We have to re-run metrics over
    these cells as there's no other accurate way to determined metric values.
    If there are no overlaps, this will do nothing.

    :param config: ExtractConfig.
    :param storage: Database storage object.
    :param indices: Indices with overlap.
    :return: Dataframe of rerun data.
    """

    ma_list = storage.get_derived_names(config.metrics, config.attrs)

    with storage.open('r') as tdb:
        # New arrays store date ranges as epoch days for GDAL TileDB
        # compatibility.  Older arrays used TileDB DATETIME_DAY, for which we
        # retain the pandas-level comparison below.
        start_datetime = (
            np.datetime64(config.date[0], 'D').astype(np.int64).item()
        )
        end_datetime = (
            np.datetime64(config.date[1], 'D').astype(np.int64).item()
        )
        cond = f'end_time >= {start_datetime} and start_time <= {end_datetime}'
        xdim = tdb.schema.domain.dim('X').domain
        ydim = tdb.schema.domain.dim('Y').domain
        minx = max(extents.x1, xdim[0])
        maxx = min(extents.x2, xdim[1])
        miny = max(extents.y1, ydim[0])
        maxy = min(extents.y2, ydim[1])

        # older versions of silvimetric supported multiple values, and
        # for backwards compatibility we will try to accept it still
        data = tdb.query(
            attrs=[*ma_list, 'end_time', 'start_time'],
            order='F',
            cond=cond,
            coords=True,
        ).df[minx : maxx - 1, miny : maxy - 1]

        if np.issubdtype(data.end_time.dtype, np.datetime64):
            start_filter = np.datetime64(config.date[0], 'D')
            end_filter = np.datetime64(config.date[1], 'D')
        elif data.end_time.dtype == object and (
            data.end_time.dropna().map(lambda value: isinstance(value, date)).any()
        ):
            start_filter = config.date[0].date()
            end_filter = config.date[1].date()
        else:
            start_filter = start_datetime
            end_filter = end_datetime
        data = data[data.end_time >= start_filter]
        data = data[data.start_time <= end_filter]

        # find values that are not unique, means they have multiple entries
        # TODO phase this out at some point, storage is no longer created
        # with duplicate entries as an option
        data = data.set_index(['Y', 'X'])
        dedup_data = data[data.index.duplicated(keep='last')]
        clean_data = data[~data.index.duplicated(False)]
        return pd.concat([clean_data, dedup_data])


def get_shard_data(
    config: ExtractConfig, shard_uri: str, extents: Extents
) -> pd.DataFrame:
    """Open and read one immutable shard on the worker that owns the task."""
    return get_data(config, Storage.from_db(shard_uri), extents)


def extract(config: ExtractConfig) -> None:
    """
    Pull data from database for each desired metric and output them to rasters

    :param config: ExtractConfig.
    """

    dask.config.set({'dataframe.convert-string': False})

    if tiledb.object_type(
        config.tdb_dir, ctx=Storage.get_tdb_context()
    ) == 'group':
        storages = Storage.shard_members(config.tdb_dir)
    else:
        storages = [Storage.from_db(config.tdb_dir)]
    storage = storages[0]
    schema = storage.open('r').schema
    ma_list = storage.get_derived_names(config.metrics, config.attrs)
    config.log.debug(f'Extracting metrics {[m for m in ma_list]}')
    root_bounds = storage.config.root

    e = Extents(
        config.bounds,
        config.resolution,
        storage.config.alignment,
        root=root_bounds,
    )
    cell_size = 0
    for a in config.attrs:
        for m in config.metrics:
            if a in m.attributes:
                cell_size = cell_size + np.dtype(m.dtype).itemsize

    client = get_client()
    if client is None or len(storages) == 1:
        frames = [get_data(config, member, e) for member in storages]
    else:
        # Shards are independent immutable arrays.  Read them on the Dask
        # workers while the shatter fleet is still available, then preserve
        # the group order when gathering for the existing dedup semantics.
        futures = [
            client.submit(get_shard_data, config, member.config.tdb_dir, e)
            for member in storages
        ]
        frames = client.gather(futures)
    final = pd.concat(frames)
    # Spatial shards are expected to be disjoint.  Keep the newest value if a
    # future overlapping shard is present, matching existing TileDB semantics.
    final = final[~final.index.duplicated(keep='last')]
    futures = []
    for ma in ma_list:
        dtype = schema.attr(ma).dtype
        nan_val = -9999 if dtype.kind in ['i', 'f'] else 0
        if dtype.kind == 'u':
            nan_val = 0
        elif dtype.kind in ['i', 'f']:
            nan_val = -9999
        else:
            nan_val = 0
        unstacked = final[ma].unstack()
        unstacked = unstacked.fillna(nan_val)
        m_data = unstacked.to_numpy()

        futures.append(
            dask.delayed(write_tif)(
                e.bounds, m_data, nan_val, ma, dtype, config
            )
        )

    # Shard reads above can use an active distributed client.  Raster writes
    # deliberately stay on the process that owns ``config.out_dir``: worker
    # hosts do not share that filesystem, and their output would not be part
    # of the caller's extract result.
    if client is None:
        dask.compute(*futures)
    else:
        dask.compute(*futures, scheduler="threads")
