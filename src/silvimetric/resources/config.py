import pyproj
import tiledb

import os
import json
import uuid
import math

from pathlib import Path
from abc import ABC, abstractmethod
from typing_extensions import Union, Tuple
from datetime import datetime

from dataclasses import dataclass, field

from .log import Log
from .extents import Bounds
from .metric import Metric
from .metrics import grid_metrics
from .attribute import Attribute, Attributes
from .. import __version__

from threading import Lock

mutex = Lock()


@dataclass(kw_only=True)
class Config(ABC):
    """Base config"""

    tdb_dir: str = field()
    """Path to TileDB directory to use."""
    log: Log = field(default_factory=lambda: Log('INFO'))
    """Log object."""
    debug: bool = field(default=False)
    """Debug flag."""

    def to_json(self):
        keys = self.__dataclass_fields__.keys()
        d = {}
        for k in keys:
            if k == 'tdb_dir':
                tdb_dir = self.__dict__[k]
                if '://' not in tdb_dir:
                    d[k] = os.path.abspath(tdb_dir)
                else:
                    d[k] = tdb_dir
            else:
                d[k] = self.__dict__[k]
        if not isinstance(d['log'], dict):
            d['log'] = d['log'].to_json()
        return d

    @classmethod
    def from_json(self, data: str):
        return self.from_string(json.dumps(data))

    @classmethod
    @abstractmethod
    def from_string(self, data: str):
        raise NotImplementedError

    def __repr__(self):
        return json.dumps(self.to_json())


@dataclass
class StorageConfig(Config):
    """Config for constructing a Storage object"""

    root: Bounds = field()
    """Root project bounding box"""
    crs: pyproj.CRS = field()
    """Coordinate reference system, same for all data in a project"""
    resolution: float = field(default=30.0)
    """Resolution of cells, same for all data in a project, defaults to 30.0"""
    alignment: str = field(default='AlignToCenter')
    """Alignment of pixels in database, same for all data in a project,
    options: 'AlignToCenter' or 'AlignToCorner', defaults to 'AlignToCenter'"""
    xsize: int = field(default=1000)
    """TileDB X Tile size for IO operations."""
    ysize: int = field(default=1000)
    """TileDB Y Tile size for IO operations."""

    attrs: list[Attribute] = field(
        default_factory=lambda: [
            Attribute(a, Attributes[a].dtype)
            for a in ['Z', 'NumberOfReturns', 'ReturnNumber', 'Intensity']
        ]
    )
    """List of :class:`silvimetric.resources.attribute.Attribute` attributes
    that represent point data, defaults to Z, NumberOfReturns, ReturnNumber,
    Intensity"""
    metrics: list[Metric] = field(
        default_factory=lambda: list(grid_metrics.get_grid_metrics().values())
    )
    """List of :class:`silvimetric.resources.metrics.grid_metrics` grid_metrics
    that represent derived data, defaults to values in grid_metrics object"""
    version: str = field(default=__version__)
    """Silvimetric version"""
    capacity: int = field(default=1000000)
    """TileDB Capacity, defaults to 1000000"""
    next_time_slot: int = field(default=1)
    """Next time slot to be allocated to a shatter process. Increment after
    use., defaults to 1"""

    def __post_init__(self) -> None:
        crs = self.crs
        if isinstance(crs, dict):
            crs = json.loads(crs)
        elif isinstance(crs, pyproj.CRS):
            self.crs = crs
        else:
            self.crs = pyproj.CRS.from_user_input(crs)

        if not self.crs.is_projected:
            raise Exception(
                'Given coordinate system is not a rectilinear'
                ' projected coordinate system'
            )

    def __eq__(self, other):
        # We don't compare logs
        for k in other.__dict__.keys():
            if k != 'log':
                if self.__dict__[k] != other.__dict__[k]:
                    return False
        return True

    def to_json(self):
        d = super().to_json()

        d['attrs'] = [a.to_json() for a in self.attrs]
        d['metrics'] = [m.to_json() for m in self.metrics]
        d['crs'] = json.loads(self.crs.to_json())
        d['root'] = self.root.to_json()
        d['xsize'] = self.xsize
        d['ysize'] = self.ysize
        d['next_time_slot'] = self.next_time_slot

        return d

    @classmethod
    def from_string(cls, data: str):
        x = json.loads(data)
        root = Bounds(*x['root'])
        if 'metrics' in x:
            with mutex:
                ms = [Metric.from_dict(m) for m in x['metrics']]
        else:
            ms = []
        if 'attrs' in x:
            attrs = [Attribute.from_dict(a) for a in x['attrs']]
        else:
            attrs = []
        if 'crs' in x:
            crs = pyproj.CRS.from_user_input(json.dumps(x['crs']))
        else:
            crs = None

        n = cls(
            tdb_dir=x['tdb_dir'],
            root=root,
            log=Log(**x['log']),
            resolution=x['resolution'],
            alignment=x['alignment'],
            attrs=attrs,
            crs=crs,
            metrics=ms,
            capacity=x['capacity'],
            version=x['version'],
            next_time_slot=x['next_time_slot'],
            xsize=x['xsize'],
            ysize=x['ysize'],
        )

        return n

    def __repr__(self):
        j = self.to_json()
        return json.dumps(j)


@dataclass
class ApplicationConfig(Config):
    """Base application config"""

    debug: bool = field(default=False)
    """Debug mode, defaults to False"""

    # Dask configuration
    dasktype: str = field(default='processes')
    """Dask parallelization type. For information see
    https://docs.dask.org/en/stable/scheduling.html#local-threads """
    scheduler: str = field(default='distributed')
    """Dask scheduler, defaults to 'distributed'"""
    workers: int = field(default=12)
    """Number of dask workers"""
    threads: int = field(default=4)
    """Number of threads per dask worker"""
    watch: bool = field(default=False)
    """Open dask diagnostic page in default web browser"""

    def to_json(self):
        d = super().to_json()
        return d

    @classmethod
    def from_string(cls, data: str):
        x = json.loads(data)
        n = cls(
            tdb_dir=x['tdb_dir'],
            debug=x['debug'],
            dasktype=x['dasktype'],
            scheduler=x['scheduler'],
            workers=x['workers'],
            threads=x['threads'],
            watch=x['watch'],
        )
        return n

    def __repr__(self):
        return json.dumps(self.to_json())


Mbr = tuple[tuple[tuple[int, int], tuple[int, int]], ...]


@dataclass
class ShatterConfig(Config):
    """Config for Shatter process"""

    filename: str
    """Input filename referencing a PDAL pipeline or point cloud file."""
    date: Tuple[datetime, datetime] = field(default=None)
    """A date range representing data collection times."""
    bounds: Union[Bounds, None] = field(default=None)
    """The bounding box of the shatter process., defaults to None"""
    name: uuid.UUID = field(default=uuid.uuid4())
    """UUID representing this shatter process and will be generated if not
    provided., defaults to uuid.uuid()"""
    tile_size: Union[int, None] = field(default=None)
    """The number of cells to include in a tile., defaults to None"""
    processing_strategy: str = field(default='leaf-v1')
    """Execution strategy: ``leaf-v1``, ``macro-v2``, or staged macro-v3."""
    read_group_size: Union[int, None] = field(default=None)
    """Number of cells in a square macro read group for ``macro-v2``."""
    processing_halo_m: Union[float, None] = field(default=None)
    """Reader collar in CRS units. Defaults to one storage resolution cell."""
    execution_timing: dict = field(default_factory=dict)
    """Executor timing summary populated by shatter after a completed run."""
    macro_diagnostics: bool = field(default=False)
    """Record per-macro worker RSS and phase timing for a bounded profiling run."""
    stage_tdb_dir: Union[str, None] = field(default=None)
    """Local TileDB URI owned by the macro-v3 stage writer."""
    stage_publish_uri: Union[str, None] = field(default=None)
    """New, immutable TileDB URI to receive the validated staged array."""
    stage_fragment_size_mb: int = field(default=300)
    """Desired local TileDB consolidation-plan fragment size in MiB."""
    stage_shard_side_macros: int = field(default=4)
    """Number of adjacent macro reads along one side of a published shard."""
    stage_shard_target_points: Union[int, None] = field(default=None)
    """Maximum coarse-quickinfo upper-bound point estimate per S3 shard."""
    stage_planner_resolution_multiple: int = field(default=8)
    """Coarse PDAL reader resolution as a multiple of storage resolution."""
    stage_planner_sample_point_multiplier: Union[float, None] = field(
        default=None
    )
    """Calibrated raw-points-per-coarse-sample-point multiplier for a source."""
    stage_planner_calibration_sample_count: int = field(default=4)
    """Number of bounded native-resolution reads used to calibrate a shard plan."""
    stage_planner_calibration_window_m: float = field(default=250.0)
    """Side length, in CRS units, of each bounded native-resolution sample."""
    stage_planner_density_safety_factor: float = field(default=1.25)
    """Inflation applied to measured source density before shard splitting."""
    stage_worker_address: Union[str, None] = field(default=None)
    """Optional Dask worker address on which to place the stage writer actor."""
    start_timestamp: float = field(default=None)
    """The process start timestamp., defaults to None"""
    end_timestamp: float = field(default=None)
    """The process ending timestamp., defaults to None"""
    point_count: int = field(default=0)
    """The number of points that has been processed so far., defaults to 0"""
    tile_point_count: int = field(default=600 * 10**3)  # 600k
    """Target number of points per Tile. Only used if tile_size is None.
    defaults to 600000"""
    mbr: Mbr = field(default_factory=lambda: tuple())
    """The minimum bounding rectangle derived from TileDB array fragments.
    This will be used to for resuming shatter processes and making sure it
    doesn't repeat work., defaults to tuple()"""
    finished: bool = False
    """Finished flag for shatter process., defaults to False"""
    time_slot: int = 0
    """The time slot that has been reserved for this shatter process. Will be
    used as an attribute in tiledb writes to better organize and manage
    processes., defaults to 0"""
    version: str = field(default=__version__)
    """SilviMetric Version"""

    def __post_init__(self) -> None:
        from .storage import Storage

        if isinstance(self.tdb_dir, Storage):
            self.tdb_dir = self.tdb_dir.config.tdb_dir
        if isinstance(self.date, datetime):
            self.date = (self.date, self.date)
        if isinstance(self.date, list):
            self.date = tuple(d for d in self.date)
        if len(self.date) > 2 or len(self.date) < 1:
            raise ValueError(
                f'Invalid date range ({self.date}). '
                'Must be either 1 or 2 values.'
            )
        if len(self.date) == 1:
            self.date = (self.date[0], self.date[0])

        if isinstance(self.tile_size, float):
            self.tile_size = int(self.tile_size)

        if isinstance(self.read_group_size, float):
            self.read_group_size = int(self.read_group_size)

        strategies = {'leaf-v1', 'macro-v2', 'macro-v3-stage-push'}
        if self.processing_strategy not in strategies:
            raise ValueError(
                'processing_strategy must be leaf-v1, macro-v2, or '
                'macro-v3-stage-push'
            )

        if self.processing_halo_m is not None and self.processing_halo_m < 0:
            raise ValueError('processing_halo_m must be non-negative')

        if self.processing_strategy in {'macro-v2', 'macro-v3-stage-push'}:
            if self.tile_size is None or self.read_group_size is None:
                raise ValueError(
                    'macro strategies require both tile_size and '
                    'read_group_size'
                )
            if self.tile_size < 1 or self.read_group_size < 1:
                raise ValueError(
                    'tile_size and read_group_size must be positive'
                )

            tile_side = math.isqrt(self.tile_size)
            read_group_side = math.isqrt(self.read_group_size)
            if tile_side**2 != self.tile_size:
                raise ValueError(
                    'macro tile_size must be a square cell count'
                )
            if read_group_side**2 != self.read_group_size:
                raise ValueError(
                    'macro read_group_size must be a square cell count'
                )
            if read_group_side < tile_side:
                raise ValueError('read_group_size must be at least tile_size')
            if read_group_side % tile_side:
                raise ValueError(
                    'read_group_size side must be divisible by tile_size side'
                )

        if self.processing_strategy == 'macro-v3-stage-push':
            if not self.stage_tdb_dir or not self.stage_publish_uri:
                raise ValueError(
                    'macro-v3-stage-push requires stage_tdb_dir and '
                    'stage_publish_uri'
                )
            if self.stage_tdb_dir == self.stage_publish_uri:
                raise ValueError(
                    'stage_tdb_dir and stage_publish_uri must be different'
                )
            if self.tdb_dir == self.stage_publish_uri:
                raise ValueError(
                    'macro-v3-stage-push tdb_dir is a schema seed and must '
                    'differ from stage_publish_uri'
                )
            if self.stage_fragment_size_mb < 1:
                raise ValueError('stage_fragment_size_mb must be positive')
            if self.stage_shard_side_macros < 1:
                raise ValueError('stage_shard_side_macros must be positive')
            if (
                self.stage_shard_target_points is not None
                and self.stage_shard_target_points < 1
            ):
                raise ValueError('stage_shard_target_points must be positive')
            if self.stage_planner_resolution_multiple < 1:
                raise ValueError(
                    'stage_planner_resolution_multiple must be positive'
                )
            if (
                self.stage_planner_sample_point_multiplier is not None
                and self.stage_planner_sample_point_multiplier < 1
            ):
                raise ValueError(
                    'stage_planner_sample_point_multiplier must be at least 1'
                )
            if self.stage_planner_calibration_sample_count < 1:
                raise ValueError(
                    'stage_planner_calibration_sample_count must be positive'
                )
            if self.stage_planner_calibration_window_m <= 0:
                raise ValueError(
                    'stage_planner_calibration_window_m must be positive'
                )
            if self.stage_planner_density_safety_factor < 1:
                raise ValueError(
                    'stage_planner_density_safety_factor must be at least 1'
                )

    @property
    def timestamp(self):
        end_time_temp = int(datetime.now().timestamp() * 1000)
        if self.start_timestamp is None:
            return tuple((0, end_time_temp))
        if self.end_timestamp is None:
            return tuple((self.start_timestamp, end_time_temp))
        else:
            return tuple((self.start_timestamp, self.end_timestamp))

    def history_json(self):
        # removing a attrs and metrics, since they'll be in the storage log
        # removing mbr because it's too big to do json pretty printing with
        # could add a custom json logger to handle mbr logging in the future
        date = (
            self.date[0].strftime('%Y-%m-%dT%H:%M:%SZ'),
            self.date[1].strftime('%Y-%m-%dT%H:%M:%SZ'),
        )
        d = dict(
            filename=self.filename,
            name=str(self.name),
            time_slot=self.time_slot,
            bounds=self.bounds.to_json(),
            date=date,
            processing_strategy=self.processing_strategy,
            tile_size=self.tile_size,
            read_group_size=self.read_group_size,
            processing_halo_m=self.processing_halo_m,
            macro_diagnostics=self.macro_diagnostics,
            stage_tdb_dir=self.stage_tdb_dir,
            stage_publish_uri=self.stage_publish_uri,
            stage_fragment_size_mb=self.stage_fragment_size_mb,
            stage_shard_side_macros=self.stage_shard_side_macros,
            stage_shard_target_points=self.stage_shard_target_points,
            stage_planner_resolution_multiple=(
                self.stage_planner_resolution_multiple
            ),
            stage_planner_sample_point_multiplier=(
                self.stage_planner_sample_point_multiplier
            ),
            stage_planner_calibration_sample_count=(
                self.stage_planner_calibration_sample_count
            ),
            stage_planner_calibration_window_m=(
                self.stage_planner_calibration_window_m
            ),
            stage_planner_density_safety_factor=(
                self.stage_planner_density_safety_factor
            ),
            stage_worker_address=self.stage_worker_address,
            execution_timing=self.execution_timing,
        )

        return d

    def to_json(self):
        d = super().to_json()

        d['name'] = str(self.name)
        d['time_slot'] = self.time_slot
        d['bounds'] = self.bounds.to_json() if self.bounds is not None else None
        d['mbr'] = list(self.mbr)
        d['date'] = [dt.strftime('%Y-%m-%dT%H:%M:%SZ') for dt in self.date]

        return d

    @classmethod
    def from_string(cls, data: str):
        x = json.loads(data)
        return cls.from_dict(x)

    @classmethod
    def from_dict(cls, data: dict):
        x = data

        if isinstance(x['date'], list):
            date = tuple(
                (datetime.strptime(d, '%Y-%m-%dT%H:%M:%SZ') for d in x['date'])
            )
        else:
            date = datetime.strptime(x['date'], '%Y-%m-%dT%H:%M:%SZ')
        mbr = tuple(tuple(tuple(mb) for mb in m) for m in x['mbr'])
        # TODO key error if these aren't there. If we're calling from_string
        # then these keys need to exist.

        n = cls(
            tdb_dir=x['tdb_dir'],
            filename=x['filename'],
            debug=x['debug'],
            name=uuid.UUID(x['name']),
            bounds=Bounds(*x['bounds']),
            tile_size=x['tile_size'],
            processing_strategy=x.get('processing_strategy', 'leaf-v1'),
            read_group_size=x.get('read_group_size'),
            processing_halo_m=x.get('processing_halo_m'),
            macro_diagnostics=x.get('macro_diagnostics', False),
            stage_tdb_dir=x.get('stage_tdb_dir'),
            stage_publish_uri=x.get('stage_publish_uri'),
            stage_fragment_size_mb=x.get('stage_fragment_size_mb', 300),
            stage_shard_side_macros=x.get('stage_shard_side_macros', 4),
            stage_shard_target_points=x.get('stage_shard_target_points'),
            stage_planner_resolution_multiple=x.get(
                'stage_planner_resolution_multiple', 8
            ),
            stage_planner_sample_point_multiplier=x.get(
                'stage_planner_sample_point_multiplier'
            ),
            stage_planner_calibration_sample_count=x.get(
                'stage_planner_calibration_sample_count', 4
            ),
            stage_planner_calibration_window_m=x.get(
                'stage_planner_calibration_window_m', 250.0
            ),
            stage_planner_density_safety_factor=x.get(
                'stage_planner_density_safety_factor', 1.25
            ),
            stage_worker_address=x.get('stage_worker_address'),
            execution_timing=x.get('execution_timing', {}),
            start_timestamp=x['start_timestamp'],
            end_timestamp=x['end_timestamp'],
            point_count=x['point_count'],
            mbr=mbr,
            date=date,
            time_slot=x['time_slot'],
            finished=x['finished'],
        )

        return n

    def __repr__(self):
        return json.dumps(self.to_json())


@dataclass
class ExtractConfig(Config):
    """Config for the Extract process."""

    out_dir: str
    """The directory where derived rasters should be written."""
    attrs: list[Attribute] = field(default=None)
    """List of attributes to use in shatter. If this is not set it
    will be filled by the attributes in the database instance."""
    metrics: list[Metric] = field(default=None)
    """A list of metrics to use in shatter. If this is not set it
    will be filled by the metrics in the database instance."""
    bounds: Bounds = field(default=None)
    """The bounding box of the shatter process., defaults to None"""
    date: Tuple[datetime, datetime] = field(
        default_factory=lambda: tuple([datetime(1970, 1, 1), datetime.now()])
    )
    """A date range representing data collection times."""

    def __post_init__(self) -> None:
        from .storage import Storage

        if isinstance(self.tdb_dir, Storage):
            config = self.tdb_dir.config
            self.tdb_dir = config.tdb_dir
        else:
            ctx = Storage.get_tdb_context()
            if tiledb.object_type(self.tdb_dir, ctx=ctx) == 'group':
                config = Storage.shard_members(self.tdb_dir)[0].config
            else:
                config = Storage.from_db(self.tdb_dir, ctx=ctx).config

        if self.attrs is None:
            self.attrs = config.attrs
        if self.metrics is None:
            self.metrics = config.metrics
        if self.bounds is None:
            self.bounds: Bounds = config.root

        if isinstance(self.date, datetime):
            self.date = (self.date, self.date)
        if isinstance(self.date, list):
            self.date = tuple(d for d in self.date)
        if len(self.date) > 2 or len(self.date) < 1:
            raise ValueError(
                f'Invalid date range ({self.date}). '
                'Must be either 1 or 2 values.'
            )
        if len(self.date) == 1:
            self.date = (self.date[0], self.date[0])

        p = Path(self.out_dir)
        p.mkdir(parents=True, exist_ok=True)

        self.resolution: float = config.resolution
        self.crs: pyproj.CRS = config.crs

    def to_json(self):
        d = super().to_json()

        d['attrs'] = [a.to_json() for a in self.attrs]
        d['metrics'] = [m.to_json() for m in self.metrics]
        d['crs'] = json.loads(self.crs.to_json())
        d['bounds'] = self.bounds.to_json()
        d['date'] = [dt.strftime('%Y-%m-%dT%H:%M:%SZ') for dt in self.date]
        return d

    @classmethod
    def from_dict(cls, data: object):
        if 'metrics' in data:
            with mutex:
                ms = [Metric.from_dict(m) for m in data['metrics']]
        if 'attrs' in data:
            attrs = [Attribute.from_dict(a) for a in data['attrs']]
        if 'bounds' in data:
            bounds = Bounds(*data['bounds'])
        if 'log' in data:
            l = data['log']  # noqa: E741
            log = Log(
                l['log_level'], l['logdir'], l['logtype'], l['logfilename']
            )
        else:
            log = Log('INFO')
        if isinstance(data['date'], list):
            date = tuple(
                (
                    datetime.strptime(d, '%Y-%m-%dT%H:%M:%SZ')
                    for d in data['date']
                )
            )
        else:
            date = datetime.strptime(data['date'], '%Y-%m-%dT%H:%M:%SZ')

        return cls(
            tdb_dir=data['tdb_dir'],
            out_dir=data['out_dir'],
            attrs=attrs,
            metrics=ms,
            debug=data['debug'],
            bounds=bounds,
            log=log,
            date=date,
        )

    @classmethod
    def from_string(cls, data: str):
        x = json.loads(data)
        return ExtractConfig.from_dict(x)

    def __repr__(self):
        return json.dumps(self.to_json())
