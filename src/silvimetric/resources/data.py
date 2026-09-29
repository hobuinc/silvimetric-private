import pathlib
import json
import copy
import logging
import os
from urllib.parse import urlparse
from typing import Optional

import tiledb
import pdal
import numpy as np
import pyproj

from .bounds import Bounds
from .config import StorageConfig
from .usgs_albers import USGS_ALBERS_CRS, USGS_ALBERS_HORIZONTAL_CRS


class Data:
    """Represents a point cloud or PDAL pipeline, and performs essential
    operations necessary to understand and execute a Shatter process."""

    def __init__(
        self,
        filename: str,
        storageconfig: StorageConfig,
        bounds: Optional[Bounds] = None,
        reader_collar: Optional[float] = None,
    ):
        self.filename = filename
        """Path to either PDAL pipeline or point cloud file"""

        # Alignment below is a processing detail.  Never mutate the Bounds
        # object owned by a caller's ShatterConfig: a resumable build must see
        # identical user inputs when a fresh driver reconstructs its ledger.
        self.bounds = copy.deepcopy(bounds)
        """Target/output bounds of this section of data."""

        if reader_collar is not None and reader_collar < 0:
            raise ValueError('reader_collar must be non-negative')
        self.reader_collar = reader_collar
        """Reader collar in CRS units; defaults to one storage resolution."""

        self.reader_thread_count = 2
        """Thread count for PDAL reader. Keep to 2 so we don't hog threads"""

        self.pdal_timing = os.environ.get(
            'SILVIMETRIC_PDAL_TIMING', ''
        ).lower() in {'1', 'true', 'yes'}
        """Emit PDAL per-stage timings when explicitly enabled by the runner."""

        self.storageconfig = storageconfig
        """:class:`silvimetric.resources.StorageConfig`"""

        self.pipeline = None
        """PDAL pipeline"""

        self.reader = self.get_reader()
        """PDAL reader"""

        self.source_crs = (
            self.get_crs(self.reader) if storageconfig.usgs_albers else None
        )
        """Source CRS used to transform canonical-profile reader bounds."""

        if storageconfig.usgs_albers:
            # Profile bounds are always in the target grid.  This lets a
            # caller select the same pixel-space rectangle in independently
            # built databases, while the reader query below is transformed
            # back to its native source CRS for hierarchy pruning.
            if self.bounds is None:
                self.bounds = self._transform_bounds(
                    Data.get_bounds(self.reader),
                    self.source_crs,
                    USGS_ALBERS_HORIZONTAL_CRS,
                )
            self.bounds.adjust_alignment(
                storageconfig.resolution,
                storageconfig.alignment,
                origin_x=storageconfig.root.minx,
                origin_y=storageconfig.root.maxy,
            )
        else:
            if self.bounds is None:
                self.bounds = Data.get_bounds(self.reader)
            self.bounds.adjust_alignment(
                storageconfig.resolution, storageconfig.alignment
            )

        self.bounds = Bounds.shared_bounds(self.bounds, storageconfig.root)
        # Preserve the long-standing normal-database behaviour: an input
        # outside the configured root is still readable (and will ultimately
        # contribute no in-root cells).  A canonical profile must fail early,
        # because a missing overlap would otherwise make its target CRS and
        # pixel-space contract ambiguous.
        if self.bounds is None and storageconfig.usgs_albers:
            raise ValueError('Input bounds do not overlap the storage root')

        self.pipeline = self.get_pipeline()

        self.log = storageconfig.log

    def to_json(self):
        j = dict(
            filename=self.filename,
            bounds=self.bounds.get(),
            pipeline=json.loads(self.pipeline.pipeline),
            is_pipeline=self.is_pipeline(),
        )
        if self.source_crs is not None:
            j['source_crs'] = self.source_crs.to_wkt()
        return j

    def __repr__(self):
        return json.dumps(self.to_json(), indent=2)

    def is_pipeline(self) -> bool:
        """Does this instance represent a pdal.Pipeline or a simple filename

        :return: Return true if input is a pipeline
        """

        if '://' in self.filename:
            parsed = urlparse(self.filename)
            p = pathlib.Path(parsed.path)
        else:
            p = pathlib.Path(self.filename)

        if p.suffix == '.json' and p.name != 'ept.json':
            return True
        return False

    def make_pipeline(self) -> pdal.Pipeline:
        """Take a COPC or EPT endpoint and generate a PDAL pipeline for it

        :return: Return PDAL pipeline
        """

        reader = pdal.Reader(self.filename, tag='reader')
        reader._options['threads'] = self.reader_thread_count
        self._apply_ept_options(reader)
        if self.bounds:
            reader._options['bounds'] = str(self._reader_bounds(self.bounds))

        return reader.pipeline()

    def get_pipeline(self) -> pdal.Pipeline:
        """Fetch the pipeline for the instance

        :raises Exception: File type isn't COPC or EPT
        :raises Exception: More than one reader detected
        :return: Return PDAL pipline
        """

        # If we are a pipeline, read and parse it. If we
        # aren't, go make_pipeline using some options that
        # process the data
        if self.is_pipeline():
            vfs = tiledb.VFS()
            pipeline_str = vfs.open(self.filename).read()
            stages = pdal.pipeline._parse_stages(pipeline_str)
            pipeline = pdal.Pipeline(stages)
        else:
            pipeline = self.make_pipeline()

        # only support COPC or EPT if someone gave us a pipeline
        # because we need to use bounds-accelerated reads to
        # process data quickly
        allowed_readers = ['copc', 'ept', 'tindex']
        readers = []
        stages = []

        for stage in pipeline.stages:
            stage_type, stage_kind = stage.type.split('.')
            if stage_type == 'readers':
                if stage_kind not in allowed_readers:
                    raise Exception(
                        "Readers for SilviMetric must be of type 'copc' or"
                        f"'ept', not '{stage_kind}'"
                    )
                readers.append(stage)

            self._apply_ept_options(stage)

            # Bounds are applied at both levels of a tindex read.  The tindex
            # stage uses them to avoid opening irrelevant tiles, while its
            # embedded COPC reader uses them to prune the COPC hierarchy.
            if stage_kind in allowed_readers:
                if self.bounds:
                    res = (
                        self.reader_collar
                        if self.reader_collar is not None
                        else self.storageconfig.resolution
                    )
                    collar = self._reader_bounds(self.bounds, collar=res)
                    self._apply_query_options(stage, collar)

            # We strip off any writers from the pipeline that were
            # given to us and drop them  on the floor
            if stage_type != 'writers':
                stages.append(stage)
                if stage_type == 'readers':
                    # LAS classes 7 and 18 are low and high noise. Remove
                    # them while the source Classification still exists:
                    # caller pipelines may reset it for SMRF/HAG, which
                    # would otherwise make these points indistinguishable
                    # from valid returns during metric aggregation.
                    stages.append(
                        pdal.Filter.expression(
                            expression=(
                                'Classification != 7 && Classification != 18'
                            )
                        )
                    )

        # we don't support weird pipelines of shapes
        # that aren't simply a line.
        if len(readers) != 1:
            raise Exception(
                f'Pipelines can only have one reader of type {allowed_readers}'
            )

        if self.storageconfig.usgs_albers:
            # Reprojection belongs after caller-provided source filters and
            # after the native-CRS reader constraint, but before the output
            # pixel coordinate assignment.  Crop in target CRS after the
            # source collar has given upstream filters their edge context.
            stages.append(pdal.Filter.reprojection(out_srs=USGS_ALBERS_CRS))
            stages.append(pdal.Filter.crop(bounds=str(self.bounds)))

        resolution = self.storageconfig.resolution
        # Add xi and yi, only need this for PDAL < 2.6
        ferry = pdal.Filter.ferry(dimensions='X=>xi, Y=>yi')
        assign_x = pdal.Filter.assign(
            value=f'xi = (X - {self.storageconfig.root.minx}) / {resolution}'
        )
        # The canonical profile's fixed maximum Y is the upper *outer edge*
        # of row zero.  Pixel-is-area rows therefore use the direct distance
        # from that edge.  Retain the historic centre-aligned expression for
        # ordinary databases so this opt-in profile cannot alter their stored
        # indices.
        y_offset = 0 if self.storageconfig.usgs_albers else 1
        assign_y = pdal.Filter.assign(
            value=f'yi = (({self.storageconfig.root.maxy} - Y) / '
            f'{resolution}) - {y_offset}'
        )

        stages.append(ferry)
        stages.append(assign_x)
        stages.append(assign_y)

        # return our pipeline
        a = pdal.Pipeline(
            stages,
            loglevel=logging.DEBUG if self.pdal_timing else logging.ERROR,
            timing=self.pdal_timing,
        )
        return a

    def execute(self, allowed_dims: Optional[list[str]] = None):
        """Execute PDAL pipeline
        :param allowed_dims: List of PDAL Dimension names to fetch from PDAL.
        :raises Exception: PDAL error message passed from execution
        """
        try:
            # if allowed_dims is not None:
            #     self.pipeline.execute(allowed_dims=allowed_dims)
            # else:
            self.pipeline.execute()
            if self.pipeline.log and self.pipeline.log is not None:
                log = self.log.info if self.pdal_timing else self.log.debug
                log(f'PDAL log: {self.pipeline.log}')
        except Exception as e:
            if self.pipeline.log and self.pipeline.log is not None:
                self.log.debug(f'PDAL log: {self.pipeline.log}')
            msg = (
                f'Error: {e} when executing pipeline: '
                f'{self.pipeline.pipeline}'
            )
            self.storageconfig.log.error(msg)
            raise e

    def get_array(self) -> np.ndarray:
        """Fetch the array from the execute()'d pipeline

        :return: get data as a numpy ndarray
        """
        return self.pipeline.arrays[0]

    array = property(get_array)

    def get_reader(self) -> pdal.Reader:
        """Grab or make the reader for this instance so we can use it to do
        things like get the count()

        :return: get PDAL reader for input
        """
        if self.is_pipeline():
            if self.pipeline is None:
                vfs = tiledb.VFS()
                pipeline_str = vfs.open(self.filename).read()
                stages = pdal.pipeline._parse_stages(pipeline_str)
            else:
                stages = self.pipeline.stages

            for stage in stages:
                stage_type, _ = stage.type.split('.')
                if stage_type == 'readers':
                    return stage
        else:
            reader = pdal.Reader(self.filename)
            reader._options['threads'] = self.reader_thread_count
            self._apply_ept_options(reader)
            return reader

    @staticmethod
    def _apply_ept_options(reader: pdal.Reader) -> None:
        """Make public EPT reads resilient to an unreadable hierarchy tile.

        USGS EPT is served from public S3, where an occasional object read can
        fail independently of the surrounding hierarchy.  PDAL can continue
        by omitting that tile; this is preferable to aborting a long shatter
        run after its bounded source read has already made progress.
        """
        if reader.type == 'readers.ept':
            reader._options['ignore_unreadable'] = True

    @staticmethod
    def _tindex_reader_args(
        reader_args, bounds: Bounds | None, resolution: float | None
    ) -> list[dict]:
        """Merge a bounded COPC reader entry into tindex ``reader_args``.

        ``readers.tindex`` owns the tile pre-filter, but it doesn't propagate
        a resolution option into its child readers.  PDAL documents
        ``reader_args`` as pipeline-stage-shaped JSON objects; keeping the
        COPC entry there applies the same bounded low-resolution query to each
        selected COPC tile.
        """
        if isinstance(reader_args, str):
            reader_args = json.loads(reader_args)
        args = copy.deepcopy(reader_args or [])
        if isinstance(args, dict):
            args = [args]
        if not isinstance(args, list):
            raise ValueError('readers.tindex.reader_args must be a JSON list')

        copc_args = None
        for entry in args:
            if entry.get('type') == 'readers.copc':
                copc_args = entry
                break
        if copc_args is None:
            copc_args = {'type': 'readers.copc'}
            args.append(copc_args)
        if bounds is not None:
            copc_args['bounds'] = str(bounds)
        if resolution is not None:
            copc_args['resolution'] = resolution
        return args

    @classmethod
    def _apply_query_options(
        cls,
        reader: pdal.Reader,
        bounds: Bounds | None,
        resolution: float | None = None,
    ) -> None:
        """Apply a bounded query to direct COPC/EPT or a COPC tindex reader."""
        if bounds is not None:
            reader._options['bounds'] = str(bounds)
        if reader.type == 'readers.tindex':
            reader._options['reader_args'] = cls._tindex_reader_args(
                reader._options.get('reader_args'), bounds, resolution
            )
        elif resolution is not None:
            reader._options['resolution'] = resolution

    @staticmethod
    def get_bounds(reader: pdal.Reader) -> Bounds:
        """Get the bounding box of a point cloud from PDAL.

        :param reader: PDAL Reader representing input data
        :return: bounding box of point cloud
        """
        p = reader.pipeline()
        qi = p.quickinfo[reader.type]
        return Bounds.from_string(json.dumps(qi['bounds']))

    @staticmethod
    def get_crs(reader: pdal.Reader) -> pyproj.CRS:
        """Return the source reader's CRS, rejecting unreferenced inputs."""
        qi = reader.pipeline().quickinfo[reader.type]
        srs = qi.get('srs')
        if isinstance(srs, dict):
            # PDAL exposes both a horizontal WKT and a compound WKT.  Retain
            # vertical provenance when available; pyproj derives a 2-D view
            # for the bounds transform below.
            srs = (
                srs.get('compoundwkt')
                or srs.get('wkt')
                or srs.get('horizontal')
            )
        if not srs:
            raise ValueError('The input reader does not advertise a CRS')
        return pyproj.CRS.from_user_input(srs)

    @staticmethod
    def _horizontal_crs(crs: pyproj.CRS) -> pyproj.CRS:
        """Return the horizontal component accepted by ``transform_bounds``."""
        return crs.to_2d() if crs.is_compound else crs

    @classmethod
    def _transform_bounds(
        cls,
        bounds: Bounds,
        source_crs: pyproj.CRS,
        target_crs: pyproj.CRS,
    ) -> Bounds:
        """Densify and transform a rectangular extent between CRS spaces.

        ``pyproj`` is normally the fastest and most direct route.  In the
        Linux/aarch64 worker environment, however, PROJ can occasionally
        be initialized in an unusable state and return four infinities rather
        than raising.  Do not hand that malformed extent to PDAL: use GDAL's
        independently initialized OSR transform as a bounded fallback.
        """
        source = cls._horizontal_crs(source_crs)
        target = cls._horizontal_crs(target_crs)
        try:
            values = pyproj.Transformer.from_crs(
                source, target, always_xy=True
            ).transform_bounds(
                bounds.minx,
                bounds.miny,
                bounds.maxx,
                bounds.maxy,
                densify_pts=21,
            )
        except pyproj.exceptions.ProjError:
            values = None
        if values is not None and np.isfinite(values).all():
            return Bounds(*values)
        return cls._transform_bounds_osr(bounds, source, target)

    @staticmethod
    def _transform_bounds_osr(
        bounds: Bounds, source_crs: pyproj.CRS, target_crs: pyproj.CRS
    ) -> Bounds:
        """Transform sampled rectangle edges with GDAL/OSR as a safe fallback."""
        from osgeo import osr

        osr.UseExceptions()
        source = osr.SpatialReference()
        source.ImportFromWkt(source_crs.to_wkt())
        target = osr.SpatialReference()
        target.ImportFromWkt(target_crs.to_wkt())
        # Both pyproj's normal path and Silvimetric's point dimensions use
        # traditional X/Y order.  Make that order explicit under GDAL 3+.
        if hasattr(osr, 'OAMS_TRADITIONAL_GIS_ORDER'):
            source.SetAxisMappingStrategy(osr.OAMS_TRADITIONAL_GIS_ORDER)
            target.SetAxisMappingStrategy(osr.OAMS_TRADITIONAL_GIS_ORDER)
        transform = osr.CoordinateTransformation(source, target)
        samples = np.linspace(0.0, 1.0, 22)
        points = [
            (bounds.minx + (bounds.maxx - bounds.minx) * fraction, bounds.miny)
            for fraction in samples
        ]
        points.extend(
            (bounds.minx + (bounds.maxx - bounds.minx) * fraction, bounds.maxy)
            for fraction in samples
        )
        points.extend(
            (bounds.minx, bounds.miny + (bounds.maxy - bounds.miny) * fraction)
            for fraction in samples
        )
        points.extend(
            (bounds.maxx, bounds.miny + (bounds.maxy - bounds.miny) * fraction)
            for fraction in samples
        )
        transformed = np.asarray(transform.TransformPoints(points), dtype=float)
        if transformed.size == 0 or not np.isfinite(transformed[:, :2]).all():
            raise ValueError(
                'Unable to transform finite bounds between '
                f'{source_crs.to_string()} and {target_crs.to_string()}'
            )
        return Bounds(
            transformed[:, 0].min(),
            transformed[:, 1].min(),
            transformed[:, 0].max(),
            transformed[:, 1].max(),
        )

    def _reader_bounds(
        self, target_bounds: Bounds, collar: float = 0.0
    ) -> Bounds:
        """Convert target-grid bounds and a target-unit collar to reader CRS."""
        if collar < 0:
            raise ValueError('reader collar must be non-negative')
        if not self.storageconfig.usgs_albers:
            return Bounds(
                target_bounds.minx - collar,
                target_bounds.miny - collar,
                target_bounds.maxx + collar,
                target_bounds.maxy + collar,
            )
        expanded = Bounds(
            target_bounds.minx - collar,
            target_bounds.miny - collar,
            target_bounds.maxx + collar,
            target_bounds.maxy + collar,
        )
        return self._transform_bounds(
            expanded,
            USGS_ALBERS_HORIZONTAL_CRS,
            self.source_crs,
        )

    def estimate_count(
        self, bounds: Bounds, reader_resolution: Optional[float] = None
    ) -> int:
        """Estimate points in ``bounds`` with PDAL metadata or a coarse read.

        ``reader_resolution`` maps to the active COPC/EPT reader's
        ``resolution`` option.  It permits a planner to query a coarser LOD
        without changing the reader instance used by a subsequent shatter.

        :param bounds: query bounding box
        :param reader_resolution: optional coarse COPC/EPT resolution
        :return: estimated point count
        """
        reader = copy.deepcopy(self.get_reader())
        if reader_resolution is not None:
            if reader_resolution <= 0:
                raise ValueError('reader_resolution must be positive')
        reader_bounds = self._reader_bounds(bounds) if bounds is not None else None
        self._apply_query_options(reader, reader_bounds, reader_resolution)

        pipeline = reader.pipeline()
        if reader_resolution is None:
            # Retain the historical metadata-only behavior for callers such
            # as the legacy quadtree.  PDAL quickinfo is not a bounded point
            # count for COPC/EPT, so it must not be used by the shard planner.
            qi = pipeline.quickinfo[reader.type]
            return qi['num_points']

        # This is the Python equivalent of ``pdal info --summary`` with a
        # reader resolution.  Unlike quickinfo, executing the bounded reader
        # causes COPC/EPT to select only the requested low-resolution nodes,
        # and the returned array count is therefore window-specific.
        try:
            pipeline.execute()
        except RuntimeError as error:
            raise RuntimeError(
                f'{reader.type} bounded density query failed: '
                f'target_bounds={bounds.get() if bounds is not None else None}, '
                f'reader_bounds={reader_bounds.get() if reader_bounds is not None else None}, '
                f'resolution={reader_resolution}. {error}'
            ) from error
        if not pipeline.arrays:
            return 0
        return len(pipeline.arrays[0])

    def count(self, bounds: Optional[Bounds] = None) -> int:
        """For the provided bounds, read and count the number of points that are
        inside their complete output-cell footprint for this instance.

        :param bounds: query bounding box
        :return: point count
        """

        reader = copy.deepcopy(self.get_reader())
        query_bounds = copy.deepcopy(bounds)
        if query_bounds is not None:
            query_bounds.adjust_alignment(
                self.storageconfig.resolution,
                self.storageconfig.alignment,
                origin_x=(
                    self.storageconfig.root.minx
                    if self.storageconfig.usgs_albers
                    else None
                ),
                origin_y=(
                    self.storageconfig.root.maxy
                    if self.storageconfig.usgs_albers
                    else None
                ),
            )
        self._apply_query_options(
            reader,
            self._reader_bounds(query_bounds)
            if query_bounds is not None
            else None,
        )

        pipeline = reader.pipeline()
        pipeline.execute()
        if not pipeline.arrays:
            return 0
        return len(pipeline.arrays[0])
