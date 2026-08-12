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

from .bounds import Bounds
from .config import StorageConfig


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

        self.bounds = bounds
        """Bounds of this section of data"""

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

        if self.bounds is None:
            self.bounds = Data.get_bounds(self.reader)

        # adjust bounds if necessary
        self.bounds.adjust_alignment(
            storageconfig.resolution, storageconfig.alignment
        )
        self.bounds = Bounds.shared_bounds(self.bounds, storageconfig.root)

        self.pipeline = self.get_pipeline()

        self.log = storageconfig.log

    def to_json(self):
        j = dict(
            filename=self.filename,
            bounds=self.bounds.get(),
            pipeline=json.loads(self.pipeline.pipeline),
            is_pipeline=self.is_pipeline(),
        )
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
            reader._options['bounds'] = str(self.bounds)

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
                    collar = Bounds(
                        self.bounds.minx - res,
                        self.bounds.miny - res,
                        self.bounds.maxx + res,
                        self.bounds.maxy + res,
                    )
                    self._apply_query_options(stage, collar)

            # We strip off any writers from the pipeline that were
            # given to us and drop them  on the floor
            if stage_type != 'writers':
                stages.append(stage)

        # we don't support weird pipelines of shapes
        # that aren't simply a line.
        if len(readers) != 1:
            raise Exception(
                    f'Pipelines can only have one reader of type {allowed_readers}'
            )

        resolution = self.storageconfig.resolution
        # Add xi and yi, only need this for PDAL < 2.6
        ferry = pdal.Filter.ferry(dimensions='X=>xi, Y=>yi')
        assign_x = pdal.Filter.assign(
            value=f'xi = (X - {self.storageconfig.root.minx}) / {resolution}'
        )
        assign_y = pdal.Filter.assign(
            value=f'yi = (({self.storageconfig.root.maxy} - Y) / '
            f'{resolution}) - 1'
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
        self._apply_query_options(reader, bounds, reader_resolution)

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
        pipeline.execute()
        if not pipeline.arrays:
            return 0
        return len(pipeline.arrays[0])

    def count(self, bounds: Optional[Bounds] = None) -> int:
        """For the provided bounds, read and count the number of points that are
        inside them for this instance.

        :param bounds: query bounding box
        :return: point count
        """

        reader = copy.deepcopy(self.get_reader())
        self._apply_query_options(reader, bounds)

        pipeline = reader.pipeline()
        pipeline.execute()
        if not pipeline.arrays:
            return 0
        return len(pipeline.arrays[0])
