import os
import uuid
import datetime
from math import ceil
import copy
import pytest
from pathlib import Path

import numpy as np
import pandas as pd
import dask
from osgeo import gdal

from silvimetric import (
    Bounds,
    Extents,
    ExtractConfig,
    Log,
    Storage,
    extract,
    info,
    shatter,
)
from silvimetric import ShatterConfig
from silvimetric.commands.shatter import plan_macro_blocks


@dask.delayed
def write(x, y, val, s: Storage, attrs, dims, metrics):
    m_list = [m.entry_name(a.name) for m in metrics for a in attrs]
    data = {
        a.name: np.array([np.array([val], dims[a.name]), None], object)[:-1]
        for a in attrs
    }

    for m in m_list:
        data[m] = [val]

    data['count'] = [val]
    data['shatter_process_num'] = 1
    with s.open('w') as w:
        w[x, y] = data


def confirm_one_entry(storage, maxy, base, pointcount):
    xysize = base
    shape = xysize**2
    pc = pointcount

    with storage.open('r') as a:
        vals = a.df[:, :].set_index(['X', 'Y'])
        assert vals.Z.shape[0] == shape
        xdom = int(a.schema.domain.dim('X').domain[1])
        ydom = int(a.schema.domain.dim('Y').domain[1])
        xtile = int(a.schema.domain.dim('X').tile)
        ytile = int(a.schema.domain.dim('Y').tile)
        assert xdom == ((xysize + 1 + xtile - 1) // xtile) * xtile - 1
        assert ydom == ((xysize + 1 + ytile - 1) // ytile) * ytile - 1
        assert vals['count'].sum() == pc
        val_const = ceil(maxy / storage.config.resolution)

        # The physical TileDB domain includes padded final blocks for GDAL,
        # while shatter writes only the logical Silvimetric extent.
        for xi in range(xysize):
            for yi in range(xysize):
                z = vals.loc[xi, yi].Z
                zmean = vals.loc[xi, yi].m_Z_mean
                if isinstance(z, np.ndarray):
                    assert np.all(z == (val_const - yi - 1))
                elif isinstance(z, pd.Series):
                    for z1 in z.values:
                        np.all(z1 == (val_const - yi - 1))
                assert z.mean() == zmean


class Test_Shatter(object):
    def test_adaptive_macro_v3_planner_uses_bounded_coarse_estimates(
        self,
        shatter_config: ShatterConfig,
        storage: Storage,
    ):
        """The planner must split dense windows using coarse PDAL estimates."""

        class CoarseData:
            def __init__(self):
                self.calls = []

            def estimate_count(self, bounds, reader_resolution=None):
                self.calls.append((bounds, reader_resolution))
                area = (bounds.maxx - bounds.minx) * (
                    bounds.maxy - bounds.miny
                )
                return int(area / 100)

            def count(self, bounds):
                area = (bounds.maxx - bounds.minx) * (
                    bounds.maxy - bounds.miny
                )
                return int(area / 100)

        shatter_config.tile_size = 1
        shatter_config.read_group_size = 4
        shatter_config.processing_strategy = 'macro-v3-stage-push'
        shatter_config.stage_tdb_dir = '/private/tmp/stage.tdb'
        shatter_config.stage_publish_uri = '/private/tmp/published.tdb'
        shatter_config.stage_shard_target_points = 40
        shatter_config.stage_planner_resolution_multiple = 2
        shatter_config.processing_halo_m = 5
        root = Extents(
            storage.config.root,
            storage.config.resolution,
            storage.config.alignment,
            storage.config.root,
        )
        macros = root.get_leaf_children(shatter_config.read_group_size)
        data = CoarseData()

        blocks, planner = plan_macro_blocks(
            macros, shatter_config, storage, data
        )

        assert len(blocks) > 1
        assert planner['method'] == 'pdal-summary-coarse-resolution-upper-bound'
        assert planner['count_source'] == 'bounded-reader-execution'
        assert planner['processing_halo_m'] == 5
        assert all(
            resolution == storage.config.resolution * 2
            for _, resolution in data.calls
        )
        assert all(
            window['estimated_max_points'] <= 40
            or window['macro_count'] == 1
            for window in planner['windows']
        )
        assert planner['point_multiplier_source'] == 'bounded-native-calibration'
        assert len(planner['calibration_samples']) == 4

    def test_adaptive_macro_v3_planner_does_not_treat_cells_as_raw_points(
        self,
        shatter_config: ShatterConfig,
        storage: Storage,
    ):
        """A dense RainyLake-like AOI must become several schedulable blocks."""

        class DenseData:
            density = 16.0

            def estimate_count(self, bounds, reader_resolution=None):
                # A coarse reader returns one representative point per 160 m
                # cell regardless of the raw return density beneath it.
                area = (bounds.maxx - bounds.minx) * (
                    bounds.maxy - bounds.miny
                )
                return int(area / 160**2)

            def count(self, bounds):
                area = (bounds.maxx - bounds.minx) * (
                    bounds.maxy - bounds.miny
                )
                return int(area * self.density)

        shatter_config.processing_strategy = 'macro-v3-stage-push'
        shatter_config.stage_tdb_dir = '/private/tmp/stage.tdb'
        shatter_config.stage_publish_uri = '/private/tmp/published.tdb'
        shatter_config.stage_shard_target_points = 50_000_000
        shatter_config.stage_planner_resolution_multiple = 8
        shatter_config.processing_halo_m = 20
        shatter_config.tile_size = 25
        shatter_config.read_group_size = 2025
        root = Bounds(0, 0, 5_400, 3_600)
        macros = [
            Extents(
                Bounds(x, y, x + 900, y + 900),
                20,
                storage.config.alignment,
                root,
            )
            for y in range(0, 3_600, 900)
            for x in range(0, 5_400, 900)
        ]

        blocks, planner = plan_macro_blocks(
            macros, shatter_config, storage, DenseData()
        )

        assert len(macros) == 24
        assert len(blocks) >= 4
        assert all(
            window['estimated_max_points'] <= 50_000_000
            or window['macro_count'] == 1
            for window in planner['windows']
        )
        assert planner['density_ceiling_points_per_m2'] >= 16.0

    def test_command(
        self,
        shatter_config: ShatterConfig,
        storage: Storage,
        test_point_count: int,
        # threaded_dask,
    ):
        shatter(shatter_config)
        base = 11 if storage.config.alignment == 'AlignToCenter' else 10
        maxy = storage.config.root.maxy
        confirm_one_entry(storage, maxy, base, test_point_count)

    def test_macro_v2_writes_on_the_worker_path(
        self,
        shatter_config: ShatterConfig,
        storage: Storage,
        test_point_count: int,
        monkeypatch: pytest.MonkeyPatch,
        threaded_dask,
    ):
        """macro-v2 must dispatch to worker-side writes, not leaf-v1."""
        shatter_config.tile_size = 1
        shatter_config.processing_strategy = 'macro-v2'
        shatter_config.read_group_size = 4

        def no_leaf_v1(*args, **kwargs):
            pytest.fail('macro-v2 must not invoke the leaf-v1 executor')

        monkeypatch.setattr(
            'silvimetric.commands.shatter.run', no_leaf_v1
        )

        shatter(shatter_config)
        base = 11 if storage.config.alignment == 'AlignToCenter' else 10
        confirm_one_entry(
            storage,
            storage.config.root.maxy,
            base,
            test_point_count,
        )
        timing = shatter_config.execution_timing
        assert timing['strategy'] == 'macro-v2'
        assert timing['point_count'] == test_point_count
        assert timing['nonempty_task_count'] > 0
        assert timing['worker_seconds']['read_seconds'] > 0
        assert timing['maintenance_seconds'] >= 0
        assert timing['run_macro_consolidation_seconds'] >= 0
        assert timing['shatter_total_seconds'] > 0

    def test_macro_v3_stages_consolidates_and_publishes(
        self,
        shatter_config: ShatterConfig,
        storage: Storage,
        test_point_count: int,
        tmp_path,
        threaded_dask,
    ):
        """Stage-and-push must not write fragments into the source array."""
        stage_dir = (tmp_path / 'stage.tdb').as_posix()
        publish_dir = (tmp_path / 'published.tdb').as_posix()
        shatter_config.tile_size = 1
        shatter_config.processing_strategy = 'macro-v3-stage-push'
        shatter_config.read_group_size = 4
        shatter_config.stage_tdb_dir = stage_dir
        shatter_config.stage_publish_uri = publish_dir
        shatter_config.stage_fragment_size_mb = 1
        shatter_config.macro_diagnostics = True

        shatter(shatter_config)

        assert shatter_config.tdb_dir == publish_dir
        published = Storage.shard_members(publish_dir)
        # Neighboring macro reads are merged locally before each compact,
        # S3-compatible shard is committed.
        assert published
        point_count = sum(
            int(member.open('r').df[:, :]['count'].sum())
            for member in published
        )
        assert point_count == test_point_count
        # A copied directory can contain fragment files without TileDB commit
        # records.  Require every published shard to expose real fragments.
        assert all(
            member.get_fragments((0, 2**63 - 1)) for member in published
        )
        # The source was a schema seed only; all data was written via stage.
        assert not storage.get_fragments((0, 2**63 - 1))

        timing = shatter_config.execution_timing
        assert timing['strategy'] == 'macro-v3-stage-push'
        assert timing['point_count'] == test_point_count
        assert timing['nonempty_task_count'] > 0
        assert timing['macro_count'] > timing['task_count']
        assert timing['stage']['published_shard_count'] == len(published)
        diagnostics = timing['macro_diagnostics']
        assert len(diagnostics) == timing['macro_count']
        assert all('bounds' in diagnostic for diagnostic in diagnostics)
        assert all(
            diagnostic['rss_peak_bytes'] >= diagnostic['rss_start_bytes']
            for diagnostic in diagnostics
        )

    def test_macro_v3_extract_matches_macro_v2(
        self,
        shatter_config: ShatterConfig,
        storage: Storage,
        tmp_path,
        threaded_dask,
        monkeypatch: pytest.MonkeyPatch,
    ):
        """Distributed shard reads must preserve the macro-v2 extract result."""
        v2_dir = (tmp_path / 'macro-v2.tdb').as_posix()
        seed_dir = (tmp_path / 'macro-v3-seed.tdb').as_posix()
        stage_dir = (tmp_path / 'macro-v3-stage.tdb').as_posix()
        v3_dir = (tmp_path / 'macro-v3-published.tdb').as_posix()
        v2_config = copy.deepcopy(storage.config)
        v2_config.tdb_dir = v2_dir
        Storage.create(v2_config)
        v3_seed_config = copy.deepcopy(storage.config)
        v3_seed_config.tdb_dir = seed_dir
        Storage.create(v3_seed_config)

        macro_common = dict(
            filename=shatter_config.filename,
            bounds=shatter_config.bounds,
            date=shatter_config.date,
            tile_size=1,
            read_group_size=4,
            processing_halo_m=shatter_config.processing_halo_m,
        )
        v2 = ShatterConfig(
            tdb_dir=v2_dir,
            processing_strategy='macro-v2',
            **macro_common,
        )
        v3 = ShatterConfig(
            tdb_dir=seed_dir,
            processing_strategy='macro-v3-stage-push',
            stage_tdb_dir=stage_dir,
            stage_publish_uri=v3_dir,
            stage_fragment_size_mb=1,
            **macro_common,
        )
        assert shatter(v2) == shatter(v3)

        class InlineClient:
            def __init__(self):
                self.submissions = []

            def submit(self, func, *args):
                self.submissions.append((func, args))
                return func(*args)

            def gather(self, futures):
                return futures

        inline_client = InlineClient()
        monkeypatch.setattr(
            'silvimetric.commands.extract.get_client', lambda: inline_client
        )
        v2_output = tmp_path / 'macro-v2-extract'
        v3_output = tmp_path / 'macro-v3-extract'
        for database, output in ((v2_dir, v2_output), (v3_dir, v3_output)):
            extract(
                ExtractConfig(
                    tdb_dir=database,
                    out_dir=str(output),
                    bounds=shatter_config.bounds,
                    date=shatter_config.date,
                )
            )

        v2_rasters = {path.name: path for path in Path(v2_output).glob('*.tif')}
        v3_rasters = {path.name: path for path in Path(v3_output).glob('*.tif')}
        assert v2_rasters.keys() == v3_rasters.keys()
        for name in v2_rasters:
            left = gdal.Open(str(v2_rasters[name]))
            right = gdal.Open(str(v3_rasters[name]))
            assert left.GetGeoTransform() == right.GetGeoTransform()
            assert left.GetProjection() == right.GetProjection()
            np.testing.assert_equal(
                left.GetRasterBand(1).ReadAsArray(),
                right.GetRasterBand(1).ReadAsArray(),
            )
        assert len(inline_client.submissions) == len(Storage.shard_members(v3_dir))

    def test_multiple(
        self,
        shatter_config: ShatterConfig,
        storage: Storage,
        test_point_count: int,
    ):
        shatter(shatter_config)
        base = 11 if storage.config.alignment == 'AlignToCenter' else 10
        maxy = storage.config.root.maxy
        confirm_one_entry(storage, maxy, base, test_point_count)

        shatter_config2 = copy.deepcopy(shatter_config)
        d2 = (datetime.datetime(2009, 1, 1), datetime.datetime(2010, 1, 1))

        # change attributes to make it a new run
        shatter_config2.name = uuid.uuid4()
        shatter_config2.mbr = ()
        shatter_config2.time_slot = storage.reserve_time_slot()
        shatter_config2.date = d2
        shatter_config2.start_timestamp = None
        shatter_config2.end_timestamp = None
        shatter(shatter_config2)

        # no longer allowing duplicates, so removing option for double items
        confirm_one_entry(storage, maxy, base, test_point_count)

        # check that you can query results by datetime
        with storage.open('r', timestamp=shatter_config2.timestamp) as a:
            assert np.all(a.df[:, :].shatter_process_num == 2)
            assert len(a.df[:, :]) == base**2

        with storage.open('r', timestamp=shatter_config.timestamp) as a2:
            assert np.all(a2.df[:, :].shatter_process_num == 1)
            assert len(a2.df[:, :]) == base**2

        with storage.open(
            'r',
            timestamp=(
                shatter_config.timestamp[0],
                shatter_config2.timestamp[1],
            ),
        ) as a3:
            vals = a3.df[:, :]
            vals = vals[vals.shatter_process_num != 0]

            proc1 = vals.shatter_process_num == 1
            assert not proc1.any()

            proc2 = vals.shatter_process_num == 2
            assert proc2.all()

        m = info(storage)
        assert len(m['history']) == 2

    def test_config(
        self,
        shatter_config: ShatterConfig,
        storage: Storage,
        test_point_count: int,
    ):
        shatter(shatter_config)
        try:
            meta = storage.get_shatter_meta(shatter_config.time_slot)
            pc = meta.point_count
            assert pc == test_point_count
        except BaseException as e:
            pytest.fail("Failed to retrieve 'shatter' metadata key." + e.args)

    @pytest.mark.parametrize(
        'sh_cfg', ['shatter_config', 'uneven_shatter_config']
    )
    def test_sub_bounds(
        self,
        sh_cfg: str,
        test_point_count: int,
        request: pytest.FixtureRequest,
        alignment: str,
        threaded_dask,
    ):
        s = request.getfixturevalue(sh_cfg)
        storage = Storage.from_db(s.tdb_dir)
        e = Extents.from_storage(s.tdb_dir)

        pc = 0
        for b in e.split():
            log = Log(20)
            time_slot = storage.reserve_time_slot()
            sc = ShatterConfig(
                tdb_dir=s.tdb_dir,
                log=log,
                filename=s.filename,
                tile_size=s.tile_size,
                bounds=b.bounds,
                date=s.date,
                time_slot=time_slot,
            )
            pc = pc + shatter(sc)
        history = info(s.tdb_dir)['history']
        assert len(history) == 4
        assert isinstance(history, list)
        pcs = [h['point_count'] for h in history]

        # When alignment is point and using uneven_shatter_config, the bounds
        # will be changed so that not all points are grabbed. This is expected.
        if alignment != 'AlignToCenter' or sh_cfg != 'uneven_shatter_config':
            assert sum(pcs) == test_point_count
            assert pc == test_point_count

        with storage.open('r') as a:
            data = a.query(attrs=['Z'], coords=True, use_arrow=False).df[:]
            data = data.set_index(['X', 'Y'])

            minx = int(data.reset_index().X.min())
            maxx = int(data.reset_index().X.max())
            miny = int(data.reset_index().Y.min())
            maxy = int(data.reset_index().Y.max())

            for xi in range(minx, maxx + 1):
                for yi in range(miny, maxy + 1):
                    curr = data.loc[xi, yi]
                    # check that each cell only has one allocation
                    assert curr.size == 1.0

    def test_partial_overlap(
        self, partial_shatter_config: ShatterConfig, alignment: int
    ):
        pc = shatter(partial_shatter_config)
        actual = 22500 if alignment == 'AlignToCorner' else 32400
        assert pc == actual

    @pytest.mark.skipif(
        not os.environ.get('AWS_SECRET_ACCESS_KEY')
        or not os.environ.get('AWS_ACCESS_KEY_ID'),
        reason=(
            'S3 integration test requires non-empty AWS_ACCESS_KEY_ID and '
            'AWS_SECRET_ACCESS_KEY environment variables'
        ),
    )
    def test_remote_creation(
        self,
        s3_shatter_config: ShatterConfig,
        s3_storage: Storage,
    ):
        # need processes scheduler to accurately test bug fix
        dask.config.set(scheduler='processes')
        maxy = s3_storage.config.root.maxy
        base = 11
        point_count = 108900
        shatter(s3_shatter_config)
        confirm_one_entry(s3_storage, maxy, base, point_count)
