import os
import uuid
import datetime
import importlib
import cloudpickle
from math import ceil
import copy
import pytest
from pathlib import Path

import numpy as np
import pandas as pd
import dask
import tiledb
from dask.distributed import Client, LocalCluster, SpecCluster, Worker
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
from silvimetric.resources.build_ledger import BuildLedger


shatter_module = importlib.import_module('silvimetric.commands.shatter')


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
    def test_batch_and_dask_paths_match_with_identical_processing_cores(
        self,
        shatter_config: ShatterConfig,
        storage: Storage,
        test_point_count: int,
        tmp_path,
    ):
        """Execution technology alone must not change a fixed-grid result."""

        def build_config(label: str) -> ShatterConfig:
            canonical_uri = (tmp_path / f'{label}.tdb').as_posix()
            storage_config = copy.deepcopy(storage.config)
            storage_config.tdb_dir = canonical_uri
            Storage.create(storage_config)
            return ShatterConfig(
                name=uuid.uuid4(),
                tdb_dir=canonical_uri,
                filename=shatter_config.filename,
                bounds=shatter_config.bounds,
                date=shatter_config.date,
                tile_size=1,
                read_group_size=4,
                processing_halo_m=shatter_config.processing_halo_m,
                processing_strategy='macro-v4-staged-publish',
                stage_shard_side_macros=1,
                stage_shard_target_points=None,
                stage_fragment_size_mb=1,
                build_stage_uri=(tmp_path / f'{label}-stages').as_posix(),
                build_ledger_uri=(tmp_path / f'{label}-ledger').as_posix(),
                build_publish_concurrency=1,
                build_publish_vfs_parallel_ops=1,
                build_stage_vfs_parallel_ops=1,
            )

        dask_config = build_config('dask-path')
        with Client(processes=False, n_workers=2, threads_per_worker=1):
            assert shatter(dask_config) == test_point_count

        batch_config = build_config('batch-path')
        plan = shatter_module.plan_macro_v4_staged_build(batch_config)
        for block_id in plan.active_block_ids:
            shatter_module.stage_macro_v4_batch_block(
                batch_config.tdb_dir,
                batch_config.build_ledger_uri,
                batch_config.name,
                block_id,
            )
            shatter_module.publish_macro_v4_batch_block(
                batch_config.tdb_dir,
                batch_config.build_ledger_uri,
                batch_config.name,
                block_id,
            )
        completed = shatter_module.complete_macro_v4_batch_build(
            batch_config.tdb_dir,
            batch_config.build_ledger_uri,
            batch_config.name,
        )
        assert completed['point_count'] == test_point_count
        shatter_module.finalize_macro_v4_staged_build(
            batch_config.tdb_dir,
            batch_config.build_ledger_uri,
            batch_config.name,
        )

        dask_plan = dask_config.execution_timing['planner']
        assert dask_plan['processing_grid_sha256'] == (
            plan.planner['processing_grid_sha256']
        )
        assert dask_plan['processing_macro_split_count'] == 0
        assert plan.planner['processing_macro_split_count'] == 0

        def raster_values(uri: str) -> pd.DataFrame:
            values = Storage.from_db(uri).open('r').df[:, :]
            return values[['X', 'Y', 'count', 'm_Z_mean']].sort_values(
                ['X', 'Y']
            ).reset_index(drop=True)

        pd.testing.assert_frame_equal(
            raster_values(dask_config.tdb_dir),
            raster_values(batch_config.tdb_dir),
            check_exact=True,
        )

    def test_macro_v4_batch_operations_are_dask_independent(
        self,
        shatter_config: ShatterConfig,
        storage: Storage,
        test_point_count: int,
        tmp_path,
    ):
        """A durable plan can be staged and published one block at a time.

        This is the execution contract used by the Batch worker image.  The
        test intentionally creates no Dask client: every worker reconstructs
        its block and configuration solely from the canonical array and the
        append-only ledger.
        """
        canonical_uri = (tmp_path / 'macro-v4-batch.tdb').as_posix()
        staged_storage_config = copy.deepcopy(storage.config)
        staged_storage_config.tdb_dir = canonical_uri
        Storage.create(staged_storage_config)
        build_id = uuid.uuid4()
        config = ShatterConfig(
            name=build_id,
            tdb_dir=canonical_uri,
            filename=shatter_config.filename,
            bounds=shatter_config.bounds,
            date=shatter_config.date,
            tile_size=1,
            read_group_size=4,
            processing_halo_m=shatter_config.processing_halo_m,
            processing_strategy='macro-v4-staged-publish',
            stage_shard_side_macros=1,
            stage_fragment_size_mb=1,
            build_stage_uri=(tmp_path / 'batch-stages').as_posix(),
            build_ledger_uri=(tmp_path / 'batch-ledger').as_posix(),
            build_publish_concurrency=1,
            build_publish_vfs_parallel_ops=1,
            build_stage_vfs_parallel_ops=1,
        )

        plan = shatter_module.plan_macro_v4_staged_build(config)
        assert plan.resumed is False
        assert plan.active_block_ids
        resumed = shatter_module.plan_macro_v4_staged_build(
            ShatterConfig.from_string(str(config))
        )
        assert resumed.resumed is True
        assert resumed.active_block_ids == plan.active_block_ids

        for block_id in plan.active_block_ids:
            shatter_module.stage_macro_v4_batch_block(
                canonical_uri, config.build_ledger_uri, build_id, block_id
            )
        for block_id in plan.active_block_ids:
            shatter_module.publish_macro_v4_batch_block(
                canonical_uri, config.build_ledger_uri, build_id, block_id
            )

        completion = shatter_module.complete_macro_v4_batch_build(
            canonical_uri, config.build_ledger_uri, build_id
        )
        assert completion['point_count'] == test_point_count
        final_config = shatter_module.finalize_macro_v4_staged_build(
            canonical_uri, config.build_ledger_uri, build_id
        )
        assert final_config.finished
        point_count = Storage.from_db(canonical_uri).open('r').df[:, :][
            'count'
        ].sum()
        assert int(point_count) == test_point_count

    def test_macro_v4_publish_commit_index_recovers_before_receipt(
        self,
        shatter_config: ShatterConfig,
        storage: Storage,
        tmp_path,
        monkeypatch,
    ):
        """A verified commit avoids a second canonical range scan on resume."""
        canonical_uri = (tmp_path / 'macro-v4-commit-recovery.tdb').as_posix()
        staged_storage_config = copy.deepcopy(storage.config)
        staged_storage_config.tdb_dir = canonical_uri
        Storage.create(staged_storage_config)
        build_id = uuid.uuid4()
        config = ShatterConfig(
            name=build_id,
            tdb_dir=canonical_uri,
            filename=shatter_config.filename,
            bounds=shatter_config.bounds,
            date=shatter_config.date,
            tile_size=1,
            read_group_size=4,
            processing_halo_m=shatter_config.processing_halo_m,
            processing_strategy='macro-v4-staged-publish',
            stage_shard_side_macros=1,
            stage_fragment_size_mb=1,
            build_stage_uri=(tmp_path / 'commit-stages').as_posix(),
            build_ledger_uri=(tmp_path / 'commit-ledger').as_posix(),
            build_publish_concurrency=1,
            build_publish_vfs_parallel_ops=1,
            build_stage_vfs_parallel_ops=1,
        )
        plan = shatter_module.plan_macro_v4_staged_build(config)
        block_id = plan.active_block_ids[0]
        shatter_module.stage_macro_v4_batch_block(
            canonical_uri, config.build_ledger_uri, build_id, block_id
        )

        original_append = BuildLedger.append

        def crash_before_published_receipt(self, block, state, *args, **kwargs):
            if block == block_id and state == 'published':
                raise KeyboardInterrupt('after verified commit index')
            return original_append(self, block, state, *args, **kwargs)

        monkeypatch.setattr(BuildLedger, 'append', crash_before_published_receipt)
        with pytest.raises(KeyboardInterrupt, match='verified commit index'):
            shatter_module.publish_macro_v4_batch_block(
                canonical_uri, config.build_ledger_uri, build_id, block_id
            )
        monkeypatch.setattr(BuildLedger, 'append', original_append)

        recovered = shatter_module.publish_macro_v4_batch_block(
            canonical_uri, config.build_ledger_uri, build_id, block_id
        )
        assert recovered.publish_commit_index_recovered
        assert BuildLedger(config.build_ledger_uri).state(block_id).state == 'published'

    def test_macro_v4_schema_identity_excludes_storage_uri(
        self, storage: Storage, tmp_path
    ):
        """A stage is schema-compatible even though it has a different URI."""
        canonical = Storage.from_db(storage.config.tdb_dir)
        stage_config = copy.deepcopy(storage.config)
        stage_config.tdb_dir = (tmp_path / 'macro-v4-schema-stage').as_posix()
        stage = Storage.create(stage_config)

        assert shatter_module._block_schema_hash(canonical) == (
            shatter_module._block_schema_hash(stage)
        )

    def test_macro_v4_schema_identity_survives_dask_serialization(
        self, storage: Storage
    ):
        """A worker must retain the canonical persisted contract verbatim.

        Metric/filter callables are dill-encoded in StorageConfig.  Dask's
        cloudpickle round trip may legitimately regenerate those encodings;
        the separate ``_serialized_config`` contract must therefore survive
        and keep the stage receipt compatible with the canonical publisher.
        """
        canonical = Storage.from_db(storage.config.tdb_dir)
        worker_storage = cloudpickle.loads(cloudpickle.dumps(canonical))

        assert worker_storage._serialized_config == canonical._serialized_config
        assert shatter_module._block_schema_hash(worker_storage) == (
            shatter_module._block_schema_hash(canonical)
        )

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
        assert planner['query_timing']['native_sample_calls'] > 0
        assert planner['query_timing']['coarse_query_calls'] > 0
        assert planner['planner_wall_seconds'] >= 0
        assert len(planner['input_processing_grid_sha256']) == 64

    def test_adaptive_planner_subdivides_an_over_limit_singleton_macro(
        self,
        shatter_config: ShatterConfig,
        storage: Storage,
    ):
        """A point cap remains a cap after a block reaches one macro."""

        class UniformDenseData:
            def estimate_count(self, bounds, reader_resolution=None):
                area = (bounds.maxx - bounds.minx) * (
                    bounds.maxy - bounds.miny
                )
                return int(area)

            def count(self, bounds):
                return self.estimate_count(bounds)

        shatter_config.tile_size = 1
        shatter_config.read_group_size = 4
        shatter_config.processing_strategy = 'macro-v3-stage-push'
        shatter_config.stage_tdb_dir = '/private/tmp/singleton-stage.tdb'
        shatter_config.stage_publish_uri = '/private/tmp/singleton-output.tdb'
        shatter_config.stage_shard_target_points = 1_200
        shatter_config.stage_planner_resolution_multiple = 2
        shatter_config.build_max_split_depth = 8
        shatter_config.build_min_cells_per_side = 1
        root = Bounds(0, 0, 160, 160)
        singleton = [
            Extents(
                root,
                storage.config.resolution,
                storage.config.alignment,
                root,
            )
        ]

        blocks, planner = plan_macro_blocks(
            singleton, shatter_config, storage, UniformDenseData()
        )

        assert len(blocks) > 1
        assert planner['processing_macro_split_count'] > 0
        assert planner['processing_grid_sha256'] != (
            planner['input_processing_grid_sha256']
        )
        assert all(
            window['estimated_max_points'] <= 1_200
            for window in planner['windows']
        )

    def test_stage_failure_classifier_recognizes_dask_killed_worker(self):
        class KilledWorker(Exception):
            pass

        assert shatter_module._stage_failure_kind(KilledWorker()) == (
            'worker_lost_after_retries'
        )

    def test_adaptive_planner_splits_the_historic_becker_memory_shape(
        self,
        shatter_config: ShatterConfig,
        storage: Storage,
    ):
        """The former 1.28 km Becker singleton must not reach a worker whole."""

        class BeckerDensityData:
            density = 14.52064

            def estimate_count(self, bounds, reader_resolution=None):
                area = (bounds.maxx - bounds.minx) * (
                    bounds.maxy - bounds.miny
                )
                return int(area / 160**2)

            def count(self, bounds):
                area = (bounds.maxx - bounds.minx) * (
                    bounds.maxy - bounds.miny
                )
                return int(area * self.density)

        shatter_config.tile_size = 4
        shatter_config.read_group_size = 4624
        shatter_config.processing_strategy = 'macro-v4-staged-publish'
        shatter_config.build_stage_uri = '/private/tmp/becker-stages'
        shatter_config.build_ledger_uri = '/private/tmp/becker-ledger'
        shatter_config.stage_shard_target_points = 15_000_000
        shatter_config.stage_planner_resolution_multiple = 8
        shatter_config.build_max_split_depth = 8
        shatter_config.build_min_cells_per_side = 2
        historic_bounds = Bounds(
            -10657890.0,
            5931810.0,
            -10656610.0,
            5933030.0,
        )
        singleton = [
            Extents(
                historic_bounds,
                20.0,
                storage.config.alignment,
                historic_bounds,
            )
        ]

        blocks, planner = plan_macro_blocks(
            singleton, shatter_config, storage, BeckerDensityData()
        )

        assert len(blocks) > 1
        assert all(
            window['estimated_max_points'] <= 15_000_000
            for window in planner['windows']
        )

    def test_adaptive_planner_uses_local_density_before_worker_recovery(
        self,
        shatter_config: ShatterConfig,
        storage: Storage,
    ):
        """A dense central flightline must not inherit sparse AOI samples.

        The whole-AOI calibration samples a 2-by-2 grid.  This fixture puts a
        dense area between those four locations, where the old planner would
        submit one oversized task and learn only after a killed worker.  The
        per-block sample sees that density before Dask work is created.
        """

        class LocalizedDensityData:
            def estimate_count(self, bounds, reader_resolution=None):
                area = (bounds.maxx - bounds.minx) * (
                    bounds.maxy - bounds.miny
                )
                return max(1, int(area / 160**2))

            def count(self, bounds):
                center_x = (bounds.minx + bounds.maxx) / 2
                center_y = (bounds.miny + bounds.maxy) / 2
                density = 100 if 600 <= center_x <= 1000 and 600 <= center_y <= 1000 else 1
                area = (bounds.maxx - bounds.minx) * (
                    bounds.maxy - bounds.miny
                )
                return int(area * density)

        shatter_config.tile_size = 1
        shatter_config.read_group_size = 4
        shatter_config.processing_strategy = 'macro-v4-staged-publish'
        shatter_config.build_stage_uri = '/private/tmp/local-density-stages'
        shatter_config.build_ledger_uri = '/private/tmp/local-density-ledger'
        shatter_config.stage_shard_target_points = 25_000_000
        shatter_config.stage_planner_resolution_multiple = 8
        shatter_config.stage_planner_calibration_sample_count = 4
        shatter_config.stage_planner_local_calibration_sample_count = 1
        shatter_config.build_max_split_depth = 8
        shatter_config.build_min_cells_per_side = 1
        root = Bounds(0, 0, 1600, 1600)
        singleton = [
            Extents(
                root,
                storage.config.resolution,
                storage.config.alignment,
                root,
            )
        ]

        blocks, planner = plan_macro_blocks(
            singleton, shatter_config, storage, LocalizedDensityData()
        )

        assert len(blocks) > 1
        assert planner['local_calibration_sample_count'] == 1
        assert any(
            sample['raw_density_points_per_m2']
            > planner['density_ceiling_points_per_m2']
            for sample in planner['local_calibration_map']
        )
        assert any(
            window['inherited_local_density_observations'] > 0
            for window in planner['windows']
        )
        assert all(
            sample['area_m2'] <= 50.0**2
            for sample in planner['local_calibration_map']
        )
        assert all(
            window['estimated_max_points'] <= 25_000_000
            for window in planner['windows']
        )

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
        monkeypatch: pytest.MonkeyPatch,
    ):
        """Stage-and-push uses independent, restart-safe local-stage tasks."""
        stage_dir = (tmp_path / 'stage.tdb').as_posix()
        publish_dir = (tmp_path / 'published.tdb').as_posix()
        shatter_config.tile_size = 1
        shatter_config.processing_strategy = 'macro-v3-stage-push'
        shatter_config.read_group_size = 4
        shatter_config.stage_tdb_dir = stage_dir
        shatter_config.stage_publish_uri = publish_dir
        shatter_config.stage_fragment_size_mb = 1
        shatter_config.macro_diagnostics = True
        stage_paths = []
        create_stage = Storage.create_stage

        def record_stage_path(source, stage_path):
            stage_paths.append(stage_path)
            return create_stage(source, stage_path)

        def no_address_pinned_partial_tasks(*args, **kwargs):
            pytest.fail(
                'macro-v3 must not depend on address-pinned partial/final '
                'tasks: a Dask worker restart makes them unrunnable'
            )

        monkeypatch.setattr(
            'silvimetric.commands.shatter.do_macro_to_partial_stage',
            no_address_pinned_partial_tasks,
        )
        monkeypatch.setattr(
            'silvimetric.commands.shatter.finalize_macro_block',
            no_address_pinned_partial_tasks,
        )
        monkeypatch.setattr(
            Storage, 'create_stage', staticmethod(record_stage_path)
        )

        shatter(shatter_config)

        assert shatter_config.tdb_dir == publish_dir
        published = Storage.shard_members(publish_dir)
        # Neighboring macro reads are merged locally before each compact,
        # S3-compatible shard is committed.
        assert published
        # A killed worker can leave a partial local array behind.  Every
        # rescheduled block attempt must therefore get a distinct local URI.
        assert stage_paths
        assert len(stage_paths) == len(set(stage_paths))
        assert all('/attempts/' in path for path in stage_paths)
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

    def test_macro_v4_writes_one_array_and_matches_macro_v2(
        self,
        shatter_config: ShatterConfig,
        storage: Storage,
        test_point_count: int,
        tmp_path,
        threaded_dask,
    ):
        """Macro-v4 stores every disjoint macro as one array's fragments."""
        v2_dir = (tmp_path / 'macro-v2.tdb').as_posix()
        v4_dir = (tmp_path / 'macro-v4.tdb').as_posix()
        v2_config = copy.deepcopy(storage.config)
        v2_config.tdb_dir = v2_dir
        Storage.create(v2_config)
        v4_storage_config = copy.deepcopy(storage.config)
        v4_storage_config.tdb_dir = v4_dir
        Storage.create(v4_storage_config)

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
        v4 = ShatterConfig(
            tdb_dir=v4_dir,
            processing_strategy='macro-v4-single-array',
            stage_fragment_size_mb=1,
            **macro_common,
        )

        assert shatter(v2) == shatter(v4) == test_point_count
        v4_storage = Storage.from_db(v4_dir)
        assert tiledb.object_type(v4_dir) == 'array'
        assert tiledb.array_fragments(v4_dir)
        confirm_one_entry(
            v4_storage,
            v4_storage.config.root.maxy,
            11 if v4_storage.config.alignment == 'AlignToCenter' else 10,
            test_point_count,
        )

        timing = v4.execution_timing
        assert timing['strategy'] == 'macro-v4-single-array'
        assert timing['point_count'] == test_point_count
        assert timing['array']['fragment_count_before'] > 1
        assert timing['array']['fragment_count_after'] >= 1
        assert timing['planner']['macro_count'] == timing['macro_count']

        v2_output = tmp_path / 'macro-v2-extract'
        v4_output = tmp_path / 'macro-v4-extract'
        for database, output in ((v2_dir, v2_output), (v4_dir, v4_output)):
            extract(
                ExtractConfig(
                    tdb_dir=database,
                    out_dir=str(output),
                    bounds=shatter_config.bounds,
                    date=shatter_config.date,
                )
            )

        v2_rasters = {path.name: path for path in Path(v2_output).glob('*.tif')}
        v4_rasters = {path.name: path for path in Path(v4_output).glob('*.tif')}
        assert v2_rasters.keys() == v4_rasters.keys()
        for name in v2_rasters:
            left = gdal.Open(str(v2_rasters[name]))
            right = gdal.Open(str(v4_rasters[name]))
            np.testing.assert_equal(
                left.GetRasterBand(1).ReadAsArray(),
                right.GetRasterBand(1).ReadAsArray(),
            )

    def test_macro_v4_accepts_parallel_disjoint_writers(
        self,
        shatter_config: ShatterConfig,
        storage: Storage,
        test_point_count: int,
        tmp_path,
    ):
        """Two Dask workers may commit disjoint macro ranges to one array."""
        v4_dir = (tmp_path / 'macro-v4-parallel.tdb').as_posix()
        v4_storage_config = copy.deepcopy(storage.config)
        v4_storage_config.tdb_dir = v4_dir
        Storage.create(v4_storage_config)
        v4 = ShatterConfig(
            tdb_dir=v4_dir,
            filename=shatter_config.filename,
            bounds=shatter_config.bounds,
            date=shatter_config.date,
            tile_size=1,
            read_group_size=4,
            processing_strategy='macro-v4-single-array',
            stage_fragment_size_mb=1,
        )
        with LocalCluster(
            n_workers=2,
            threads_per_worker=1,
            processes=False,
            dashboard_address=None,
        ) as cluster:
            with Client(cluster):
                assert shatter(v4) == test_point_count

        v4_storage = Storage.from_db(v4_dir)
        assert int(v4_storage.open('r').df[:, :]['count'].sum()) == test_point_count
        executor = v4.execution_timing['planner']['distributed_executor']
        assert executor['mode'] == 'disjoint-blocks-write-one-array'
        assert executor['block_task_count'] >= 2
        assert executor['retries'] == 0

    def test_macro_v4_staged_publish_resumes_from_immutable_ledger(
        self,
        shatter_config: ShatterConfig,
        storage: Storage,
        test_point_count: int,
        tmp_path,
        threaded_dask,
    ):
        """A sealed build can be restarted without recomputing its macros."""
        staged_dir = (tmp_path / 'macro-v4-staged.tdb').as_posix()
        stage_uri = (tmp_path / 'durable-stage').as_posix()
        ledger_uri = (tmp_path / 'durable-ledger').as_posix()
        staged_storage_config = copy.deepcopy(storage.config)
        staged_storage_config.tdb_dir = staged_dir
        Storage.create(staged_storage_config)

        common = dict(
            tdb_dir=staged_dir,
            filename=shatter_config.filename,
            bounds=shatter_config.bounds,
            date=shatter_config.date,
            tile_size=1,
            read_group_size=4,
            processing_halo_m=shatter_config.processing_halo_m,
            processing_strategy='macro-v4-staged-publish',
            stage_shard_side_macros=1,
            stage_fragment_size_mb=1,
            build_stage_uri=stage_uri,
            build_ledger_uri=ledger_uri,
            build_publish_concurrency=2,
            build_publish_vfs_parallel_ops=1,
        )
        first = ShatterConfig(name=uuid.uuid4(), **common)
        assert shatter(first) == test_point_count

        ledger = BuildLedger(ledger_uri)
        status = first.execution_timing['build_ledger']
        assert status['states']['published'] == status['planned_block_count']
        assert not status['partial']
        sealed = ledger.state('__build__')
        assert sealed is not None
        assert sealed.state == 'build_sealed'
        assert sealed.details['point_count'] == test_point_count

        # Each normal publish retains phase telemetry for fleet tuning and a
        # verified commit index for the crash window before its normal ledger
        # receipt becomes visible.
        block_id = next(
            path.name
            for path in (tmp_path / 'durable-ledger' / 'blocks').iterdir()
            if path.name != '__build__'
        )
        published = ledger.state(block_id)
        assert published is not None
        assert published.state == 'published'
        assert published.details['publish_stage_read_seconds'] >= 0
        assert published.details['publish_precheck_seconds'] >= 0
        assert published.details['publish_postcheck_seconds'] >= 0
        assert published.details['publish_logical_tiledb_reads'] >= 2
        assert ledger.publish_commit(block_id) is not None

        # Same build name and ledger is the explicit resume contract.  The
        # run derives point totals from published receipts rather than writing
        # duplicate fragments for an already sealed build.
        resumed = ShatterConfig(name=first.name, **common)
        assert shatter(resumed) == test_point_count
        resume_status = resumed.execution_timing['build_ledger']
        assert resume_status['published_block_count'] == (
            resume_status['planned_block_count']
        )
        assert resumed.execution_timing['planner']['distributed_executor'][
            'stage_task_count'
        ] == 0
        assert resumed.time_slot == first.time_slot
        # Resuming a sealed build is read-only from the canonical array's
        # history perspective: it must reuse the original slot rather than
        # leave an empty reservation behind.
        assert Storage.from_db(staged_dir).config.next_time_slot == (
            first.time_slot + 1
        )
        assert resumed.execution_timing['array']['consolidation_skipped']
        point_count = Storage.from_db(staged_dir).open('r').df[:, :][
            'count'
        ].sum()
        assert int(point_count) == test_point_count

    def test_macro_v4_staged_publish_defers_sealing_to_scheduler_only_step(
        self,
        shatter_config: ShatterConfig,
        storage: Storage,
        test_point_count: int,
        tmp_path,
    ):
        """Workers may disappear after publishing without blocking sealing."""
        staged_dir = (tmp_path / 'macro-v4-deferred.tdb').as_posix()
        stage_uri = (tmp_path / 'deferred-stage').as_posix()
        ledger_uri = (tmp_path / 'deferred-ledger').as_posix()
        staged_storage_config = copy.deepcopy(storage.config)
        staged_storage_config.tdb_dir = staged_dir
        Storage.create(staged_storage_config)
        config = ShatterConfig(
            name=uuid.uuid4(),
            tdb_dir=staged_dir,
            filename=shatter_config.filename,
            bounds=shatter_config.bounds,
            date=shatter_config.date,
            tile_size=1,
            read_group_size=4,
            processing_halo_m=shatter_config.processing_halo_m,
            processing_strategy='macro-v4-staged-publish',
            stage_shard_side_macros=1,
            stage_fragment_size_mb=1,
            build_stage_uri=stage_uri,
            build_ledger_uri=ledger_uri,
            build_publish_concurrency=2,
            build_publish_vfs_parallel_ops=1,
            build_stage_vfs_parallel_ops=1,
            defer_build_finalization=True,
        )
        with LocalCluster(
            n_workers=2,
            threads_per_worker=1,
            processes=False,
            dashboard_address=None,
            resources={'silvimetric_stage': 1},
        ) as cluster:
            with Client(cluster):
                assert shatter(config) == test_point_count

        ledger = BuildLedger(ledger_uri)
        assert ledger.state('__build__').state == 'build_published'
        assert config.execution_timing['array']['finalization_deferred']
        stored = Storage.from_db(staged_dir).get_shatter_meta(config.time_slot)
        assert not stored.finished

        # No LocalCluster or Dask client exists here. This proves that the
        # expensive canonical maintenance phase depends only on the ledger and
        # canonical array, not on workers, source EPT access, or PDAL.
        finalized = shatter_module.finalize_macro_v4_staged_build(
            staged_dir, ledger_uri, config.name
        )
        assert finalized.finished
        assert finalized.point_count == test_point_count
        assert ledger.state('__build__').state == 'build_sealed'
        assert finalized.execution_timing['array']['fragment_count_before'] >= 1
        point_count = Storage.from_db(staged_dir).open('r').df[:, :][
            'count'
        ].sum()
        assert int(point_count) == test_point_count

    def test_macro_v4_staged_publish_recovers_after_publish_failure(
        self,
        shatter_config: ShatterConfig,
        storage: Storage,
        test_point_count: int,
        tmp_path,
    ):
        """A failed canonical publish leaves reusable stages and receipts."""
        staged_dir = (tmp_path / 'macro-v4-partial.tdb').as_posix()
        stage_uri = (tmp_path / 'partial-stage').as_posix()
        ledger_uri = (tmp_path / 'partial-ledger').as_posix()
        staged_storage_config = copy.deepcopy(storage.config)
        staged_storage_config.tdb_dir = staged_dir
        Storage.create(staged_storage_config)
        common = dict(
            tdb_dir=staged_dir,
            filename=shatter_config.filename,
            bounds=shatter_config.bounds,
            date=shatter_config.date,
            tile_size=1,
            read_group_size=4,
            processing_halo_m=shatter_config.processing_halo_m,
            processing_strategy='macro-v4-staged-publish',
            stage_shard_side_macros=1,
            stage_fragment_size_mb=1,
            build_stage_uri=stage_uri,
            build_ledger_uri=ledger_uri,
            build_publish_concurrency=1,
            build_publish_vfs_parallel_ops=1,
            build_stage_vfs_parallel_ops=1,
        )
        build_id = uuid.uuid4()
        original = shatter_module.publish_staged_macro_block
        calls = 0

        def fail_one_publish(*args, **kwargs):
            nonlocal calls
            calls += 1
            if calls == 1:
                raise KeyboardInterrupt('intentional publish interruption')
            return original(*args, **kwargs)

        monkeypatch = pytest.MonkeyPatch()
        monkeypatch.setattr(
            shatter_module, 'publish_staged_macro_block', fail_one_publish
        )
        try:
            with pytest.raises(
                KeyboardInterrupt, match='intentional publish interruption'
            ):
                shatter(ShatterConfig(name=build_id, **common))
        finally:
            monkeypatch.undo()

        ledger = BuildLedger(ledger_uri)
        # The build manifest records a partial state while the blocks have
        # durable staged or published receipts. No successful work is lost.
        assert ledger.state('__build__').state == 'build_partial'
        assert calls >= 1

        resumed = ShatterConfig(name=build_id, **common)
        assert shatter(resumed) == test_point_count
        summary = resumed.execution_timing['build_ledger']
        assert summary['published_block_count'] == summary['planned_block_count']
        assert not summary['partial']
        point_count = Storage.from_db(staged_dir).open('r').df[:, :][
            'count'
        ].sum()
        assert int(point_count) == test_point_count

    def test_macro_v4_staged_publish_recovers_after_consolidation_interrupt(
        self,
        shatter_config: ShatterConfig,
        storage: Storage,
        test_point_count: int,
        tmp_path,
    ):
        """Final consolidation can be safely re-entered after interruption."""
        staged_dir = (tmp_path / 'macro-v4-consolidate.tdb').as_posix()
        stage_uri = (tmp_path / 'consolidate-stage').as_posix()
        ledger_uri = (tmp_path / 'consolidate-ledger').as_posix()
        staged_storage_config = copy.deepcopy(storage.config)
        staged_storage_config.tdb_dir = staged_dir
        Storage.create(staged_storage_config)
        common = dict(
            tdb_dir=staged_dir,
            filename=shatter_config.filename,
            bounds=shatter_config.bounds,
            date=shatter_config.date,
            tile_size=1,
            read_group_size=4,
            processing_halo_m=shatter_config.processing_halo_m,
            processing_strategy='macro-v4-staged-publish',
            stage_shard_side_macros=1,
            stage_fragment_size_mb=1,
            build_stage_uri=stage_uri,
            build_ledger_uri=ledger_uri,
            build_publish_concurrency=1,
            build_publish_vfs_parallel_ops=1,
            build_stage_vfs_parallel_ops=1,
        )
        build_id = uuid.uuid4()
        original = Storage.consolidate_canonical_array
        calls = 0

        def interrupt_once(self, fragment_size_mb):
            nonlocal calls
            calls += 1
            if calls == 1:
                raise RuntimeError('intentional consolidation interruption')
            return original(self, fragment_size_mb)

        monkeypatch = pytest.MonkeyPatch()
        monkeypatch.setattr(
            Storage, 'consolidate_canonical_array', interrupt_once
        )
        try:
            with pytest.raises(
                RuntimeError, match='intentional consolidation interruption'
            ):
                shatter(ShatterConfig(name=build_id, **common))
        finally:
            monkeypatch.undo()

        ledger = BuildLedger(ledger_uri)
        assert ledger.state('__build__').state == 'build_partial'

        resumed = ShatterConfig(name=build_id, **common)
        assert shatter(resumed) == test_point_count
        assert resumed.execution_timing['planner']['distributed_executor'][
            'stage_task_count'
        ] == 0
        assert ledger.state('__build__').state == 'build_sealed'

    def test_macro_v4_staged_publish_uses_dask_stages_and_bounded_publishers(
        self,
        shatter_config: ShatterConfig,
        storage: Storage,
        test_point_count: int,
        tmp_path,
    ):
        """Stages execute in parallel while canonical writers stay bounded."""
        staged_dir = (tmp_path / 'macro-v4-distributed.tdb').as_posix()
        staged_storage_config = copy.deepcopy(storage.config)
        staged_storage_config.tdb_dir = staged_dir
        Storage.create(staged_storage_config)
        config = ShatterConfig(
            name=uuid.uuid4(),
            tdb_dir=staged_dir,
            filename=shatter_config.filename,
            bounds=shatter_config.bounds,
            date=shatter_config.date,
            tile_size=1,
            read_group_size=4,
            processing_halo_m=shatter_config.processing_halo_m,
            processing_strategy='macro-v4-staged-publish',
            stage_shard_side_macros=1,
            stage_fragment_size_mb=1,
            build_stage_uri=(tmp_path / 'distributed-stage').as_posix(),
            build_ledger_uri=(tmp_path / 'distributed-ledger').as_posix(),
            build_publish_concurrency=2,
            build_publish_vfs_parallel_ops=1,
            build_stage_vfs_parallel_ops=1,
        )
        # Model the production split fleet: stage processes are allowed to
        # retain large PDAL point views, while canonical publishers have no
        # stage token at all.  This prevents a future capacity check against
        # the total number of workers from silently falling back to
        # unrestricted stage scheduling.
        worker_specs = {
            **{
                f'stage-{index}': {
                    'cls': Worker,
                    'options': {
                        'nthreads': 1,
                        'resources': {'silvimetric_stage': 1},
                    },
                }
                for index in range(2)
            },
            **{
                f'publisher-{index}': {
                    'cls': Worker,
                    'options': {
                        'nthreads': 1,
                        'resources': {'silvimetric_publisher': 1},
                    },
                }
                for index in range(2)
            },
        }
        with SpecCluster(
            workers=worker_specs,
            asynchronous=False,
            silence_logs=False,
        ) as cluster:
            with Client(cluster):
                assert shatter(config) == test_point_count

        executor = config.execution_timing['planner']['distributed_executor']
        assert executor['mode'] == 'durable-stages-bounded-canonical-publishers'
        assert executor['stage_task_count'] >= 2
        assert executor['publisher_concurrency'] == 2
        assert executor['stage_inflight_limit'] == 2
        assert executor['stage_resource'] == 'silvimetric_stage'
        assert executor['stage_resource_capacity'] == 2
        assert executor['publisher_resource'] == 'silvimetric_publisher'
        assert executor['publisher_resource_capacity'] == 2
        assert executor['max_pending_publish_blocks'] >= 0
        assert executor['publish_retries'] == 0
        ledger = BuildLedger(config.build_ledger_uri)
        assert any(
            record.state == 'build_stage_tasks_complete'
            for record in ledger.records('__build__')
        )
        point_count = Storage.from_db(staged_dir).open('r').df[:, :][
            'count'
        ].sum()
        assert int(point_count) == test_point_count

    def test_macro_v4_adaptively_splits_a_memory_limited_stage(
        self,
        shatter_config: ShatterConfig,
        storage: Storage,
        test_point_count: int,
        tmp_path,
    ):
        """A known memory failure is replaced by disjoint durable children."""
        staged_dir = (tmp_path / 'macro-v4-adaptive-memory.tdb').as_posix()
        stage_uri = (tmp_path / 'adaptive-memory-stage').as_posix()
        ledger_uri = (tmp_path / 'adaptive-memory-ledger').as_posix()
        staged_storage_config = copy.deepcopy(storage.config)
        staged_storage_config.tdb_dir = staged_dir
        Storage.create(staged_storage_config)
        config = ShatterConfig(
            name=uuid.uuid4(),
            tdb_dir=staged_dir,
            filename=shatter_config.filename,
            bounds=shatter_config.bounds,
            date=shatter_config.date,
            tile_size=1,
            read_group_size=4,
            processing_halo_m=shatter_config.processing_halo_m,
            processing_strategy='macro-v4-staged-publish',
            stage_shard_side_macros=1,
            stage_fragment_size_mb=1,
            build_stage_uri=stage_uri,
            build_ledger_uri=ledger_uri,
            build_publish_concurrency=1,
            build_stage_retries=0,
            build_max_split_depth=1,
            build_min_cells_per_side=1,
            build_publish_vfs_parallel_ops=1,
            build_stage_vfs_parallel_ops=1,
        )
        original = shatter_module.stage_macro_block
        failure_marker = tmp_path / 'one-memory-failure'

        def fail_one_parent(macros, *args, **kwargs):
            block_id = shatter_module._shard_name(macros)
            try:
                with failure_marker.open('x', encoding='utf8') as marker:
                    marker.write(block_id)
                raise MemoryError('known over-memory-limit macro')
            except FileExistsError:
                pass
            return original(macros, *args, **kwargs)

        monkeypatch = pytest.MonkeyPatch()
        monkeypatch.setattr(shatter_module, 'stage_macro_block', fail_one_parent)
        try:
            with LocalCluster(
                n_workers=2,
                threads_per_worker=1,
                processes=False,
                dashboard_address=None,
            ) as cluster:
                with Client(cluster):
                    assert shatter(config) == test_point_count
        finally:
            monkeypatch.undo()

        ledger = BuildLedger(ledger_uri)
        parent_id = failure_marker.read_text(encoding='utf8')
        split_records = [
            record
            for record in ledger.records(parent_id)
            if record.state == 'split'
        ]
        assert len(split_records) == 1
        children = split_records[0].details['children']
        parent_records = ledger.records(parent_id)
        assert len(
            [record for record in parent_records if record.state == 'failed']
        ) == 1
        assert not any(
            record.state in {'staged', 'published'}
            for record in parent_records
        )
        assert len(children) == 2
        assert all(
            ledger.state(child['block_id']).state == 'published'
            for child in children
        )
        executor = config.execution_timing['planner']['distributed_executor']
        assert executor['adaptive_split_count'] == 1
        assert executor['adaptive_splits'] == [
            {
                'parent_id': parent_id,
                'failure_kind': 'memory_pressure',
                'split_depth': 0,
                'child_ids': [child['block_id'] for child in children],
            }
        ]
        assert config.execution_timing['build_ledger']['root_block_count'] < (
            config.execution_timing['build_ledger']['planned_block_count']
        )
        assert config.execution_timing['build_ledger']['adaptive_split_count'] == 1
        point_count = Storage.from_db(staged_dir).open('r').df[:, :][
            'count'
        ].sum()
        assert int(point_count) == test_point_count

        # Subdivision changes only read scheduling.  A fresh extract must
        # remain pixel-identical to the established non-staged macro path.
        baseline_dir = (tmp_path / 'macro-v2-memory-baseline.tdb').as_posix()
        baseline_storage_config = copy.deepcopy(storage.config)
        baseline_storage_config.tdb_dir = baseline_dir
        Storage.create(baseline_storage_config)
        baseline = ShatterConfig(
            tdb_dir=baseline_dir,
            filename=shatter_config.filename,
            bounds=shatter_config.bounds,
            date=shatter_config.date,
            tile_size=1,
            read_group_size=4,
            processing_halo_m=shatter_config.processing_halo_m,
            processing_strategy='macro-v2',
        )
        assert shatter(baseline) == test_point_count
        baseline_output = tmp_path / 'macro-v2-memory-baseline'
        adaptive_output = tmp_path / 'macro-v4-memory-adaptive'
        for database, output in (
            (baseline_dir, baseline_output),
            (staged_dir, adaptive_output),
        ):
            extract(
                ExtractConfig(
                    tdb_dir=database,
                    out_dir=str(output),
                    bounds=shatter_config.bounds,
                    date=shatter_config.date,
                )
            )
        baseline_rasters = {
            path.name: path for path in Path(baseline_output).glob('*.tif')
        }
        adaptive_rasters = {
            path.name: path for path in Path(adaptive_output).glob('*.tif')
        }
        assert baseline_rasters.keys() == adaptive_rasters.keys()
        for name in baseline_rasters:
            left = gdal.Open(str(baseline_rasters[name]))
            right = gdal.Open(str(adaptive_rasters[name]))
            np.testing.assert_equal(
                left.GetRasterBand(1).ReadAsArray(),
                right.GetRasterBand(1).ReadAsArray()
            )

    def test_macro_v4_stage_failure_remains_resumable(
        self,
        shatter_config: ShatterConfig,
        storage: Storage,
        test_point_count: int,
        tmp_path,
    ):
        """An arbitrary worker/task failure leaves a clean resumable ledger."""
        staged_dir = (tmp_path / 'macro-v4-stage-recovery.tdb').as_posix()
        stage_uri = (tmp_path / 'stage-recovery-stage').as_posix()
        ledger_uri = (tmp_path / 'stage-recovery-ledger').as_posix()
        staged_storage_config = copy.deepcopy(storage.config)
        staged_storage_config.tdb_dir = staged_dir
        Storage.create(staged_storage_config)
        common = dict(
            tdb_dir=staged_dir,
            filename=shatter_config.filename,
            bounds=shatter_config.bounds,
            date=shatter_config.date,
            tile_size=1,
            read_group_size=4,
            processing_halo_m=shatter_config.processing_halo_m,
            processing_strategy='macro-v4-staged-publish',
            stage_shard_side_macros=1,
            stage_fragment_size_mb=1,
            build_stage_uri=stage_uri,
            build_ledger_uri=ledger_uri,
            build_publish_concurrency=1,
            build_stage_retries=0,
            build_publish_vfs_parallel_ops=1,
            build_stage_vfs_parallel_ops=1,
        )
        build_id = uuid.uuid4()
        original = shatter_module.stage_macro_block
        failure_marker = tmp_path / 'one-worker-failure'

        def fail_one_stage(macros, *args, **kwargs):
            try:
                with failure_marker.open('x', encoding='utf8') as marker:
                    marker.write(shatter_module._shard_name(macros))
                raise RuntimeError('simulated worker loss outside task control')
            except FileExistsError:
                pass
            return original(macros, *args, **kwargs)

        monkeypatch = pytest.MonkeyPatch()
        monkeypatch.setattr(shatter_module, 'stage_macro_block', fail_one_stage)
        try:
            with LocalCluster(
                n_workers=2,
                threads_per_worker=1,
                processes=False,
                dashboard_address=None,
            ) as cluster:
                with Client(cluster):
                    with pytest.raises(RuntimeError, match='simulated worker loss'):
                        shatter(ShatterConfig(name=build_id, **common))
        finally:
            monkeypatch.undo()

        ledger = BuildLedger(ledger_uri)
        assert ledger.state('__build__').state == 'build_partial'
        failed_id = failure_marker.read_text(encoding='utf8')
        assert ledger.state(failed_id).state == 'failed'

        with LocalCluster(
            n_workers=2,
            threads_per_worker=1,
            processes=False,
            dashboard_address=None,
        ) as cluster:
            with Client(cluster):
                resumed = ShatterConfig(name=build_id, **common)
                assert shatter(resumed) == test_point_count

        assert resumed.execution_timing['planner']['distributed_executor'][
            'stage_task_count'
        ] == 1

        # The published output is enough to prove recovery; do not run a
        # third time merely to inspect timing.
        point_count = Storage.from_db(staged_dir).open('r').df[:, :][
            'count'
        ].sum()
        assert int(point_count) == test_point_count
        assert ledger.state('__build__').state == 'build_sealed'

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
