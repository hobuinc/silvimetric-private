import numpy as np
import signal
import json
import hashlib
import math
import os
import resource
import sys
import uuid
from collections import deque
from threading import Event, Thread
from datetime import datetime
import copy
from dataclasses import dataclass, field
from time import perf_counter

from typing_extensions import Generator
import pandas as pd
import tiledb

from distributed.client import _get_global_client as get_client

from dask.delayed import delayed
from dask import compute
from dask.distributed import as_completed

from .. import Bounds, Extents, Storage, Data, ShatterConfig, Metric
from ..resources.build_ledger import BuildLedger
from ..resources.taskgraph import Graph


def _rss_bytes() -> int | None:
    """Return this process's resident set size without a psutil dependency.

    The production workers run on Linux, where ``/proc`` provides the current
    RSS.  ``ru_maxrss`` is retained as a portable high-water fallback and is
    particularly useful if a PDAL extension holds the GIL too long for the
    sampler thread to run at its exact peak.
    """
    try:
        with open('/proc/self/status', encoding='utf8') as status:
            for line in status:
                if line.startswith('VmRSS:'):
                    return int(line.split()[1]) * 1024
    except OSError:
        pass

    value = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
    # macOS reports bytes; Linux and the other intended Unix workers report
    # KiB.  This branch also keeps the diagnostic useful in local tests.
    return int(value if sys.platform == 'darwin' else value * 1024)


class MacroMemoryMonitor:
    """Sample worker RSS while a macro task executes.

    RSS belongs to the Dask worker process rather than a Python task.  The
    resulting record is therefore labelled as a *worker-process* peak.  The
    bounded profiling run intentionally schedules one macro at a time per
    worker, which makes this value a safe per-macro concurrency input.
    """

    def __init__(self, interval_seconds: float = 0.1):
        self.interval_seconds = interval_seconds
        self.pid = os.getpid()
        self.rss_start_bytes = _rss_bytes()
        self.ru_maxrss_start_bytes = _rss_bytes()
        self.rss_peak_bytes = self.rss_start_bytes or 0
        self._stop = Event()
        self._thread = Thread(target=self._sample, daemon=True)
        self._thread.start()

    def _sample(self) -> None:
        while not self._stop.wait(self.interval_seconds):
            rss = _rss_bytes()
            if rss is not None:
                self.rss_peak_bytes = max(self.rss_peak_bytes, rss)

    def stop(self) -> dict:
        self._stop.set()
        self._thread.join(timeout=self.interval_seconds * 2)
        rss_end = _rss_bytes()
        if rss_end is not None:
            self.rss_peak_bytes = max(self.rss_peak_bytes, rss_end)
        # _rss_bytes falls back to ru_maxrss off Linux, but on Linux capture
        # it as well: native PDAL work may temporarily prevent sampling.
        ru_maxrss = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
        ru_maxrss_bytes = int(
            ru_maxrss if sys.platform == 'darwin' else ru_maxrss * 1024
        )
        self.rss_peak_bytes = max(self.rss_peak_bytes, ru_maxrss_bytes)
        return {
            'worker_pid': self.pid,
            'rss_start_bytes': self.rss_start_bytes,
            'rss_end_bytes': rss_end,
            'rss_peak_bytes': self.rss_peak_bytes,
            'ru_maxrss_start_bytes': self.ru_maxrss_start_bytes,
            'ru_maxrss_bytes': ru_maxrss_bytes,
        }


def final(
    config: ShatterConfig,
    storage: Storage,
    finished: bool = False,
):
    """
    Final method for shatter, add last config attributes and save it to metadata

    :param config: :class:`silvimetric.resources.config.ShatterConfig`.
    :param storage: :class:`silvimetric.resources.storage.Storage`.
    :param finished: Shatter finish flag, defaults to False
    """
    # modify config to reflect result of shattter process
    config.log.debug('Saving shatter metadata.')
    config.end_timestamp = int(datetime.now().timestamp() * 1000)
    config.mbr = storage.mbrs(config=config)
    config.finished = finished
    storage.save_shatter_meta(config)


def _record_staged_build_interruption(
    config: ShatterConfig, storage: Storage, error: BaseException
) -> None:
    """Leave a durable partial-build marker before propagating an interrupt."""
    if (
        config.processing_strategy != 'macro-v4-staged-publish'
        or not config.build_ledger_uri
    ):
        return
    try:
        ledger = BuildLedger(
            config.build_ledger_uri, Storage.get_tdb_context(storage)
        )
        latest = ledger.state('__build__')
        if latest is None or latest.state != 'build_partial':
            ledger.append(
                '__build__',
                'build_partial',
                build_id=str(config.name),
                reason=type(error).__name__,
                message=str(error),
            )
    except Exception:
        # Preserve the original exception (especially KeyboardInterrupt) if
        # an object-store outage prevents writing a final observation. The
        # durable per-block receipts remain sufficient for a later resume.
        config.log.exception('Unable to append macro-v4 partial-build receipt')


def get_data(
    extents: Extents,
    filename: str,
    storage: Storage,
    reader_collar: float | None = None,
    water_mask_uri: str | None = None,
) -> pd.DataFrame:
    """
    Execute pipeline and retrieve point cloud data for this extent

    :param extents: :class:`silvimetric.resources.extents.Extents` being used.
    :param filename: Path to either PDAL pipeline or point cloud.
    :param storage: :class:`silvimetric.resources.storage.Storage` database.
    :return: Point data array from PDAL.
    """
    water_mask = None
    if water_mask_uri:
        from ..resources.water_mask import WaterMask

        if not storage.config.usgs_albers:
            raise ValueError('water mask requires USGS Albers storage')
        water_mask = WaterMask(
            water_mask_uri,
            root_x=storage.config.root.minx,
            root_y=storage.config.root.maxy,
            resolution=storage.config.resolution,
        )
        if water_mask.core_is_fully_water(
            extents.x1, extents.y1, extents.x2, extents.y2
        ):
            # Do not instantiate Data: even discovering the source CRS or
            # constructing its PDAL reader can access the remote EPT. A mixed
            # core must still read its complete collar for SMRF/HAG context.
            print(
                'SILVIMETRIC_WATER_SKIP '
                + json.dumps({
                    'bounds': extents.bounds.get(),
                    'core_cells': (extents.x2 - extents.x1)
                    * (extents.y2 - extents.y1),
                }),
                flush=True,
            )
            return pd.DataFrame()

    attrs = [*[a.name for a in storage.get_attributes()], 'xi', 'yi']
    data = Data(
        filename,
        storage.config,
        bounds=extents.bounds,
        reader_collar=reader_collar,
    )
    p = data.pipeline
    data.execute(allowed_dims=[*attrs, 'X', 'Y'])
    if data.pdal_timing and p.log:
        print(
            'SILVIMETRIC_PDAL_TIMING '
            + json.dumps(
                {
                    'bounds': extents.bounds.get(),
                    'log': p.log,
                }
            ),
            flush=True,
        )

    try:
        points = p.get_dataframe(0)
    except IndexError:
        return pd.DataFrame()

    points = points.loc[points.Y < extents.bounds.maxy]
    points = points.loc[points.Y >= extents.bounds.miny]
    points = points.loc[points.X >= extents.bounds.minx]
    points = points.loc[points.X < extents.bounds.maxx][attrs]

    points.loc[:, 'xi'] = np.floor(points.xi).astype(np.int32)
    # The USGS Albers root is the upper *outer edge* of pixel row zero, so
    # both pixel coordinates use floor.  The older centre-aligned layout
    # retains its historical ceil conversion for backwards compatibility.
    y_indices = (
        np.floor(points.yi)
        if storage.config.usgs_albers
        else np.ceil(points.yi)
    )
    points.loc[:, 'yi'] = y_indices.astype(np.int32)

    if water_mask is not None and not points.empty:
        points = water_mask.omit_pixels(points)

    return points


def run_graph(data_in: pd.DataFrame, metrics: list[Metric]) -> pd.DataFrame:
    """
    Run DataFrames through metric processes

    :param data_in: Input DataFrame of point data.
    :param metrics: List of Metrics to run.
    :return: DataFrame of derived data.
    """
    graph = Graph(metrics)

    return graph.run(data_in)


def agg_list(data_in: pd.DataFrame, proc_num: int) -> pd.DataFrame:
    """
    Make variable-length point data attributes into lists

    :param data_in: Input DataFrame of point data.
    :param proc_num: Shatter process increment.
    :return: DataFrame of aggregated point data.
    """
    # groupby won't set index on an empty array
    if data_in.empty:
        return data_in.set_index(['xi', 'yi'])

    old_dtypes = data_in.dtypes
    xyi_dtypes = {'xi': np.int32, 'yi': np.int32}
    o = np.dtype('O')
    first_col_name = data_in.columns[0]
    cols = [a for a in data_in.columns if a not in ['xi', 'yi']]
    col_dtypes = {a: o for a in cols}

    coerced = data_in.astype(col_dtypes | xyi_dtypes)
    gb = coerced.groupby(['xi', 'yi'], sort=True)
    counts_df = gb[first_col_name].agg('count').rename('count')
    listed = (
        gb.agg(lambda x: np.array(x, old_dtypes[x.name]))
        .join(counts_df)
        .assign(shatter_process_num=proc_num)
    )
    return listed


def join(list_data: pd.DataFrame, metric_data: pd.DataFrame) -> pd.DataFrame:
    """
    Join the list data and metric DataFrames together.

    :param list_data: DataFrame from agg_list.
    :param metric_data: DataFrame from run_graph.
    :return: Joined DataFrame of Metrics and Attributes.
    """
    return list_data.join(metric_data).reset_index()


def write(
    data_in: pd.DataFrame,
    storage: Storage,
    dates: tuple[datetime, datetime],
) -> int:
    """
    Write cell data to database

    :param data_in: Data to be written to database.
    :param storage: :class:`silvimetric.resources.storage.Storage`.
    :param dates: Tuple of start and end datetimes for dataset.
    :return: Number of points written.
    """
    if data_in.empty:
        return 0

    storage.write(data_in, dates)

    pc = data_in['count'].sum().item()
    p = copy.deepcopy(pc)

    return p


def do_one(
    leaf: Extents, config: ShatterConfig, storage: Storage
) -> pd.DataFrame:
    """
    Create dask bags and the order of operations.

    :param leaf: Extents to operate on.
    :param config: :class:`silvimetric.resources.config.ShatterConfig`.
    :param storage: :class:`silvimetric.resources.storage.Storage`.
    :return: Number of points written.
    """

    # remove any extents that have already been done, only skip if full overlap
    if config.mbr:
        if not all(leaf.disjoint_by_mbr(m) for m in config.mbr):
            return None
    points = get_data(
        leaf, config.filename, storage, water_mask_uri=config.water_mask_uri
    )
    if points.empty:
        return None
    listed_data = agg_list(points, config.time_slot)
    metric_data = run_graph(points, storage.get_metrics())
    joined_data = join(listed_data, metric_data)

    del points, listed_data, metric_data

    return joined_data


def do_macro(
    macro: Extents, config: ShatterConfig, storage: Storage
) -> 'MacroTaskResult':
    """Process one macro read group and write its aggregate on the worker.

    ``macro-v2`` deliberately returns only a scalar.  The potentially large
    point and aggregate DataFrames are retained by the worker until the
    corresponding contiguous TileDB write has completed.
    """
    if config.mbr:
        if not all(macro.disjoint_by_mbr(m) for m in config.mbr):
            return MacroTaskResult.empty()

    task_started = perf_counter()
    memory_monitor = MacroMemoryMonitor() if config.macro_diagnostics else None
    phase_started = task_started
    points = get_data(
        macro,
        config.filename,
        storage,
        reader_collar=config.processing_halo_m,
        water_mask_uri=config.water_mask_uri,
    )
    read_seconds = perf_counter() - phase_started
    if points.empty:
        result = MacroTaskResult(
            point_count=0,
            cell_count=0,
            read_seconds=read_seconds,
            aggregate_seconds=0.0,
            metric_seconds=0.0,
            join_seconds=0.0,
            write_seconds=0.0,
            total_seconds=perf_counter() - task_started,
        )
        return _attach_macro_diagnostic(result, macro, memory_monitor)

    phase_started = perf_counter()
    listed_data = agg_list(points, config.time_slot)
    aggregate_seconds = perf_counter() - phase_started

    phase_started = perf_counter()
    metric_data = run_graph(points, storage.get_metrics())
    metric_seconds = perf_counter() - phase_started

    phase_started = perf_counter()
    joined_data = join(listed_data, metric_data)
    join_seconds = perf_counter() - phase_started
    cell_count = len(joined_data)
    del points, listed_data, metric_data

    phase_started = perf_counter()
    point_count = write(joined_data, storage, config.date)
    write_seconds = perf_counter() - phase_started
    del joined_data
    result = MacroTaskResult(
        point_count=point_count,
        cell_count=cell_count,
        read_seconds=read_seconds,
        aggregate_seconds=aggregate_seconds,
        metric_seconds=metric_seconds,
        join_seconds=join_seconds,
        write_seconds=write_seconds,
        total_seconds=perf_counter() - task_started,
    )
    return _attach_macro_diagnostic(result, macro, memory_monitor)


def _attach_macro_diagnostic(
    result: 'MacroTaskResult', macro: Extents, memory_monitor: MacroMemoryMonitor | None
) -> 'MacroTaskResult':
    """Attach an opt-in, small diagnostic record to one macro result."""
    if memory_monitor is None:
        return result
    result.macro_diagnostics.append(
        {
            'bounds': macro.bounds.get(),
            'point_count': result.point_count,
            'cell_count': result.cell_count,
            'read_seconds': round(result.read_seconds, 6),
            'aggregate_seconds': round(result.aggregate_seconds, 6),
            'metric_seconds': round(result.metric_seconds, 6),
            'join_seconds': round(result.join_seconds, 6),
            'write_seconds': round(result.write_seconds, 6),
            'total_seconds': round(result.total_seconds, 6),
            **memory_monitor.stop(),
        }
    )
    return result


def _resolve_actor_result(result):
    """Return a local value or synchronously resolve a Dask ActorFuture."""
    resolver = getattr(result, 'result', None)
    return resolver() if callable(resolver) else result


class MacroStageWriter:
    """Single-owner local TileDB writer used by ``macro-v3-stage-push``.

    When constructed as a Dask actor, macro workers transfer their final
    aggregates directly to this actor.  The actor buffers spatially adjacent
    macros and writes each compact batch once; workers therefore do not queue
    behind a TileDB write per macro.
    """

    def __init__(self, stage_config, shatter_config: ShatterConfig):
        self.storage = Storage.create(stage_config)
        self.shatter_config = copy.deepcopy(shatter_config)
        self.shatter_config.tdb_dir = stage_config.tdb_dir
        self.storage.save_shatter_meta(self.shatter_config)
        self.storage.set_stage_state('writing')
        self._pending: dict[tuple[int, int], list[pd.DataFrame]] = {}
        self._buffered_bytes = 0
        self._max_buffered_bytes = 0
        self._buffered_task_count = 0

    def write(self, data_in: pd.DataFrame, dates) -> int:
        """Accept one macro result without performing a synchronous write."""
        macro_side = math.isqrt(self.shatter_config.read_group_size)
        # Four-by-four macro bins are spatially compact enough for dense
        # writes, while eliminating the previous one-fragment-per-macro path.
        stage_side = macro_side * 4
        key = (
            int(data_in['xi'].min()) // stage_side,
            int(data_in['yi'].min()) // stage_side,
        )
        self._pending.setdefault(key, []).append(data_in)
        self._buffered_task_count += 1
        self._buffered_bytes += int(data_in.memory_usage(deep=True).sum())
        self._max_buffered_bytes = max(
            self._max_buffered_bytes, self._buffered_bytes
        )
        return int(data_in['count'].sum())

    def _flush_pending(self) -> dict:
        """Write spatial bins in deterministic order immediately before plan."""
        started = perf_counter()
        batch_count = 0
        for key in sorted(self._pending):
            frames = self._pending[key]
            data_in = pd.concat(frames).sort_values(by=['xi', 'yi'])
            write(data_in, self.storage, self.shatter_config.date)
            batch_count += 1
        self._pending.clear()
        self._buffered_bytes = 0
        return {
            'stage_buffered_task_count': self._buffered_task_count,
            'stage_spatial_batch_count': batch_count,
            'stage_max_buffered_bytes': self._max_buffered_bytes,
            'stage_buffer_flush_seconds': round(perf_counter() - started, 6),
        }

    def finalize(self, publish_uri: str, fragment_size_mb: int) -> dict:
        """Plan, consolidate, validate, and publish from the stage owner."""
        started = perf_counter()
        flush_timing = self._flush_pending()
        fragments_before = self.storage.stage_fragment_summary()

        planning_started = perf_counter()
        plan = self.storage.stage_consolidation_plan(fragment_size_mb)
        planning_seconds = perf_counter() - planning_started
        self.storage.set_stage_state(
            'planned',
            desired_fragment_size_mb=fragment_size_mb,
            fragment_count_before=len(fragments_before),
            plan=plan,
        )

        consolidation_started = perf_counter()
        consolidated_nodes = self.storage.consolidate_stage_plan(
            fragment_size_mb
        )
        consolidation_seconds = perf_counter() - consolidation_started
        self.storage.vacuum()
        fragments_after = self.storage.stage_fragment_summary()
        self.storage.set_stage_state(
            'locally_validated',
            fragment_count_after=len(fragments_after),
            fragments_after=fragments_after,
        )

        publish_started = perf_counter()
        self.storage.set_stage_state(
            'publishing', publish_uri=publish_uri
        )
        published = self.storage.publish_stage(publish_uri)
        publish_seconds = perf_counter() - publish_started
        self.storage.set_stage_state('published', **published)

        return {
            'stage_fragment_size_mb': fragment_size_mb,
            'stage_fragment_count_before': len(fragments_before),
            'stage_fragment_bytes_before': sum(
                fragment['bytes'] for fragment in fragments_before
            ),
            'stage_consolidation_plan': plan,
            'stage_plan_node_count': len(plan),
            'stage_consolidated_node_count': consolidated_nodes,
            'stage_fragment_count_after': len(fragments_after),
            'stage_fragment_bytes_after': sum(
                fragment['bytes'] for fragment in fragments_after
            ),
            'stage_plan_seconds': round(planning_seconds, 6),
            'stage_local_consolidation_seconds': round(
                consolidation_seconds, 6
            ),
            'stage_publish_seconds': round(publish_seconds, 6),
            'stage_finalize_seconds': round(perf_counter() - started, 6),
            **flush_timing,
            **published,
        }


def _shard_name(macros: list[Extents]) -> str:
    """Stable spatial shard name which is valid below local and S3 prefixes."""
    bounds = Bounds(
        min(macro.bounds.minx for macro in macros),
        min(macro.bounds.miny for macro in macros),
        max(macro.bounds.maxx for macro in macros),
        max(macro.bounds.maxy for macro in macros),
    )
    return '-'.join(
        str(int(value))
        for value in (bounds.minx, bounds.miny, bounds.maxx, bounds.maxy)
    )


def _stage_attempt_uri(config: ShatterConfig, shard_name: str) -> str:
    """Return an attempt-local stage path for a macro-v3 shard task.

    A Dask task may be rerun after its worker is killed.  The killed process
    can leave a partially-created TileDB stage on its host-local disk, so a
    stable ``shards/<name>.tdb`` path makes a legitimate retry fail before it
    can do any work.  The published S3 shard name remains stable; only the
    disposable local stage gains a unique attempt component.
    """
    attempt_id = uuid.uuid4().hex
    return (
        f'{config.stage_tdb_dir}/attempts/{shard_name}/{attempt_id}.tdb'
    )


def do_macro_to_shard(
    macros: list[Extents], config: ShatterConfig, storage: Storage
) -> 'MacroTaskResult':
    """Write one compact macro block locally and publish one S3 shard.

    All macros in a block are spatially adjacent and are processed serially by
    one worker.  Their local fragments are consolidated before a single,
    committed S3 array is materialized.  This avoids both a central writer and
    an unbounded number of tiny S3 arrays.
    """
    shard_name = _shard_name(macros)
    stage_uri = _stage_attempt_uri(config, shard_name)
    publish_uri = f'{config.stage_publish_uri}/shards/{shard_name}'
    stage = Storage.create_stage(storage, stage_uri)
    stage_config = copy.deepcopy(config)
    stage_config.tdb_dir = stage_uri
    stage.save_shatter_meta(stage_config)
    macro_results = [do_macro(macro, config, stage) for macro in macros]
    result = combine_macro_results(macro_results)
    if not result.point_count:
        return result
    stage_config.point_count = result.point_count
    stage_config.finished = True
    stage_config.end_timestamp = int(datetime.now().timestamp() * 1000)
    stage.save_shatter_meta(stage_config)
    stage.set_stage_state('locally_validated', shard_name=shard_name)
    stage_started = perf_counter()
    plan = stage.stage_consolidation_plan(config.stage_fragment_size_mb)
    result.stage_consolidation_count = stage.consolidate_stage_plan(
        config.stage_fragment_size_mb
    )
    result.stage_local_fragment_count = len(
        tiledb.array_fragments(stage.config.tdb_dir)
    )
    published = stage.publish_stage(publish_uri, stage_config.time_slot)
    result.stage_publish_seconds = perf_counter() - stage_started
    result.published_fragment_count = published['published_fragment_count']
    result.published_bytes = published['published_bytes']
    result.shard_uri = published['publish_uri']
    return result


def do_macro_to_single_array(
    macros: list[Extents], config: ShatterConfig, storage: Storage
) -> 'MacroTaskResult':
    """Process one spatial block into the canonical macro-v4 array.

    A macro remains the PDAL/metric execution unit, including its read
    collar. ``do_macro`` trims its output to the macro's core grid extent, so
    adjacent blocks write disjoint dense-array cells. TileDB records those
    writes as fragments of *one* array; there is no Group-level mosaic or
    per-shard public array to discover at read time.

    Task retries are intentionally disabled by the caller. A retry after an
    uncertain TileDB commit could append a second fragment for the same cell
    range. Idempotent staged publication is a later macro-v4 enhancement.
    """
    return combine_macro_results(
        [do_macro(macro, config, storage) for macro in macros]
    )


def _durable_stage_uri(
    config: ShatterConfig, block_id: str, attempt_id: str
) -> str:
    """Spread immutable stage attempts across object-store key prefixes."""
    digest = hashlib.sha256(block_id.encode()).hexdigest()
    return (
        f'{config.build_stage_uri.rstrip("/")}/'
        f'{digest[:2]}/{digest[2:4]}/{block_id}/{attempt_id}.tdb'
    )


def _result_details(result: 'MacroTaskResult') -> dict:
    """Convert a compact task result into JSON-safe durable receipt details."""
    return {
        'point_count': int(result.point_count),
        'cell_count': int(result.cell_count),
        'macro_count': int(result.macro_count),
        'read_seconds': float(result.read_seconds),
        'aggregate_seconds': float(result.aggregate_seconds),
        'metric_seconds': float(result.metric_seconds),
        'join_seconds': float(result.join_seconds),
        'write_seconds': float(result.write_seconds),
        'total_seconds': float(result.total_seconds),
        'stage_publish_seconds': float(result.stage_publish_seconds),
        'publish_stage_read_seconds': float(result.publish_stage_read_seconds),
        'publish_precheck_seconds': float(result.publish_precheck_seconds),
        'publish_write_seconds': float(result.publish_write_seconds),
        'publish_postcheck_seconds': float(result.publish_postcheck_seconds),
        'publish_commit_seconds': float(result.publish_commit_seconds),
        'publish_receipt_seconds': float(result.publish_receipt_seconds),
        'publish_stage_data_bytes': int(result.publish_stage_data_bytes),
        'publish_peak_rss_bytes': int(result.publish_peak_rss_bytes),
        'publish_logical_tiledb_reads': int(result.publish_logical_tiledb_reads),
        'publish_logical_tiledb_writes': int(result.publish_logical_tiledb_writes),
        'publish_commit_index_recovered': bool(result.publish_commit_index_recovered),
        'macro_diagnostics': result.macro_diagnostics,
    }


def _result_from_details(
    details: dict, *, stage_uri: str | None = None
) -> 'MacroTaskResult':
    """Recreate a scheduler-sized result from a ledger receipt."""
    return MacroTaskResult(
        point_count=int(details.get('point_count', 0)),
        cell_count=int(details.get('cell_count', 0)),
        read_seconds=float(details.get('read_seconds', 0.0)),
        aggregate_seconds=float(details.get('aggregate_seconds', 0.0)),
        metric_seconds=float(details.get('metric_seconds', 0.0)),
        join_seconds=float(details.get('join_seconds', 0.0)),
        write_seconds=float(details.get('write_seconds', 0.0)),
        total_seconds=float(details.get('total_seconds', 0.0)),
        stage_uri=stage_uri,
        macro_count=int(details.get('macro_count', 1)),
        stage_publish_seconds=float(details.get('stage_publish_seconds', 0.0)),
        publish_stage_read_seconds=float(
            details.get('publish_stage_read_seconds', 0.0)
        ),
        publish_precheck_seconds=float(details.get('publish_precheck_seconds', 0.0)),
        publish_write_seconds=float(details.get('publish_write_seconds', 0.0)),
        publish_postcheck_seconds=float(
            details.get('publish_postcheck_seconds', 0.0)
        ),
        publish_commit_seconds=float(details.get('publish_commit_seconds', 0.0)),
        publish_receipt_seconds=float(
            details.get('publish_receipt_seconds', 0.0)
        ),
        publish_stage_data_bytes=int(details.get('publish_stage_data_bytes', 0)),
        publish_peak_rss_bytes=int(details.get('publish_peak_rss_bytes', 0)),
        publish_logical_tiledb_reads=int(
            details.get('publish_logical_tiledb_reads', 0)
        ),
        publish_logical_tiledb_writes=int(
            details.get('publish_logical_tiledb_writes', 0)
        ),
        publish_commit_index_recovered=bool(
            details.get('publish_commit_index_recovered', False)
        ),
        macro_diagnostics=details.get('macro_diagnostics', []),
    )


def _data_signature(data: pd.DataFrame) -> dict:
    """Return a bounded integrity signature for populated canonical cells.

    TileDB commits a write atomically, but a process can die after a successful
    commit and before its durable receipt is written.  The signature lets a
    resume run distinguish that case from a failed/incomplete write without
    rescanning the source point cloud.  Count and coordinates are sufficient
    to establish the exact populated grid footprint; the schema hash in the
    receipt binds the metric definition used to produce those cells.
    """
    populated = data[data['count'] > 0][['X', 'Y', 'count']].sort_values(
        ['X', 'Y'], ignore_index=True
    )
    hashes = pd.util.hash_pandas_object(populated, index=False).to_numpy(
        dtype='uint64', copy=False
    )
    return {
        'cell_count': int(len(populated)),
        'point_count': int(populated['count'].sum()),
        'xy_count_sha256': hashlib.sha256(hashes.tobytes()).hexdigest(),
    }


def _read_populated_stage(
    stage_uri: str, vfs_parallel_ops: int | None = None
) -> tuple[Storage, pd.DataFrame]:
    """Open a durable stage and return only its populated cells."""
    stage = Storage.from_db(stage_uri)
    if vfs_parallel_ops is not None:
        stage.set_context_overrides(
            **{'vfs.s3.max_parallel_ops': vfs_parallel_ops}
        )
    with stage.open('r') as reader:
        data = reader.df[:, :]
    return stage, data[data['count'] > 0].copy()


def _canonical_signature(
    storage: Storage, staged_data: pd.DataFrame
) -> dict:
    """Read only canonical count cells required to reconcile one stage.

    TileDB commits a dense-array fragment atomically across all attributes.
    The count footprint is consequently enough to distinguish a completed
    commit from no commit, while fetching only a scalar avoids rereading the
    raw variable-length point attributes and every derived metric for each
    publish acknowledgement.
    """
    if staged_data.empty:
        return _data_signature(staged_data)
    minx, maxx = int(staged_data.X.min()), int(staged_data.X.max())
    miny, maxy = int(staged_data.Y.min()), int(staged_data.Y.max())
    with storage.open('r') as reader:
        query = reader.query(attrs=['count'], coords=True)
        if storage.config.dimension_order == 'YX':
            candidate = query.df[miny:maxy, minx:maxx]
        else:
            candidate = query.df[minx:maxx, miny:maxy]
    # Other, disjoint macro cores can lie inside this block's bounding box.
    # They must not make a successful publish appear to have a different
    # footprint. Compare only the cells owned by this immutable stage.
    owned = candidate.merge(
        staged_data[['X', 'Y']], on=['X', 'Y'], how='inner', validate='one_to_one'
    )
    return _data_signature(owned)


def _canonical_point_count(
    storage: Storage, macros: list[Extents], row_chunk: int = 256
) -> int:
    """Sum the canonical count attribute over a bounded build footprint.

    Receipt counts describe what stages produced, not necessarily what
    survived subsequent dense writes. Scan only ``count`` in row chunks so a
    finalizer can detect an overwritten cell without loading point arrays or
    every metric into memory.
    """
    if not macros:
        return 0
    x1 = min(macro.x1 for macro in macros)
    x2 = max(macro.x2 for macro in macros)
    y1 = min(macro.y1 for macro in macros)
    y2 = max(macro.y2 for macro in macros)
    total = 0
    with storage.open('r') as reader:
        for row in range(y1, y2, row_chunk):
            stop = min(row + row_chunk, y2) - 1
            query = reader.query(attrs=['count'])
            if storage.config.dimension_order == 'YX':
                values = query.df[row:stop, x1:x2 - 1]
            else:
                values = query.df[x1:x2 - 1, row:stop]
            total += int(values['count'].sum())
    return total


def _stage_macro_partitions(
    staged_data: pd.DataFrame, macros: list[Extents]
) -> list[pd.DataFrame]:
    """Partition a stage into disjoint, coalesced rectangles for dense writes.

    ``Storage.write`` must fill every cell in a rectangular TileDB slice.
    Writing the bounding rectangle of an irregular multi-macro block would
    fill its holes with zero and erase cells belonging to other blocks.
    """
    x = staged_data['X'].to_numpy()
    y = staged_data['Y'].to_numpy()
    assigned = np.zeros(len(staged_data), dtype=bool)
    partitions = []
    for x1, y1, x2, y2 in _coalesced_macro_rectangles(macros):
        in_macro = (
            (x >= x1) & (x < x2) & (y >= y1) & (y < y2)
        )
        if np.any(assigned & in_macro):
            raise RuntimeError('Macro-v4 stage has overlapping write rectangles.')
        assigned |= in_macro
        if np.any(in_macro):
            partitions.append(staged_data.loc[in_macro])
    if not np.all(assigned):
        raise RuntimeError('Macro-v4 stage has cells outside its planned cores.')
    return partitions


def _coalesced_macro_rectangles(
    macros: list[Extents],
) -> list[tuple[int, int, int, int]]:
    """Cover disjoint macro cores with fewer rectangles, without filling holes.

    Sweep the exact grid-row boundaries of the planned cores. Each row band
    merges touching X intervals, then identical intervals in neighboring
    bands merge vertically. Unlike a bounding-box write, every returned
    rectangle lies wholly inside the union of the planned cores.
    """
    if not macros:
        return []
    y_edges = sorted({edge for macro in macros for edge in (macro.y1, macro.y2)})
    active: dict[tuple[int, int], tuple[int, int, int, int]] = {}
    completed = []
    for y1, y2 in zip(y_edges, y_edges[1:]):
        intervals = sorted(
            (macro.x1, macro.x2)
            for macro in macros
            if macro.y1 <= y1 and macro.y2 >= y2
        )
        merged = []
        for x1, x2 in intervals:
            if x1 >= x2:
                raise RuntimeError('Macro-v4 stage has an empty macro core.')
            if merged and x1 < merged[-1][1]:
                raise RuntimeError('Macro-v4 stage has overlapping macro cores.')
            if merged and x1 == merged[-1][1]:
                merged[-1] = (merged[-1][0], x2)
            else:
                merged.append((x1, x2))
        current = set(merged)
        for interval in tuple(active):
            if interval not in current:
                completed.append(active.pop(interval))
        for x1, x2 in merged:
            key = (x1, x2)
            if key in active:
                old = active[key]
                active[key] = (x1, old[1], x2, y2)
            else:
                active[key] = (x1, y1, x2, y2)
    completed.extend(active.values())
    return sorted(completed, key=lambda rect: (rect[1], rect[0], rect[3]))


def _block_schema_hash(storage: Storage) -> str:
    """Tie stages to the stable, output-affecting storage definition.

    ``tdb_dir`` is intentionally excluded: a durable stage and its canonical
    destination must be distinct arrays, but they have the same schema.  The
    ledger separately binds a build to its canonical URI, so removing it here
    does not permit a foreign build to publish. ``next_time_slot`` is also
    excluded: reserving a slot is normal build bookkeeping and must not turn
    an otherwise identical resume into a different schema. Logging is
    similarly not part of the array contract.
    """
    # The exact document persisted by the canonical array is its storage
    # contract. Re-serializing callable metric/filter definitions after a
    # Dask process boundary can change their dill bytes even though the
    # effective TileDB schema is unchanged.
    serialized = getattr(storage, '_serialized_config', None)
    schema = (
        json.loads(serialized)
        if serialized is not None
        else storage.config.to_json()
    )
    schema.pop('tdb_dir', None)
    schema.pop('next_time_slot', None)
    schema.pop('log', None)
    return hashlib.sha256(
        json.dumps(schema, sort_keys=True, default=str).encode()
    ).hexdigest()


def _build_inputs(
    config: ShatterConfig, storage: Storage, block_ids: list[str]
) -> dict:
    """Identify the reproducible inputs of one resumable macro-v4 build."""
    return {
        'canonical_uri': config.tdb_dir.rstrip('/'),
        'schema_sha256': _block_schema_hash(storage),
        'source': config.filename,
        'date': [value.isoformat() for value in config.date],
        'bounds': config.bounds.get() if config.bounds is not None else None,
        'tile_size': config.tile_size,
        'read_group_size': config.read_group_size,
        'processing_halo_m': config.processing_halo_m,
        'water_mask_uri': config.water_mask_uri,
        'block_ids': block_ids,
    }


def _build_signature(
    config: ShatterConfig, storage: Storage, block_ids: list[str]
) -> str:
    """Return an auditable stable hash of a macro-v4 build's inputs."""
    payload = _build_inputs(config, storage, block_ids)
    return hashlib.sha256(
        json.dumps(payload, sort_keys=True, default=str).encode()
    ).hexdigest()


def _assert_disjoint_macro_v4_append(
    config: ShatterConfig, storage: Storage, extents: Extents
) -> None:
    """Keep a later build from replacing cells owned by an earlier build.

    Dense stage writes fill rectangular slices, including empty cells.  A
    second build may therefore append to the same canonical array only when
    its *aligned pixel domain* is disjoint from every earlier shatter.  A
    different unfinished build is also refused: its published footprint is
    not yet a stable base for an append.  The caller must still serialize
    independent planners at the orchestration layer; array history is not a
    distributed lock.
    """
    for slot in range(1, storage.config.next_time_slot):
        try:
            previous = storage.get_shatter_meta(slot)
        except KeyError:
            # Reserving a history slot precedes publishing its metadata.
            continue
        if str(previous.name) == str(config.name):
            continue
        if not previous.finished:
            raise ValueError(
                'Cannot append while another build on the canonical array '
                f'is unfinished: {previous.name}.'
            )
        for field in ('date', 'usgs_albers', 'water_mask_uri',
                      'processing_halo_m', 'read_group_size'):
            if getattr(previous, field) != getattr(config, field):
                raise ValueError(
                    f'Macro-v4 append would mix {field} across builds; '
                    f'prior build is {previous.name}.'
                )
        if previous.bounds is None:
            raise ValueError(
                f'Cannot prove that prior build {previous.name} is disjoint: '
                'its bounds are missing.'
            )
        prior = Extents.from_sub(
            storage, Bounds(*previous.bounds.get())
        )
        if not (
            extents.x2 <= prior.x1 or prior.x2 <= extents.x1
            or extents.y2 <= prior.y1 or prior.y2 <= extents.y1
        ):
            raise ValueError(
                'Macro-v4 append would overlap pixels owned by prior '
                f'build {previous.name}; use disjoint grid-aligned bounds.'
            )


@dataclass(frozen=True)
class MacroV4BatchPlan:
    """A durable macro-v4 plan ready for an external work dispatcher.

    The plan deliberately contains identifiers rather than Dask futures or
    point data.  Batch, SQS, and a later recovery driver can all reconstruct
    a block from its immutable ledger receipt and the canonical array's
    persisted shatter configuration.
    """

    build_id: str
    ledger_uri: str
    canonical_uri: str
    root_block_ids: tuple[str, ...]
    active_block_ids: tuple[str, ...]
    planner: dict
    resumed: bool


def _macro_v4_batch_config(
    tdb_dir: str, build_ledger_uri: str, build_id: str | uuid.UUID
) -> tuple[ShatterConfig, Storage, BuildLedger]:
    """Load and verify the persisted contract for one external work unit."""
    storage = Storage.from_db(tdb_dir)
    ledger = BuildLedger(build_ledger_uri, Storage.get_tdb_context(storage))
    manifest = ledger.build_manifest()
    expected_id = str(build_id)
    if manifest is None:
        raise ValueError(
            f'No macro-v4 build manifest exists at {build_ledger_uri!r}.'
        )
    if manifest.details.get('build_id') != expected_id:
        raise ValueError(
            'The requested build id does not match the ledger manifest: '
            f'{expected_id!r} != {manifest.details.get("build_id")!r}.'
        )
    if manifest.details.get('canonical_uri', '').rstrip('/') != tdb_dir.rstrip('/'):
        raise ValueError(
            'The requested canonical array does not match the ledger manifest.'
        )
    time_slot = manifest.details.get('time_slot')
    if time_slot is None:
        raise ValueError(
            'The build ledger has no recoverable macro-v4 history time slot.'
        )
    config = storage.get_shatter_meta(int(time_slot))
    if (
        config.processing_strategy != 'macro-v4-staged-publish'
        or str(config.name) != expected_id
        or config.build_ledger_uri != build_ledger_uri
    ):
        raise ValueError(
            'Canonical-array history does not match the requested macro-v4 '
            'external work unit.'
        )
    return config, storage, ledger


def _macro_v4_batch_block(
    block_id: str, ledger: BuildLedger, storage: Storage
) -> tuple[list[Extents], int]:
    """Reconstruct an active work unit from its append-only receipts."""
    current = ledger.state(block_id)
    if current is None:
        raise ValueError(f'No macro-v4 receipt exists for block {block_id!r}.')
    if current.state == 'split':
        raise ValueError(
            f'Macro-v4 block {block_id!r} is a split parent, not an active leaf.'
        )
    descriptor = current.details.get('macro_bounds')
    split_depth = current.details.get('split_depth')
    # A failed/publishing observation intentionally contains only failure or
    # acknowledgement details.  Find the original planned receipt carrying
    # the stable reconstruction descriptor.
    if descriptor is None:
        for record in reversed(ledger.records(block_id)):
            descriptor = record.details.get('macro_bounds')
            if descriptor is not None:
                split_depth = record.details.get('split_depth', split_depth)
                break
    if descriptor is None:
        raise ValueError(
            f'Macro-v4 block {block_id!r} has no reconstruction descriptor.'
        )
    return _block_from_descriptor(descriptor, storage), int(split_depth or 0)


def plan_macro_v4_staged_build(config: ShatterConfig) -> MacroV4BatchPlan:
    """Persist a macro-v4 plan without starting a Dask or PDAL worker fleet.

    ``macro-v4-staged-publish`` already uses immutable stages and receipts;
    this function separates its planning phase so an external dispatcher can
    run each active leaf in an isolated Batch job.  It is safe to invoke again
    with the same build id and inputs: the ledger identity check preserves the
    original history slot and rejects foreign inputs.
    """
    if config.processing_strategy != 'macro-v4-staged-publish':
        raise ValueError(
            'plan_macro_v4_staged_build requires macro-v4-staged-publish.'
        )
    if config.start_timestamp is None:
        config.start_timestamp = int(datetime.now().timestamp() * 1000)

    storage = Storage.from_db(config.tdb_dir)
    if config.usgs_albers != storage.config.usgs_albers:
        raise ValueError(
            '--usgs_albers must be used exactly when the destination was '
            'initialized with the USGS Albers grid profile'
        )
    data = Data(config.filename, storage.config, config.bounds)
    extents = Extents.from_sub(config.tdb_dir, data.bounds)
    config.bounds = data.bounds
    _assert_disjoint_macro_v4_append(config, storage, extents)
    ledger = BuildLedger(config.build_ledger_uri, Storage.get_tdb_context(storage))

    if not config.time_slot:
        original_manifest = ledger.build_manifest()
        if (
            original_manifest is not None
            and original_manifest.details.get('build_id') == str(config.name)
        ):
            original_time_slot = original_manifest.details.get('time_slot')
            if original_time_slot is None:
                raise ValueError(
                    'The existing build ledger predates time-slot recovery and '
                    'cannot safely resume this canonical-array build.'
                )
            config.time_slot = int(original_time_slot)
    leaf_size = storage.config.ysize * storage.config.xsize
    potential_leaves = extents.get_root_aligned_leaf_children(leaf_size)
    macros = [
        macro
        for extent in potential_leaves
        for macro in extents.get_overlap(extent).get_leaf_children(
            config.read_group_size
        )
    ]
    macro_blocks, planner = plan_macro_blocks(macros, config, storage, data)
    # A transient source/planner failure must not consume a canonical history
    # slot. Reserve only after the bounded plan has actually completed.
    if not config.time_slot:
        config.time_slot = storage.reserve_time_slot()
    root_blocks = {_shard_name(block): block for block in macro_blocks}
    if len(root_blocks) != len(macro_blocks):
        raise RuntimeError('Macro-v4 planner produced non-unique block IDs.')
    root_block_ids = tuple(sorted(root_blocks))
    build_signature = _build_signature(config, storage, list(root_block_ids))
    build_inputs = _build_inputs(config, storage, list(root_block_ids))
    original_manifest = ledger.build_manifest()
    resumed = ledger.assert_build_identity(
        build_id=str(config.name),
        canonical_uri=config.tdb_dir,
        build_signature=build_signature,
        build_inputs=build_inputs,
    )
    if not resumed:
        ledger.append(
            '__build__',
            'build_started',
            build_id=str(config.name),
            canonical_uri=config.tdb_dir,
            planned_block_count=len(root_block_ids),
            schema_sha256=_block_schema_hash(storage),
            build_signature=build_signature,
            build_inputs=build_inputs,
            time_slot=config.time_slot,
        )
    else:
        config.time_slot = int(original_manifest.details['time_slot'])

    storage.set_context_overrides(
        **{'vfs.s3.max_parallel_ops': config.build_publish_vfs_parallel_ops}
    )
    storage.save_shatter_meta(config)
    for block_id in root_block_ids:
        if ledger.state(block_id) is None:
            ledger.append(
                block_id,
                'planned',
                **_ledger_block_details(
                    root_blocks[block_id], parent_id=None, split_depth=0
                ),
            )
    active_blocks = _active_ledger_blocks(root_blocks, ledger, storage)
    return MacroV4BatchPlan(
        build_id=str(config.name),
        ledger_uri=config.build_ledger_uri,
        canonical_uri=config.tdb_dir,
        root_block_ids=root_block_ids,
        active_block_ids=tuple(sorted(active_blocks)),
        planner=planner,
        resumed=resumed,
    )


def stage_macro_v4_batch_block(
    tdb_dir: str, build_ledger_uri: str, build_id: str | uuid.UUID, block_id: str
) -> 'MacroTaskResult':
    """Run one reconstructed macro-v4 stage outside Dask.

    Batch workers call this after acquiring an external lease.  The function
    deliberately does not claim or lock work: object-store receipts remain
    portable while DynamoDB supplies the queue-specific atomic lease.
    """
    config, storage, ledger = _macro_v4_batch_config(
        tdb_dir, build_ledger_uri, build_id
    )
    macros, _ = _macro_v4_batch_block(block_id, ledger, storage)
    return stage_macro_block(macros, config, storage, build_ledger_uri)


def publish_macro_v4_batch_block(
    tdb_dir: str, build_ledger_uri: str, build_id: str | uuid.UUID, block_id: str
) -> 'MacroTaskResult':
    """Publish one durable macro-v4 stage outside Dask."""
    config, _storage, _ledger = _macro_v4_batch_config(
        tdb_dir, build_ledger_uri, build_id
    )
    return publish_staged_macro_block(block_id, config, build_ledger_uri)


def _macro_v4_root_blocks(
    ledger: BuildLedger, storage: Storage
) -> dict[str, list[Extents]]:
    """Reconstruct root cores, including roots that later split into leaves."""
    manifest = ledger.build_manifest()
    if manifest is None:
        raise ValueError('The macro-v4 build has no durable manifest.')
    root_ids = manifest.details.get('build_inputs', {}).get('block_ids')
    if not root_ids:
        raise ValueError('The macro-v4 build manifest has no root block IDs.')
    root_blocks: dict[str, list[Extents]] = {}
    for root_id in root_ids:
        descriptor = None
        for record in reversed(ledger.records(root_id)):
            descriptor = record.details.get('macro_bounds')
            if descriptor is not None:
                break
        if descriptor is None:
            raise ValueError(
                f'Macro-v4 root {root_id!r} has no reconstruction descriptor.'
            )
        root_blocks[root_id] = _block_from_descriptor(descriptor, storage)
    return root_blocks


def complete_macro_v4_batch_build(
    tdb_dir: str, build_ledger_uri: str, build_id: str | uuid.UUID
) -> dict:
    """Record the durable Batch-to-finalizer handoff after all leaves publish.

    A Batch completion monitor calls this operation instead of inferring
    success from job counts.  The ledger can include adaptive children, so it
    reconstructs the active plan tree and refuses to seal a build with even
    one missing/staged/failed leaf.
    """
    config, storage, ledger = _macro_v4_batch_config(
        tdb_dir, build_ledger_uri, build_id
    )
    root_blocks = _macro_v4_root_blocks(ledger, storage)
    root_ids = tuple(root_blocks)

    active_blocks = _active_ledger_blocks(root_blocks, ledger, storage)
    active_block_ids = sorted(active_blocks)
    summary = ledger.summary(active_block_ids)
    records = summary['records']
    incomplete = {
        block_id: record.state if record is not None else 'missing'
        for block_id, record in records.items()
        if record is None or record.state != 'published'
    }
    if incomplete:
        raise RuntimeError(
            'Cannot complete this macro-v4 Batch build while active leaves '
            f'are incomplete: {incomplete!r}'
        )

    point_count = sum(
        int(record.details.get('point_count', 0))
        for record in records.values()
        if record is not None
    )
    canonical_count = _canonical_point_count(
        storage, [macro for block in root_blocks.values() for macro in block]
    )
    if canonical_count != point_count:
        raise RuntimeError(
            'Cannot complete macro-v4 Batch build: canonical count attribute '
            f'contains {canonical_count} points, but published receipts total '
            f'{point_count} points.'
        )
    existing_stage_complete = _latest_build_receipt(
        ledger, 'build_stage_tasks_complete'
    )
    if existing_stage_complete is None:
        ledger.append(
            '__build__',
            'build_stage_tasks_complete',
            stage_task_count=len(active_block_ids),
            staged_block_count=len(active_block_ids),
            pending_publish_block_count=0,
            executor='batch',
        )
    published_build = {
        'build_id': str(build_id),
        'planned_block_count': len(active_block_ids),
        'root_block_count': len(root_ids),
        'published_block_count': len(active_block_ids),
        'point_count': point_count,
        'canonical_point_count': canonical_count,
        'executor': 'batch',
    }
    if _latest_build_receipt(ledger, 'build_published') is None:
        ledger.append('__build__', 'build_published', **published_build)
    # The finalizer reads the durable receipt rather than this return value.
    # Returning it lets a queue monitor render progress without scanning
    # TileDB fragments or trusting approximate SQS counts.
    return {**published_build, 'states': summary['states']}


def split_macro_v4_batch_block(
    tdb_dir: str,
    build_ledger_uri: str,
    build_id: str | uuid.UUID,
    block_id: str,
    error: BaseException,
) -> list[tuple[str, list[Extents], int]]:
    """Record an externally observed failure and create adaptive children.

    This is used by a small on-demand reconciliation job after an OOM-killed
    Batch container, where Python in the original stage process had no chance
    to emit its own failure receipt.
    """
    config, storage, ledger = _macro_v4_batch_config(
        tdb_dir, build_ledger_uri, build_id
    )
    macros, split_depth = _macro_v4_batch_block(block_id, ledger, storage)
    return _split_failed_stage_block(
        block_id, macros, split_depth, error, config, ledger
    )


def presplit_macro_v4_batch_block(
    tdb_dir: str,
    build_ledger_uri: str,
    build_id: str | uuid.UUID,
    block_id: str,
) -> list[tuple[str, list[Extents], int]]:
    """Subdivide an unreleased planned Batch leaf before consuming capacity.

    The operator must first claim the leaf in its external control ledger. A
    durable split receipt precedes child receipts, so a crash between the two
    can be resumed without changing the plan or duplicating canonical writes.
    Unlike failure recovery, this does not invent a failed stage attempt.
    """
    config, storage, ledger = _macro_v4_batch_config(
        tdb_dir, build_ledger_uri, build_id
    )
    current = ledger.state(block_id)
    if current is None:
        raise ValueError(f'No macro-v4 receipt exists for block {block_id!r}.')
    if current.state == 'split':
        if current.details.get('failure_kind') != 'capacity_preflight':
            raise ValueError(f'Block {block_id!r} was split after a stage failure.')
        child_records = current.details['children']
        children = [
            _block_from_descriptor(record['macro_bounds'], storage)
            for record in child_records
        ]
        split_depth = int(current.details['split_depth'])
    else:
        if current.state != 'planned':
            raise ValueError(
                f'Block {block_id!r} is {current.state!r}, not an unreleased plan.'
            )
        macros, split_depth = _macro_v4_batch_block(block_id, ledger, storage)
        if split_depth >= config.build_max_split_depth:
            raise ValueError(f'Block {block_id!r} is at its maximum split depth.')
        children = split_macro_block_once(macros, config)
        if not children:
            raise ValueError(f'Block {block_id!r} cannot be subdivided further.')
        child_records = [
            {
                'block_id': _shard_name(child),
                'macro_bounds': _block_descriptor(child),
                'bounds': _block_bounds(child).get(),
                'split_depth': split_depth + 1,
            }
            for child in children
        ]
        if block_id in {record['block_id'] for record in child_records} or len(
            {record['block_id'] for record in child_records}
        ) != len(child_records):
            raise RuntimeError(f'Preflight split of {block_id!r} is not disjoint.')
        ledger.append(
            block_id,
            'split',
            failure_kind='capacity_preflight',
            split_depth=split_depth,
            children=child_records,
        )

    for child, record in zip(children, child_records):
        if ledger.state(record['block_id']) is None:
            ledger.append(
                record['block_id'],
                'planned',
                **_ledger_block_details(
                    child, parent_id=block_id, split_depth=split_depth + 1
                ),
            )
    return [
        (record['block_id'], child, split_depth + 1)
        for record, child in zip(child_records, children)
    ]


def stage_macro_block(
    macros: list[Extents],
    config: ShatterConfig,
    storage: Storage,
    ledger_uri: str,
) -> 'MacroTaskResult':
    """Compute one deterministic macro block into a durable, immutable stage.

    This is safe to retry: every attempt uses a fresh stage URI, and only a
    fully written stage receives a ``staged`` ledger receipt.  Unacknowledged
    attempt directories are disposable and cannot be published by recovery.
    """
    block_id = _shard_name(macros)
    ledger = BuildLedger(ledger_uri, Storage.get_tdb_context(storage))
    current = ledger.state(block_id)
    if current is not None and current.state == 'published':
        result = _result_from_details(current.details)
        result.block_id = block_id
        return result
    if current is not None and current.state == 'staged':
        result = _result_from_details(
            current.details, stage_uri=current.details.get('stage_uri')
        )
        result.block_id = block_id
        return result

    attempt_id = uuid.uuid4().hex
    ledger.append(
        block_id,
        'computing',
        attempt_id=attempt_id,
        macro_count=len(macros),
        bounds=_block_bounds(macros).get(),
    )
    stage_uri = _durable_stage_uri(config, block_id, attempt_id)
    stage = Storage.create_stage(storage, stage_uri)
    stage.set_context_overrides(
        **{'vfs.s3.max_parallel_ops': config.build_stage_vfs_parallel_ops}
    )
    stage_config = copy.deepcopy(config)
    stage_config.tdb_dir = stage_uri
    stage.save_shatter_meta(stage_config)
    result = combine_macro_results(
        [do_macro(macro, stage_config, stage) for macro in macros]
    )
    stage_config.point_count = result.point_count
    stage_config.finished = True
    stage_config.end_timestamp = int(datetime.now().timestamp() * 1000)
    stage.save_shatter_meta(stage_config)
    stage.set_stage_state('durably_staged', block_id=block_id)

    _, staged_data = _read_populated_stage(
        stage_uri, config.build_stage_vfs_parallel_ops
    )
    signature = _data_signature(staged_data)
    if signature['point_count'] != result.point_count:
        raise RuntimeError(
            f'Durable stage {stage_uri!r} point count does not match its '
            'macro result.'
        )
    result.stage_uri = stage_uri
    result.block_id = block_id
    receipt = {
        **_result_details(result),
        'stage_uri': stage_uri,
        'signature': signature,
        'schema_sha256': _block_schema_hash(storage),
        'bounds': _block_bounds(macros).get(),
        # The stage URI is immutable; this compact manifest binds its logical
        # contents and schema to the later publisher without adding a global
        # TileDB metadata write.
        'stage_manifest': {
            'version': 1,
            'uri': stage_uri,
            'logical_data_bytes': int(staged_data.memory_usage(deep=True).sum()),
            'signature': signature,
            'schema_sha256': _block_schema_hash(storage),
        },
    }
    ledger.append(block_id, 'staged', attempt_id=attempt_id, **receipt)
    return result


def publish_staged_macro_block(
    block_id: str,
    config: ShatterConfig,
    ledger_uri: str,
) -> 'MacroTaskResult':
    """Publish one staged block through a bounded canonical-array writer.

    A ``published`` receipt is emitted only after a range-limited canonical
    read matches the stage signature.  If the process died after a successful
    write but before that receipt, a later run detects the matching range and
    records a reconciled publish rather than writing it again.
    """
    canonical = Storage.from_db(config.tdb_dir)
    canonical.set_context_overrides(
        **{'vfs.s3.max_parallel_ops': config.build_publish_vfs_parallel_ops}
    )
    ledger = BuildLedger(ledger_uri, Storage.get_tdb_context(canonical))
    current = ledger.state(block_id)
    if current is None or current.state not in {'staged', 'published'}:
        raise RuntimeError(f'No staged receipt exists for block {block_id!r}')
    if current.state == 'published':
        result = _result_from_details(current.details)
        result.block_id = block_id
        return result

    details = current.details
    macros, _ = _macro_v4_batch_block(block_id, ledger, canonical)
    stage_uri = details['stage_uri']
    result = _result_from_details(details, stage_uri=stage_uri)
    if details['schema_sha256'] != _block_schema_hash(canonical):
        raise RuntimeError(
            f'Staged block {block_id!r} was built with a different schema.'
        )
    # A publish commit is written only after the strict post-write canonical
    # verification below.  It makes the usual crash window (write succeeded,
    # receipt not yet appended) cheap to recover without weakening normal
    # correctness checks.
    commit = ledger.publish_commit(block_id)
    if commit is not None:
        committed = commit.get('details', {})
        if (
            committed.get('stage_uri') == stage_uri
            and committed.get('signature') == details['signature']
            and committed.get('schema_sha256') == details['schema_sha256']
        ):
            result.publish_commit_index_recovered = True
            result.publish_commit_seconds = float(
                committed.get('publish_commit_seconds', 0.0)
            )
            ledger.append(
                block_id,
                'published',
                stage_uri=stage_uri,
                reconciled=True,
                commit_index_uri=commit['uri'],
                commit_index_recovered=True,
                **_result_details(result),
                signature=details['signature'],
                schema_sha256=details['schema_sha256'],
                bounds=details['bounds'],
            )
            return result

    monitor = MacroMemoryMonitor()
    published_started = perf_counter()
    stage_read_started = perf_counter()
    _stage, staged_data = _read_populated_stage(
        stage_uri, config.build_publish_vfs_parallel_ops
    )
    result.publish_stage_read_seconds = perf_counter() - stage_read_started
    result.publish_stage_data_bytes = int(staged_data.memory_usage(deep=True).sum())
    result.publish_logical_tiledb_reads = 1
    actual_signature = _data_signature(staged_data)
    if actual_signature != details['signature']:
        raise RuntimeError(
            f'Staged block {block_id!r} no longer matches its ledger receipt.'
        )
    ledger.append(block_id, 'publishing', stage_uri=stage_uri)
    precheck_started = perf_counter()
    reconciled = _canonical_signature(canonical, staged_data) == actual_signature
    result.publish_precheck_seconds = perf_counter() - precheck_started
    result.publish_logical_tiledb_reads += 1
    if not reconciled and not staged_data.empty:
        write_started = perf_counter()
        partitions = _stage_macro_partitions(staged_data, macros)
        for partition in partitions:
            canonical.write(
                partition.drop(columns=['start_time', 'end_time']).rename(
                    columns={'X': 'xi', 'Y': 'yi'}
                ),
                config.date,
            )
        result.publish_write_seconds = perf_counter() - write_started
        result.publish_logical_tiledb_writes = len(partitions)
    postcheck_started = perf_counter()
    if _canonical_signature(canonical, staged_data) != actual_signature:
        raise RuntimeError(
            f'Canonical write for block {block_id!r} could not be reconciled.'
        )
    result.publish_postcheck_seconds = perf_counter() - postcheck_started
    result.publish_logical_tiledb_reads += 1
    result.publish_peak_rss_bytes = int(monitor.stop()['rss_peak_bytes'] or 0)
    result.stage_publish_seconds = perf_counter() - published_started
    commit_started = perf_counter()
    commit_uri = ledger.append_publish_commit(
        block_id,
        stage_uri=stage_uri,
        signature=actual_signature,
        schema_sha256=details['schema_sha256'],
        bounds=details['bounds'],
        **_result_details(result),
    )
    result.publish_commit_seconds = perf_counter() - commit_started
    result.block_id = block_id
    receipt_started = perf_counter()
    ledger.append(
        block_id,
        'published',
        stage_uri=stage_uri,
        reconciled=reconciled,
        commit_index_uri=commit_uri,
        **_result_details(result),
        signature=actual_signature,
        schema_sha256=details['schema_sha256'],
        bounds=details['bounds'],
    )
    result.publish_receipt_seconds = perf_counter() - receipt_started
    return result


def do_macro_to_partial_stage(
    macro: Extents,
    block_name: str,
    config: ShatterConfig,
    storage: Storage,
) -> 'MacroTaskResult':
    """Process one macro into a worker-local, non-published partial stage."""
    macro_name = _shard_name([macro])
    stage_uri = (
        f'{config.stage_tdb_dir}/partials/{block_name}/{macro_name}.tdb'
    )
    stage = Storage.create_stage(storage, stage_uri)
    stage_config = copy.deepcopy(config)
    stage_config.tdb_dir = stage_uri
    stage.save_shatter_meta(stage_config)
    result = do_macro(macro, config, stage)
    if not result.point_count:
        return result
    stage_config.point_count = result.point_count
    stage_config.finished = True
    stage_config.end_timestamp = int(datetime.now().timestamp() * 1000)
    stage.save_shatter_meta(stage_config)
    stage.set_stage_state('local_partial_validated', block_name=block_name)
    result.stage_uri = stage_uri
    return result


def finalize_macro_block(
    macro_results: list['MacroTaskResult'],
    macros: list[Extents],
    config: ShatterConfig,
    storage: Storage,
) -> 'MacroTaskResult':
    """Merge local partial stages, consolidate once, and commit one S3 shard.

    The caller pins this task to the same worker as the partial tasks, so only
    lightweight result records cross Dask.  Point/metric DataFrames never
    leave that worker's local disk and memory.
    """
    result = combine_macro_results(macro_results)
    if not result.point_count:
        return result
    stage_started = perf_counter()
    shard_name = _shard_name(macros)
    stage_uri = f'{config.stage_tdb_dir}/shards/{shard_name}.tdb'
    publish_uri = f'{config.stage_publish_uri}/shards/{shard_name}'
    stage = Storage.create_stage(storage, stage_uri)
    stage_config = copy.deepcopy(config)
    stage_config.tdb_dir = stage_uri
    stage.save_shatter_meta(stage_config)
    for partial_result in macro_results:
        if not partial_result.stage_uri:
            continue
        partial = Storage.from_db(partial_result.stage_uri)
        with partial.open('r') as reader:
            data = reader.df[:, :]
        data = data[data['count'] > 0]
        if not data.empty:
            stage.write(
                data.drop(columns=['start_time', 'end_time']).rename(
                    columns={'X': 'xi', 'Y': 'yi'}
                ),
                config.date,
            )
    stage_config.point_count = result.point_count
    stage_config.finished = True
    stage_config.end_timestamp = int(datetime.now().timestamp() * 1000)
    stage.save_shatter_meta(stage_config)
    stage.set_stage_state('locally_merged', shard_name=shard_name)
    result.stage_consolidation_count = stage.consolidate_stage_plan(
        config.stage_fragment_size_mb
    )
    result.stage_local_fragment_count = len(
        tiledb.array_fragments(stage.config.tdb_dir)
    )
    published = stage.publish_stage(publish_uri, stage_config.time_slot)
    result.stage_publish_seconds = perf_counter() - stage_started
    result.total_seconds += result.stage_publish_seconds
    result.published_fragment_count = published['published_fragment_count']
    result.published_bytes = published['published_bytes']
    result.shard_uri = published['publish_uri']
    return result


def do_macro_to_stage(
    macro: Extents,
    config: ShatterConfig,
    storage: Storage,
    stage_writer: MacroStageWriter,
) -> 'MacroTaskResult':
    """Process a macro and stream its aggregate to the local stage owner."""
    if config.mbr:
        if not all(macro.disjoint_by_mbr(m) for m in config.mbr):
            return MacroTaskResult.empty()

    task_started = perf_counter()
    phase_started = task_started
    points = get_data(
        macro,
        config.filename,
        storage,
        reader_collar=config.processing_halo_m,
        water_mask_uri=config.water_mask_uri,
    )
    read_seconds = perf_counter() - phase_started
    if points.empty:
        return MacroTaskResult(
            0,
            0,
            read_seconds,
            0.0,
            0.0,
            0.0,
            0.0,
            perf_counter() - task_started,
        )

    phase_started = perf_counter()
    listed_data = agg_list(points, config.time_slot)
    aggregate_seconds = perf_counter() - phase_started
    phase_started = perf_counter()
    metric_data = run_graph(points, storage.get_metrics())
    metric_seconds = perf_counter() - phase_started
    phase_started = perf_counter()
    joined_data = join(listed_data, metric_data)
    join_seconds = perf_counter() - phase_started
    cell_count = len(joined_data)
    del points, listed_data, metric_data

    phase_started = perf_counter()
    point_count = _resolve_actor_result(
        stage_writer.write(joined_data, config.date)
    )
    write_seconds = perf_counter() - phase_started
    del joined_data
    return MacroTaskResult(
        point_count=point_count,
        cell_count=cell_count,
        read_seconds=read_seconds,
        aggregate_seconds=aggregate_seconds,
        metric_seconds=metric_seconds,
        join_seconds=join_seconds,
        write_seconds=write_seconds,
        total_seconds=perf_counter() - task_started,
    )


@dataclass
class MacroTaskResult:
    """Small, serializable macro-v2 timing record returned to the scheduler."""

    point_count: int
    cell_count: int
    read_seconds: float
    aggregate_seconds: float
    metric_seconds: float
    join_seconds: float
    write_seconds: float
    total_seconds: float
    shard_uri: str | None = None
    stage_uri: str | None = None
    block_id: str | None = None
    macro_count: int = 1
    stage_consolidation_count: int = 0
    stage_local_fragment_count: int = 0
    published_fragment_count: int = 0
    published_bytes: int = 0
    stage_publish_seconds: float = 0.0
    publish_stage_read_seconds: float = 0.0
    publish_precheck_seconds: float = 0.0
    publish_write_seconds: float = 0.0
    publish_postcheck_seconds: float = 0.0
    publish_commit_seconds: float = 0.0
    publish_receipt_seconds: float = 0.0
    publish_stage_data_bytes: int = 0
    publish_peak_rss_bytes: int = 0
    publish_logical_tiledb_reads: int = 0
    publish_logical_tiledb_writes: int = 0
    publish_commit_index_recovered: bool = False
    macro_diagnostics: list[dict] = field(default_factory=list)

    @classmethod
    def empty(cls) -> 'MacroTaskResult':
        return cls(0, 0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0)


def combine_macro_results(results: list[MacroTaskResult]) -> MacroTaskResult:
    """Reduce serial macro work in a spatial publish block to one record."""
    phases = (
        'read_seconds',
        'aggregate_seconds',
        'metric_seconds',
        'join_seconds',
        'write_seconds',
        'total_seconds',
    )
    values = {
        phase: sum(getattr(result, phase) for result in results)
        for phase in phases
    }
    return MacroTaskResult(
        point_count=sum(result.point_count for result in results),
        cell_count=sum(result.cell_count for result in results),
        macro_count=len(results),
        macro_diagnostics=[
            diagnostic
            for result in results
            for diagnostic in result.macro_diagnostics
        ],
        **values,
    )


Leaves = Generator[Extents, None, None]


def run(leaves: Leaves, config: ShatterConfig, storage: Storage) -> int:
    """
    Coordinate running of shatter process and handle any interruptions

    :param leaves: Generator of Leaf nodes.
    :param config: :class:`silvimetric.resources.config.ShatterConfig`
    :param storage: :class:`silvimetric.resources.storage.Storage`
    :return: Number of points processed.
    """

    start_time = int(datetime.now().timestamp()*1000)
    dc = get_client()

    joined_dfs = []
    failures = []

    if dc is not None:
        futures = [
            dc.submit(do_one, leaf=leaf, config=config, storage=storage)
            for leaf in leaves
        ]
        res = as_completed(futures, with_results=True, raise_errors=False)
        for future, df in res:
            if future.status == 'error':
                failures.append((future, df))
                continue

            if df is not None:
                joined_dfs.append(df)
            del df

        if failures:
            messages = []
            for future, error in failures:
                key = getattr(future, 'key', '<unknown task>')
                messages.append(f'{key}: {type(error).__name__}: {error}')
            failure = RuntimeError(
                f'{len(failures)} Dask shatter task(s) failed:\n'
                + '\n'.join(messages)
            )
            cause = failures[0][1]
            if isinstance(cause, BaseException):
                raise failure from cause
            raise failure
    else:
        processes = [delayed(do_one)(leaf, config, storage) for leaf in leaves]
        results = compute(*processes)

        joined_dfs = [df for df in results if df is not None]

    if joined_dfs:
        final_df = pd.concat(joined_dfs).sort_values(by=['xi', 'yi'])
        pc = write(final_df, storage, config.date)
        config.point_count = config.point_count + pc

        del final_df, joined_dfs
    else:
        config.point_count = 0

    end_time = int(datetime.now().timestamp()*1000)
    storage.consolidate(timestamp=(start_time, end_time))

    return config.point_count


def _raise_task_failures(failures: list[tuple[object, object]]) -> None:
    """Raise one useful error after all distributed task results are known."""
    messages = []
    for future, error in failures:
        key = getattr(future, 'key', '<unknown task>')
        messages.append(f'{key}: {type(error).__name__}: {error}')
    failure = RuntimeError(
        f'{len(failures)} Dask shatter task(s) failed:\n' + '\n'.join(messages)
    )
    cause = failures[0][1]
    if isinstance(cause, BaseException):
        raise failure from cause
    raise failure


def run_macro(
    macros: Leaves, config: ShatterConfig, storage: Storage
) -> int:
    """Run macro-v2 without collecting aggregate DataFrames on the driver.

    Each task writes one disjoint macro extent itself.  This avoids the
    scheduler memory and network pressure of ``pd.concat(joined_dfs)`` in the
    leaf-v1 executor.  A task must not be retried after an uncertain write;
    callers should resume from the saved MBR metadata instead.
    """
    start_time = int(datetime.now().timestamp() * 1000)
    driver_started = perf_counter()
    dc = get_client()
    failures = []
    task_results: list[MacroTaskResult] = []

    if dc is not None:
        futures = [
            dc.submit(
                do_macro,
                macro=macro,
                config=config,
                storage=storage,
                retries=0,
            )
            for macro in macros
        ]
        completed = as_completed(futures, with_results=True, raise_errors=False)
        for future, result in completed:
            if future.status == 'error':
                failures.append((future, result))
            else:
                task_results.append(result)
        if failures:
            _raise_task_failures(failures)
    else:
        for macro in macros:
            task_results.append(do_macro(macro, config, storage))

    point_count = sum(result.point_count for result in task_results)
    batch_timing = summarize_macro_timing(
        task_results, perf_counter() - driver_started
    )
    config.point_count += point_count
    end_time = int(datetime.now().timestamp() * 1000)
    consolidation_started = perf_counter()
    storage.consolidate(timestamp=(start_time, end_time))
    batch_timing['run_macro_consolidation_seconds'] = round(
        perf_counter() - consolidation_started, 6
    )
    config.execution_timing = merge_macro_timing(
        config.execution_timing, batch_timing
    )
    return config.point_count


def make_stage_writer(
    source_storage: Storage, config: ShatterConfig
) -> MacroStageWriter:
    """Create the single local stage owner, optionally pinned to one worker."""
    stage_config = copy.deepcopy(source_storage.config)
    stage_config.tdb_dir = config.stage_tdb_dir
    dc = get_client()
    if dc is None:
        return MacroStageWriter(stage_config, config)

    submit_options = {'actor': True}
    if config.stage_worker_address:
        submit_options['workers'] = [config.stage_worker_address]
        submit_options['allow_other_workers'] = False
    future = dc.submit(
        MacroStageWriter,
        stage_config,
        config,
        **submit_options,
    )
    return future.result()


def run_macro_to_stage(
    macros: Leaves,
    config: ShatterConfig,
    storage: Storage,
    data: Data,
) -> int:
    """Run macro-v3 compact spatial blocks and publish committed shards."""
    driver_started = perf_counter()
    dc = get_client()
    failures = []
    task_results: list[MacroTaskResult] = []
    macro_blocks, planner = plan_macro_blocks(
        list(macros), config, storage, data
    )
    if dc is not None:
        if not dc.scheduler_info()['workers']:
            raise RuntimeError('macro-v3 requires at least one Dask worker')
        estimated_points = {
            window['name']: window['estimated_max_points']
            for window in planner.get('windows', [])
        }
        planned_blocks = sorted(
            macro_blocks,
            key=lambda block: estimated_points.get(_shard_name(block), 0),
            reverse=True,
        )
        # A block owns a local TileDB stage, so it must perform its complete
        # process -> consolidate -> publish lifecycle on one worker.  Do not
        # split it into partial tasks then pin a finalizer to a concrete Dask
        # worker address: a nanny restart changes that address and leaves the
        # finalizer permanently unrunnable.  One independent block per task
        # retains Dask work stealing and permits safe recovery on another
        # worker, while stage paths remain local to the executing task.
        futures = [
            dc.submit(
                do_macro_to_shard,
                macros=block,
                config=config,
                storage=storage,
                retries=0,
            )
            for block in planned_blocks
        ]
        planner['distributed_executor'] = {
            'mode': 'one-local-stage-per-spatial-block',
            'worker_address_restrictions': False,
            'block_task_count': len(futures),
        }
        completed = as_completed(futures, with_results=True, raise_errors=False)
        for future, result in completed:
            if future.status == 'error':
                failures.append((future, result))
            else:
                task_results.append(result)
        if failures:
            _raise_task_failures(failures)
    else:
        for block in macro_blocks:
            task_results.append(do_macro_to_shard(block, config, storage))

    config.point_count += sum(result.point_count for result in task_results)
    published = [result for result in task_results if result.shard_uri]
    if published:
        with tiledb.Group(config.stage_publish_uri, 'w') as group:
            for result in published:
                name = result.shard_uri.rsplit('/', 1)[-1]
                group.add(result.shard_uri, name=name)
    timing = summarize_macro_timing(
        task_results,
        perf_counter() - driver_started,
        strategy='macro-v3-stage-push',
    )
    planner['actual_shards'] = sorted(
        [
            {
                'uri': result.shard_uri,
                'macro_count': result.macro_count,
                'point_count': result.point_count,
                'published_bytes': result.published_bytes,
            }
            for result in published
        ],
        key=lambda shard: shard['uri'],
    )
    timing['planner'] = planner
    config.execution_timing = merge_macro_timing(
        config.execution_timing,
        timing,
    )
    return config.point_count


def run_macro_to_single_array(
    macros: Leaves,
    config: ShatterConfig,
    storage: Storage,
    data: Data,
) -> int:
    """Write macro-v4 blocks directly into one pre-created TileDB array.

    This initial proof path has no external shard catalog in its read model:
    spatial blocks create internal fragments of ``config.tdb_dir`` and are
    consolidated after every worker completes. The destination must be empty
    because macro-v4 has not yet implemented an idempotent recovery ledger.
    """
    driver_started = perf_counter()
    existing_fragments = tiledb.array_fragments(config.tdb_dir)
    if existing_fragments:
        raise RuntimeError(
            'macro-v4-single-array requires an empty destination array; '
            'create a new immutable release URI for each run.'
        )

    dc = get_client()
    failures = []
    task_results: list[MacroTaskResult] = []
    macro_blocks, planner = plan_macro_blocks(
        list(macros), config, storage, data
    )
    if dc is not None:
        if not dc.scheduler_info()['workers']:
            raise RuntimeError('macro-v4 requires at least one Dask worker')
        estimated_points = {
            window['name']: window['estimated_max_points']
            for window in planner.get('windows', [])
        }
        planned_blocks = sorted(
            macro_blocks,
            key=lambda block: estimated_points.get(_shard_name(block), 0),
            reverse=True,
        )
        futures = [
            dc.submit(
                do_macro_to_single_array,
                macros=block,
                config=config,
                storage=storage,
                retries=0,
            )
            for block in planned_blocks
        ]
        planner['distributed_executor'] = {
            'mode': 'disjoint-blocks-write-one-array',
            'worker_address_restrictions': False,
            'block_task_count': len(futures),
            'retries': 0,
        }
        completed = as_completed(futures, with_results=True, raise_errors=False)
        for future, result in completed:
            if future.status == 'error':
                failures.append((future, result))
            else:
                task_results.append(result)
        if failures:
            _raise_task_failures(failures)
    else:
        for block in macro_blocks:
            task_results.append(do_macro_to_single_array(block, config, storage))

    config.point_count += sum(result.point_count for result in task_results)
    fragments_before = storage.stage_fragment_summary()
    consolidation_started = perf_counter()
    storage.consolidate_canonical_array(config.stage_fragment_size_mb)
    storage.vacuum()
    for mode in ['fragment_meta', 'commits', 'array_meta']:
        storage.consolidate(mode)
        storage.vacuum(mode)
    fragments_after = storage.stage_fragment_summary()

    timing = summarize_macro_timing(
        task_results,
        perf_counter() - driver_started,
        strategy='macro-v4-single-array',
    )
    timing['planner'] = planner
    timing['array'] = {
        'fragment_target_mb': config.stage_fragment_size_mb,
        'fragment_count_before': len(fragments_before),
        'fragment_bytes_before': sum(
            fragment['bytes'] for fragment in fragments_before
        ),
        'consolidation_count': max(
            len(fragments_before) - len(fragments_after), 0
        ),
        'fragment_count_after': len(fragments_after),
        'fragment_bytes_after': sum(
            fragment['bytes'] for fragment in fragments_after
        ),
        'consolidation_seconds': round(
            perf_counter() - consolidation_started, 6
        ),
    }
    config.execution_timing = merge_macro_timing(
        config.execution_timing,
        timing,
    )
    return config.point_count


def _stage_failure_kind(error: object) -> str:
    """Classify a final stage-task failure without trusting worker-local state.

    A Dask nanny can kill the entire worker process, so the Python task often
    cannot raise ``MemoryError`` itself.  Dask reports that condition as a
    ``KilledWorker`` failure after its configured retries.  Batch may also
    kill a valid but oversized work unit at its time limit. Splitting either
    block is safe because it has no durable stage receipt yet; ordinary
    application errors remain resumable but are not silently subdivided.
    """
    error_type = type(error).__name__
    text = f'{error_type}: {error}'.lower()
    if isinstance(error, TimeoutError) or 'attempt duration exceeded timeout' in text:
        return 'time_limit'
    if isinstance(error, MemoryError) or 'memory' in text or 'nanny' in text:
        return 'memory_pressure'
    if 'killedworker' in text or 'worker died' in text:
        return 'worker_lost_after_retries'
    return 'task_error'


def _ledger_block_details(
    macros: list[Extents], *, parent_id: str | None, split_depth: int
) -> dict:
    """Return the durable reconstruction data for one adaptive work unit."""
    return {
        'bounds': _block_bounds(macros).get(),
        'macro_count': len(macros),
        'macro_bounds': _block_descriptor(macros),
        'parent_id': parent_id,
        'split_depth': split_depth,
    }


def _active_ledger_blocks(
    root_blocks: dict[str, list[Extents]],
    ledger: BuildLedger,
    storage: Storage,
) -> dict[str, tuple[list[Extents], int]]:
    """Resolve the active leaves of the append-only adaptive plan tree."""
    active: dict[str, tuple[list[Extents], int]] = {}
    pending = [
        (block_id, macros, 0)
        for block_id, macros in sorted(root_blocks.items())
    ]
    while pending:
        block_id, macros, depth = pending.pop()
        record = ledger.state(block_id)
        if record is None or record.state != 'split':
            if record is not None:
                depth = int(record.details.get('split_depth', depth))
            active[block_id] = (macros, depth)
            continue

        children = record.details.get('children')
        if not children:
            raise RuntimeError(
                f'Adaptive split receipt for {block_id!r} has no children.'
            )
        for child in children:
            child_id = child['block_id']
            descriptor = child.get('macro_bounds')
            if descriptor is None:
                child_record = ledger.state(child_id)
                descriptor = (
                    child_record.details.get('macro_bounds')
                    if child_record is not None
                    else None
                )
            if descriptor is None:
                raise RuntimeError(
                    f'Adaptive split child {child_id!r} has no macro bounds.'
                )
            pending.append(
                (
                    child_id,
                    _block_from_descriptor(descriptor, storage),
                    int(child.get('split_depth', depth + 1)),
                )
            )
    return active


def _split_failed_stage_block(
    block_id: str,
    macros: list[Extents],
    split_depth: int,
    error: object,
    config: ShatterConfig,
    ledger: BuildLedger,
) -> list[tuple[str, list[Extents], int]]:
    """Record a failed stage and replace a resource-limited parent by children.

    Only an un-staged block can reach this function.  Its children therefore
    cover new, disjoint canonical ranges and use new immutable stage prefixes;
    no successful read, stage, or canonical write is discarded or duplicated.
    """
    kind = _stage_failure_kind(error)
    ledger.append(
        block_id,
        'failed',
        failure_kind=kind,
        error_type=type(error).__name__,
        error=str(error)[:2000],
        split_depth=split_depth,
    )
    if kind not in {'memory_pressure', 'worker_lost_after_retries', 'time_limit'}:
        return []
    if split_depth >= config.build_max_split_depth:
        return []
    children = split_macro_block_once(macros, config)
    if not children:
        return []

    child_records = []
    for child in children:
        child_id = _shard_name(child)
        if child_id == block_id:
            raise RuntimeError(
                f'Adaptive split of {block_id!r} did not reduce its bounds.'
            )
        child_records.append(
            {
                'block_id': child_id,
                'macro_bounds': _block_descriptor(child),
                'bounds': _block_bounds(child).get(),
                'split_depth': split_depth + 1,
            }
        )
    if len({child['block_id'] for child in child_records}) != len(child_records):
        raise RuntimeError(f'Adaptive split of {block_id!r} has duplicate children.')

    ledger.append(
        block_id,
        'split',
        failure_kind=kind,
        split_depth=split_depth,
        children=child_records,
    )
    for child, child_record in zip(children, child_records):
        ledger.append(
            child_record['block_id'],
            'planned',
            **_ledger_block_details(
                child,
                parent_id=block_id,
                split_depth=split_depth + 1,
            ),
        )
    return [
        (child['block_id'], macros, split_depth + 1)
        for child, macros in zip(child_records, children)
    ]


def run_macro_staged_publish(
    macros: Leaves,
    config: ShatterConfig,
    storage: Storage,
    data: Data,
) -> int:
    """Build one canonical array through durable stages and bounded writers.

    Compute tasks can scale horizontally because they write immutable stages at
    independent object-store prefixes.  The only operations against the shared
    canonical TileDB array run through a small publisher pool.  The ledger is
    the source of truth for resume; it is intentionally independent of Dask's
    transient task graph and of TileDB's physical fragment inventory.
    """
    driver_started = perf_counter()
    macro_blocks, planner = plan_macro_blocks(
        list(macros), config, storage, data
    )
    root_blocks = {_shard_name(block): block for block in macro_blocks}
    if len(root_blocks) != len(macro_blocks):
        raise RuntimeError('Macro-v4 planner produced non-unique block IDs.')
    root_block_ids = sorted(root_blocks)
    ledger = BuildLedger(config.build_ledger_uri, Storage.get_tdb_context(storage))
    # Identity binds the immutable root plan.  Runtime resource failures may
    # add ledger children below those roots without making a valid recovery
    # look like a foreign build.
    build_signature = _build_signature(config, storage, root_block_ids)
    build_inputs = _build_inputs(config, storage, root_block_ids)
    original_manifest = ledger.build_manifest()
    resumed_build = ledger.assert_build_identity(
        build_id=str(config.name),
        canonical_uri=config.tdb_dir,
        build_signature=build_signature,
        build_inputs=build_inputs,
    )
    if not resumed_build:
        ledger.append(
            '__build__',
            'build_started',
            build_id=str(config.name),
            canonical_uri=config.tdb_dir,
            planned_block_count=len(root_block_ids),
            schema_sha256=_block_schema_hash(storage),
            build_signature=build_signature,
            build_inputs=build_inputs,
            time_slot=config.time_slot,
        )
    else:
        original_time_slot = original_manifest.details.get('time_slot')
        if original_time_slot is None:
            raise ValueError(
                'The existing build ledger predates time-slot recovery and '
                'cannot safely resume this canonical-array build.'
            )
        config.time_slot = int(original_time_slot)
    # Final canonical writes and consolidation share a conservative VFS
    # budget. The independent stage writers use their own budget above.
    storage.set_context_overrides(
        **{'vfs.s3.max_parallel_ops': config.build_publish_vfs_parallel_ops}
    )
    # The original time slot is now bound (including on a resumed driver), so
    # worker-stage metadata and canonical history describe one logical build.
    storage.save_shatter_meta(config)

    # Persist enough information to reconstruct every root on a later driver.
    # New adaptive children carry equivalent descriptors in their own planned
    # receipts and in the parent's immutable split receipt.
    for block_id in root_block_ids:
        if ledger.state(block_id) is None:
            ledger.append(
                block_id,
                'planned',
                **_ledger_block_details(
                    root_blocks[block_id], parent_id=None, split_depth=0
                ),
            )

    staged_results: list[MacroTaskResult] = []
    published_results: list[MacroTaskResult] = []
    stage_candidates: list[tuple[str, list[Extents], int]] = []
    active_blocks = _active_ledger_blocks(root_blocks, ledger, storage)
    for block_id, (block, split_depth) in sorted(active_blocks.items()):
        record = ledger.state(block_id)
        if record is not None and record.state == 'published':
            published_results.append(_result_from_details(record.details))
        elif record is not None and record.state == 'staged':
            resumed = _result_from_details(
                record.details, stage_uri=record.details.get('stage_uri')
            )
            resumed.block_id = block_id
            staged_results.append(resumed)
        else:
            stage_candidates.append((block_id, block, split_depth))

    failures = []
    adaptive_split_count = 0
    adaptive_splits = []
    dc = get_client()

    if dc is not None:
        workers = dc.scheduler_info()['workers']
        if not workers:
            raise RuntimeError(
                'macro-v4-staged-publish requires at least one Dask worker'
            )
        # Keep stages and canonical publishes on one event stream.  The
        # previous executor synchronously waited on a publisher whenever its
        # small pool was full.  A slow canonical write then blocked stage
        # receipt handling and deferred split children until the entire stage
        # generation had drained.
        completion_stream = as_completed(
            [], with_results=True, raise_errors=False
        )
        active_stages = {}
        active_publishes = {}
        pending_publishes = deque()
        stage_candidates = deque(stage_candidates)
        # A stage holds a full PDAL point view and a TileDB write buffer.  It
        # is memory-bound, unlike the lightweight driver/publisher work, and
        # must never share a Dask worker *process* with another stage.  The
        # EC2 fleet gives every worker process one named resource token.  A
        # generic user-supplied cluster may not expose it, so retain a
        # conservative one-submission-per-worker fallback rather than using
        # the sum of worker threads (which can oversubscribe memory 4x).
        stage_resource_name = 'silvimetric_stage'
        stage_resource_capacity = sum(
            worker.get('resources', {}).get(stage_resource_name, 0)
            for worker in workers.values()
        )
        stage_resources = (
            {stage_resource_name: 1}
            # Dedicated publisher processes intentionally do not advertise a
            # stage token.  Testing this capacity against *all* workers would
            # therefore disable the stage restriction in a split fleet and
            # let memory-heavy PDAL work land on a publisher.  A positive
            # named-resource capacity is the complete signal that the
            # cluster supports isolated stage placement.
            if stage_resource_capacity > 0
            else None
        )
        stage_inflight_limit = max(
            1,
            int(stage_resource_capacity)
            if stage_resources is not None
            else len(workers),
        )
        # Canonical publication should be able to live on a small, durable
        # worker pool after the PDAL-heavy stage fleet is retired.  A generic
        # local Dask cluster has no such resource, so preserve the historical
        # scheduling fallback for tests and user-managed clusters.
        publisher_resource_name = 'silvimetric_publisher'
        publisher_resource_capacity = sum(
            worker.get('resources', {}).get(publisher_resource_name, 0)
            for worker in workers.values()
        )
        if (
            publisher_resource_capacity
            and publisher_resource_capacity < config.build_publish_concurrency
        ):
            raise RuntimeError(
                'macro-v4 dedicated publisher capacity is smaller than '
                'build_publish_concurrency: '
                f'{publisher_resource_capacity} < '
                f'{config.build_publish_concurrency}'
            )
        publisher_resources = (
            {publisher_resource_name: 1}
            if publisher_resource_capacity >= config.build_publish_concurrency
            else None
        )
        max_pending_publish_blocks = 0
        stage_task_count = 0
        resumed_staged_block_count = len(staged_results)
        pending_publishes.extend(result.block_id for result in staged_results)
        stage_tasks_complete_recorded = False

        def record_stage_tasks_complete() -> None:
            """Emit a durable handoff once no memory-heavy work remains.

            The record is deliberately separate from ``build_published``:
            independent publishers may still be reading durable stages and
            writing the canonical array.  An external fleet controller can
            safely retire workers that advertise only ``silvimetric_stage``
            at this point without disturbing those publishers.
            """
            nonlocal stage_tasks_complete_recorded
            if stage_tasks_complete_recorded:
                return
            if stage_candidates or active_stages:
                return
            ledger.append(
                '__build__',
                'build_stage_tasks_complete',
                stage_task_count=stage_task_count,
                staged_block_count=len(staged_results),
                pending_publish_block_count=(
                    len(pending_publishes) + len(active_publishes)
                ),
            )
            stage_tasks_complete_recorded = True

        def schedule_ready_work() -> None:
            """Keep stage and canonical-publish work independently bounded."""
            nonlocal stage_task_count, max_pending_publish_blocks
            while (
                stage_candidates
                and len(active_stages) < stage_inflight_limit
            ):
                block_id, block, split_depth = stage_candidates.popleft()
                future = dc.submit(
                    stage_macro_block,
                    macros=block,
                    config=config,
                    storage=storage,
                    ledger_uri=config.build_ledger_uri,
                    retries=config.build_stage_retries,
                    resources=stage_resources,
                )
                active_stages[future] = (block_id, block, split_depth)
                completion_stream.add(future)
                stage_task_count += 1
            while (
                pending_publishes
                and len(active_publishes) < config.build_publish_concurrency
            ):
                block_id = pending_publishes.popleft()
                future = dc.submit(
                    publish_staged_macro_block,
                    block_id=block_id,
                    config=config,
                    ledger_uri=config.build_ledger_uri,
                    retries=0,
                    resources=publisher_resources,
                )
                active_publishes[future] = block_id
                completion_stream.add(future)
            max_pending_publish_blocks = max(
                max_pending_publish_blocks, len(pending_publishes)
            )

        # A failed memory-bound stage yields children as soon as its future
        # resolves.  They can be scheduled while unrelated roots continue,
        # instead of waiting for every root in a breadth-first generation.
        while (
            stage_candidates
            or active_stages
            or pending_publishes
            or active_publishes
        ):
            schedule_ready_work()
            record_stage_tasks_complete()
            future, result = next(completion_stream)
            stage = active_stages.pop(future, None)
            if stage is not None:
                block_id, block, split_depth = stage
                if future.status != 'finished':
                    children = _split_failed_stage_block(
                        block_id,
                        block,
                        split_depth,
                        result,
                        config,
                        ledger,
                    )
                    if children:
                        adaptive_split_count += 1
                        split_receipt = ledger.state(block_id)
                        adaptive_splits.append(
                            {
                                'parent_id': block_id,
                                'failure_kind': split_receipt.details[
                                    'failure_kind'
                                ],
                                'split_depth': split_depth,
                                'child_ids': [
                                    child_id for child_id, _, _ in children
                                ],
                            }
                        )
                        # A memory split is a corrective continuation of the
                        # failed work unit, not another breadth-first root.
                        # Put it ahead of untouched roots so recovery starts
                        # at the next free stage slot.
                        stage_candidates.extendleft(reversed(children))
                    else:
                        failures.append((future, result))
                else:
                    staged_results.append(result)
                    pending_publishes.append(result.block_id)
                continue

            active_publishes.pop(future)
            if future.status != 'finished':
                failures.append((future, result))
            else:
                published_results.append(result)
        record_stage_tasks_complete()
        planner['distributed_executor'] = {
            'mode': 'durable-stages-bounded-canonical-publishers',
            'stage_task_count': stage_task_count,
            'resume_staged_block_count': resumed_staged_block_count,
            'publisher_concurrency': config.build_publish_concurrency,
            'stage_inflight_limit': stage_inflight_limit,
            'stage_resource': stage_resource_name if stage_resources else None,
            'stage_resource_capacity': stage_resource_capacity,
            'publisher_resource': (
                publisher_resource_name if publisher_resources else None
            ),
            'publisher_resource_capacity': publisher_resource_capacity,
            'max_pending_publish_blocks': max_pending_publish_blocks,
            'publisher_vfs_parallel_ops': config.build_publish_vfs_parallel_ops,
            'stage_vfs_parallel_ops': config.build_stage_vfs_parallel_ops,
            'stage_retries': config.build_stage_retries,
            'publish_retries': 0,
            'adaptive_split_count': adaptive_split_count,
            'adaptive_splits': adaptive_splits,
        }
    else:
        stage_task_count = 0
        resumed_staged_block_count = len(staged_results)
        while stage_candidates:
            pending_candidates = stage_candidates
            stage_candidates = []
            for block_id, block, split_depth in pending_candidates:
                stage_task_count += 1
                try:
                    staged_results.append(
                        stage_macro_block(
                            block, config, storage, config.build_ledger_uri
                        )
                    )
                except Exception as error:
                    children = _split_failed_stage_block(
                        block_id,
                        block,
                        split_depth,
                        error,
                        config,
                        ledger,
                    )
                    if children:
                        adaptive_split_count += 1
                        split_receipt = ledger.state(block_id)
                        adaptive_splits.append(
                            {
                                'parent_id': block_id,
                                'failure_kind': split_receipt.details[
                                    'failure_kind'
                                ],
                                'split_depth': split_depth,
                                'child_ids': [
                                    child_id for child_id, _, _ in children
                                ],
                            }
                        )
                        stage_candidates.extend(children)
                    else:
                        failures.append((None, error))
        for result in staged_results:
            try:
                published_results.append(
                    publish_staged_macro_block(
                        result.block_id, config, config.build_ledger_uri
                    )
                )
            except Exception as error:
                failures.append((None, error))
        planner['distributed_executor'] = {
            'mode': 'local-durable-stages-bounded-canonical-publishers',
            'stage_task_count': stage_task_count,
            'resume_staged_block_count': resumed_staged_block_count,
            'publisher_concurrency': 1,
            'publisher_vfs_parallel_ops': config.build_publish_vfs_parallel_ops,
            'stage_vfs_parallel_ops': config.build_stage_vfs_parallel_ops,
            'stage_retries': 0,
            'publish_retries': 0,
            'adaptive_split_count': adaptive_split_count,
            'adaptive_splits': adaptive_splits,
        }

    # Each block has at most one stable published receipt.  Compute the total
    # from ledger state so a resumed driver cannot double-count prior work.
    active_blocks = _active_ledger_blocks(root_blocks, ledger, storage)
    active_block_ids = sorted(active_blocks)
    summary = ledger.summary(active_block_ids)
    published_records = [
        record
        for record in summary['records'].values()
        if record is not None and record.state == 'published'
    ]
    config.point_count = sum(
        int(record.details.get('point_count', 0))
        for record in published_records
    )
    timing = summarize_macro_timing(
        published_results,
        perf_counter() - driver_started,
        strategy='macro-v4-staged-publish',
    )
    timing['planner'] = planner
    timing['build_ledger'] = {
        'uri': config.build_ledger_uri,
        'build_id': str(config.name),
        'build_signature': build_signature,
        'states': summary['states'],
        'published_block_count': len(published_records),
        'planned_block_count': len(active_block_ids),
        'root_block_count': len(root_block_ids),
        'adaptive_split_count': adaptive_split_count,
        'partial': bool(failures)
        or len(published_records) != len(active_block_ids),
    }
    config.execution_timing = timing

    if failures or len(published_records) != len(active_block_ids):
        ledger.append(
            '__build__',
            'build_partial',
            build_id=str(config.name),
            published_block_count=len(published_records),
            planned_block_count=len(active_block_ids),
            root_block_count=len(root_block_ids),
            failed_task_count=len(failures),
        )
        if failures:
            _raise_task_failures(failures)
        raise RuntimeError(
            'Macro-v4 staged build is incomplete; resume with the same '
            'build name and ledger URI.'
        )

    # This is the durable handoff between the horizontally scalable build
    # phase and array maintenance. It is recorded only after every active leaf
    # has a published receipt, so a scheduler-only finalizer never has to
    # rediscover or execute point-cloud work.
    published_build = {
        'build_id': str(config.name),
        'planned_block_count': len(active_block_ids),
        'root_block_count': len(root_block_ids),
        'published_block_count': len(published_records),
        'point_count': config.point_count,
    }
    prior_build_state = ledger.state('__build__')
    if prior_build_state is None or prior_build_state.state != 'build_sealed':
        ledger.append('__build__', 'build_published', **published_build)

    if config.defer_build_finalization:
        timing['array'] = {
            'fragment_target_mb': config.stage_fragment_size_mb,
            'finalization_deferred': True,
        }
        config.execution_timing = timing
        return config.point_count

    config.execution_timing = timing
    return _seal_macro_v4_staged_build(config, storage, ledger)


def _latest_build_receipt(ledger: BuildLedger, state: str):
    """Return the newest receipt for one build-level transition."""
    receipts = [
        receipt
        for receipt in ledger.records('__build__')
        if receipt.state == state
    ]
    return receipts[-1] if receipts else None


def _seal_macro_v4_staged_build(
    config: ShatterConfig, storage: Storage, ledger: BuildLedger,
    *, consolidate: bool = True,
) -> int:
    """Seal a fully published build, optionally deferring global maintenance.

    This function deliberately has no ``Data`` or Dask dependency. It is safe
    to run after the point-processing workers have gone away, and an
    interrupted maintenance attempt re-enters from the immutable published
    receipt rather than physical fragment names.
    """
    prior_build_state = ledger.state('__build__')
    if prior_build_state is not None and prior_build_state.state == 'build_sealed':
        config.point_count = int(
            prior_build_state.details.get('point_count', config.point_count)
        )
        config.execution_timing['array'] = {
            'fragment_target_mb': config.stage_fragment_size_mb,
            'fragment_count_after': prior_build_state.details.get(
                'fragment_count'
            ),
            'fragment_bytes_after': prior_build_state.details.get(
                'fragment_bytes'
            ),
            'consolidation_skipped': True,
            'finalization_phases_seconds': prior_build_state.details.get(
                'finalization_phases_seconds', {}
            ),
        }
        return config.point_count

    published = _latest_build_receipt(ledger, 'build_published')
    if published is None:
        raise RuntimeError(
            'Cannot finalize this macro-v4 build because it has no durable '
            'build_published receipt. Resume the build phase first.'
        )
    config.point_count = int(published.details['point_count'])
    phases: dict[str, float] = {}

    def timed(phase: str, operation):
        started = perf_counter()
        try:
            return operation()
        finally:
            # Record even a failed operation so an interrupted seal can be
            # diagnosed without treating the attempt as a successful seal.
            phases[phase] = round(perf_counter() - started, 6)

    finalization_started = perf_counter()
    fragments_before = timed(
        'fragment_summary_before', storage.stage_fragment_summary
    )
    ledger.append(
        '__build__',
        'build_consolidating' if consolidate else 'build_sealing',
        build_id=str(config.name),
        fragment_count=len(fragments_before),
        published_block_count=published.details['published_block_count'],
    )
    try:
        if consolidate:
            timed(
                'fragment_consolidate',
                lambda: storage.consolidate_canonical_array(
                    config.stage_fragment_size_mb
                ),
            )
            timed('fragment_vacuum', storage.vacuum)
            for mode in ['fragment_meta', 'commits', 'array_meta']:
                timed(f'{mode}_consolidate', lambda mode=mode: storage.consolidate(mode))
                timed(f'{mode}_vacuum', lambda mode=mode: storage.vacuum(mode))
        fragments_after = timed(
            'fragment_summary_after', storage.stage_fragment_summary
        )
        root_blocks = timed(
            'ledger_root_blocks', lambda: _macro_v4_root_blocks(ledger, storage)
        )
        canonical_count = timed(
            'canonical_count_scan',
            lambda: _canonical_point_count(
                storage, [macro for block in root_blocks.values() for macro in block]
            ),
        )
        if canonical_count != config.point_count:
            count_label = (
                'post-consolidation canonical' if consolidate else 'canonical'
            )
            raise RuntimeError(
                f'Cannot seal macro-v4 build: the {count_label} '
                f'count is {canonical_count}, but published receipts total '
                f'{config.point_count}.'
            )
    except BaseException as error:
        # The earlier build_published receipt remains the source of truth for
        # a retry; this marker only identifies the failed phase for operators.
        try:
            ledger.append(
                '__build__',
                'build_partial',
                build_id=str(config.name),
                phase='finalization',
                finalization_phases_seconds=phases,
                reason=type(error).__name__,
                message=str(error),
            )
        except Exception:
            config.log.exception(
                'Unable to append macro-v4 finalization failure receipt'
            )
        raise

    seal = {
        'build_id': str(config.name),
        'build_state': 'sealed',
        'planned_block_count': published.details['planned_block_count'],
        'root_block_count': published.details['root_block_count'],
        'published_block_count': published.details['published_block_count'],
        'point_count': config.point_count,
        'canonical_point_count': canonical_count,
        'consolidated': consolidate,
        'fragment_count': len(fragments_after),
        'fragment_bytes': sum(fragment['bytes'] for fragment in fragments_after),
        'finalization_phases_seconds': phases,
    }
    timed('seal_receipt', lambda: ledger.append('__build__', 'build_sealed', **seal))
    timed(
        'seal_metadata',
        lambda: storage.save_metadata(
            f'macro_v4_build_{config.name}', json.dumps(seal, sort_keys=True)
        ),
    )
    config.execution_timing['array'] = {
        'fragment_target_mb': config.stage_fragment_size_mb,
        'fragment_count_before': len(fragments_before),
        'fragment_count_after': len(fragments_after),
        'fragment_bytes_before': sum(
            fragment['bytes'] for fragment in fragments_before
        ),
        'fragment_bytes_after': sum(
            fragment['bytes'] for fragment in fragments_after
        ),
        'consolidation_deferred': not consolidate,
        'consolidation_seconds': round(
            perf_counter() - finalization_started, 6
        ),
        'finalization_phases_seconds': phases,
    }
    return config.point_count


def finalize_macro_v4_staged_build(
    tdb_dir: str, build_ledger_uri: str, build_id: str | uuid.UUID,
    *, consolidate: bool = True,
) -> ShatterConfig:
    """Seal a published macro-v4 build without a Dask or PDAL worker fleet.

    Storage history and the immutable ledger manifest bind this operation to
    the original shatter configuration. The finalizer therefore does not
    reopen the source EPT resource or re-plan work units.
    """
    finalization_started = perf_counter()
    storage = Storage.from_db(tdb_dir)
    ledger = BuildLedger(build_ledger_uri, Storage.get_tdb_context(storage))
    manifest = ledger.build_manifest()
    if manifest is None:
        raise ValueError(
            f'No macro-v4 build manifest exists at {build_ledger_uri!r}.'
        )
    expected_id = str(build_id)
    if manifest.details.get('build_id') != expected_id:
        raise ValueError(
            'The requested build id does not match the ledger manifest: '
            f'{expected_id!r} != {manifest.details.get("build_id")!r}.'
        )
    if manifest.details.get('canonical_uri', '').rstrip('/') != tdb_dir.rstrip('/'):
        raise ValueError(
            'The requested canonical array does not match the ledger manifest.'
        )
    time_slot = manifest.details.get('time_slot')
    if time_slot is None:
        raise ValueError(
            'The build ledger predates scheduler-only finalization and has '
            'no recoverable history time slot.'
        )
    config = storage.get_shatter_meta(int(time_slot))
    if (
        config.processing_strategy != 'macro-v4-staged-publish'
        or str(config.name) != expected_id
        or config.build_ledger_uri != build_ledger_uri
    ):
        raise ValueError(
            'Canonical-array history does not match the requested macro-v4 '
            'build finalization.'
        )
    config.defer_build_finalization = False
    _seal_macro_v4_staged_build(config, storage, ledger, consolidate=consolidate)
    # Preserve the publish phase's ``shatter_total_seconds``. It measures
    # source processing; including intentional worker-drain time here would
    # make benchmark comparisons misleading.
    config.execution_timing['scheduler_only_finalization_seconds'] = round(
        perf_counter() - finalization_started, 6
    )
    final(config, storage, finished=True)
    return config


def consolidate_macro_v4_snapshot(
    snapshot_uri: str, build_ledger_uri: str, build_id: str | uuid.UUID,
    *, fragment_size_mb: int = 300,
) -> dict:
    """Benchmark maintenance on a verified copy, never the append base.

    The copied TileDB array keeps the original history metadata.  Its source
    ledger describes exactly which first-phase macro cores to count before
    and after consolidation.  A failure leaves the snapshot re-enterable;
    it does not change the source ledger or canonical array.
    """
    if fragment_size_mb <= 0:
        raise ValueError('fragment_size_mb must be positive')
    storage = Storage.from_db(snapshot_uri)
    ledger = BuildLedger(build_ledger_uri, Storage.get_tdb_context(storage))
    manifest = ledger.build_manifest()
    if manifest is None or manifest.details.get('build_id') != str(build_id):
        raise ValueError('Snapshot benchmark ledger/build identity mismatch')
    source_uri = manifest.details.get('canonical_uri', '').rstrip('/')
    if not source_uri or source_uri == snapshot_uri.rstrip('/'):
        raise ValueError('Snapshot benchmark requires a copy, not the source array')
    sealed = _latest_build_receipt(ledger, 'build_sealed')
    if sealed is None or sealed.details.get('build_id') != str(build_id):
        raise ValueError('Source build must be sealed before a snapshot benchmark')
    config = storage.get_shatter_meta(int(manifest.details['time_slot']))
    if str(config.name) != str(build_id) or not config.finished:
        raise ValueError('Snapshot history does not match the sealed source build')
    blocks = _macro_v4_root_blocks(ledger, storage)
    macros = [macro for group in blocks.values() for macro in group]
    expected = int(sealed.details['point_count'])
    before_count = _canonical_point_count(storage, macros)
    if before_count != expected:
        raise RuntimeError(
            f'Snapshot contains {before_count} points; expected {expected}'
        )
    before = storage.stage_fragment_summary()
    started = perf_counter()
    storage.consolidate_canonical_array(fragment_size_mb)
    storage.vacuum()
    for mode in ('fragment_meta', 'commits', 'array_meta'):
        storage.consolidate(mode)
        storage.vacuum(mode)
    elapsed = perf_counter() - started
    after_count = _canonical_point_count(storage, macros)
    if after_count != expected:
        raise RuntimeError(
            f'Snapshot changed point count: {before_count} -> {after_count}'
        )
    after = storage.stage_fragment_summary()
    return {
        'build_id': str(build_id),
        'source_uri': source_uri,
        'snapshot_uri': snapshot_uri,
        'point_count': expected,
        'fragment_count_before': len(before),
        'fragment_count_after': len(after),
        'fragment_bytes_before': sum(item['bytes'] for item in before),
        'fragment_bytes_after': sum(item['bytes'] for item in after),
        'consolidation_seconds': round(elapsed, 6),
    }


def consolidate_macro_v4_collection(
    tdb_dir: str,
    builds: list[tuple[str, str | uuid.UUID]],
    expected_bounds: Bounds,
    *, fragment_size_mb: int = 300,
) -> dict:
    """Verify a complete disjoint phase partition and maintain one array.

    ``expected_bounds`` is the grid-aligned outer pixel rectangle declared
    by the collection plan.  It may include empty source space, but it must
    be covered by the union of all sealed phase rectangles without holes.
    This operation can be retried after an interrupted TileDB consolidation.
    """
    if not builds or fragment_size_mb <= 0:
        raise ValueError('Need sealed builds and a positive fragment target')
    storage = Storage.from_db(tdb_dir)
    target = Extents.from_sub(storage, expected_bounds)
    listed = {str(build_id) for _, build_id in builds}
    if len(listed) != len(builds):
        raise ValueError('Duplicate build identity in collection plan')
    history = []
    for slot in range(1, storage.config.next_time_slot):
        try:
            history.append(storage.get_shatter_meta(slot))
        except KeyError:
            continue
    if {str(item.name) for item in history} != listed:
        raise ValueError('Canonical array history differs from collection plan')
    extents = []
    phases = []
    for ledger_uri, build_id in builds:
        build_id = str(build_id)
        ledger = BuildLedger(ledger_uri, Storage.get_tdb_context(storage))
        manifest = ledger.build_manifest()
        if (
            manifest is None
            or manifest.details.get('build_id') != build_id
            or manifest.details.get('canonical_uri', '').rstrip('/')
            != tdb_dir.rstrip('/')
        ):
            raise ValueError(f'Collection phase {build_id} ledger mismatch')
        receipt = ledger.state('__build__')
        if receipt is None or receipt.state != 'build_sealed':
            raise ValueError(f'Collection phase {build_id} is not sealed')
        config = storage.get_shatter_meta(int(manifest.details['time_slot']))
        if str(config.name) != build_id or not config.finished:
            raise ValueError(f'Collection phase {build_id} history mismatch')
        extent = Extents.from_sub(storage, config.bounds)
        if not (
            target.x1 <= extent.x1 < extent.x2 <= target.x2
            and target.y1 <= extent.y1 < extent.y2 <= target.y2
        ):
            raise ValueError(f'Collection phase {build_id} exceeds target grid')
        for other in extents:
            if not (
                extent.x2 <= other.x1 or other.x2 <= extent.x1
                or extent.y2 <= other.y1 or other.y2 <= extent.y1
            ):
                raise ValueError('Collection phases overlap in pixel space')
        extents.append(extent)
        blocks = _macro_v4_root_blocks(ledger, storage)
        macros = [macro for group in blocks.values() for macro in group]
        actual = _canonical_point_count(storage, macros)
        expected = int(receipt.details['point_count'])
        if actual != expected:
            raise RuntimeError(
                f'Collection phase {build_id} has {actual} points, '
                f'but its sealed receipt says {expected}'
            )
        phases.append({
            'build_id': build_id, 'ledger_uri': ledger_uri,
            'point_count': expected,
            'pixel_bounds': [extent.x1, extent.y1, extent.x2, extent.y2],
        })
    target_area = (target.x2 - target.x1) * (target.y2 - target.y1)
    covered_area = sum(
        (extent.x2 - extent.x1) * (extent.y2 - extent.y1)
        for extent in extents
    )
    if covered_area != target_area:
        raise ValueError(
            f'Collection partition covers {covered_area} of {target_area} pixels'
        )
    before = storage.stage_fragment_summary()
    started = perf_counter()
    storage.consolidate_canonical_array(fragment_size_mb)
    storage.vacuum()
    for mode in ('fragment_meta', 'commits', 'array_meta'):
        storage.consolidate(mode)
        storage.vacuum(mode)
    elapsed = perf_counter() - started
    after = storage.stage_fragment_summary()
    # Recheck every durable phase footprint after maintenance, not merely a
    # scalar total that could hide one gained and one lost cell.
    for phase in phases:
        ledger = BuildLedger(
            phase['ledger_uri'], Storage.get_tdb_context(storage)
        )
        blocks = _macro_v4_root_blocks(ledger, storage)
        actual = _canonical_point_count(
            storage, [macro for group in blocks.values() for macro in group]
        )
        if actual != phase['point_count']:
            raise RuntimeError(
                f"Post-maintenance point count changed for {phase['build_id']}"
            )
    return {
        'canonical_uri': tdb_dir,
        'point_count': sum(phase['point_count'] for phase in phases),
        'phase_count': len(phases),
        'target_pixel_bounds': [target.x1, target.y1, target.x2, target.y2],
        'phases': phases,
        'fragment_count_before': len(before),
        'fragment_count_after': len(after),
        'fragment_bytes_before': sum(item['bytes'] for item in before),
        'fragment_bytes_after': sum(item['bytes'] for item in after),
        'consolidation_seconds': round(elapsed, 6),
    }


def group_macro_blocks(
    macros: list[Extents], config: ShatterConfig, storage: Storage
) -> list[list[Extents]]:
    """Bin contiguous macro reads into compact, independently publishable blocks."""
    macro_side = math.isqrt(config.read_group_size)
    macro_width = macro_side * storage.config.resolution
    block_width = macro_width * config.stage_shard_side_macros
    root = storage.config.root
    blocks: dict[tuple[int, int], list[Extents]] = {}
    for macro in macros:
        key = (
            math.floor((macro.bounds.minx - root.minx) / block_width),
            math.floor((macro.bounds.miny - root.miny) / block_width),
        )
        blocks.setdefault(key, []).append(macro)
    return [
        sorted(
            block,
            key=lambda macro: (macro.bounds.miny, macro.bounds.minx),
        )
        for _, block in sorted(blocks.items())
    ]


def _block_bounds(macros: list[Extents]) -> Bounds:
    """Return the smallest axis-aligned bounds enclosing a macro block."""
    return Bounds(
        min(macro.bounds.minx for macro in macros),
        min(macro.bounds.miny for macro in macros),
        max(macro.bounds.maxx for macro in macros),
        max(macro.bounds.maxy for macro in macros),
    )


def _block_descriptor(macros: list[Extents]) -> list[list[float]]:
    """Serialize the exact, grid-aligned macro cores of one work unit."""
    return [macro.bounds.get() for macro in macros]


def _block_from_descriptor(
    descriptor: list[list[float]], storage: Storage
) -> list[Extents]:
    """Reconstruct a durable work-unit descriptor on a resumed driver."""
    return [
        Extents(
            Bounds(*bounds),
            storage.config.resolution,
            storage.config.alignment,
            storage.config.root,
        )
        for bounds in descriptor
    ]


def _adaptive_min_cells(config: ShatterConfig) -> int:
    """Return the smallest safe adaptive child side in output cells."""
    if config.build_min_cells_per_side is not None:
        return config.build_min_cells_per_side
    return max(math.isqrt(config.tile_size), 1)


def split_macro_block_once(
    macros: list[Extents], config: ShatterConfig
) -> list[list[Extents]]:
    """Divide one failed spatial work unit into deterministic grid children.

    A multi-macro work unit is bisected between existing macro cores.  A
    singleton macro is bisected along its longest cell dimension.  The latter
    is essential for dense EPT/COPC areas: a point cap is not an effective
    safety limit if the planner treats one macro as indivisible.
    """
    if len(macros) > 1:
        bounds = _block_bounds(macros)
        split_x = (bounds.maxx - bounds.minx) >= (bounds.maxy - bounds.miny)
        key = (
            (lambda macro: (macro.bounds.minx, macro.bounds.miny))
            if split_x
            else (lambda macro: (macro.bounds.miny, macro.bounds.minx))
        )
        ordered = sorted(macros, key=key)
        midpoint = len(ordered) // 2
        return [ordered[:midpoint], ordered[midpoint:]]

    macro = macros[0]
    min_side = _adaptive_min_cells(config)
    x_cells = macro.x2 - macro.x1
    y_cells = macro.y2 - macro.y1
    split_x = x_cells >= y_cells
    if split_x and x_cells < 2 * min_side:
        split_x = False
    if not split_x and y_cells < 2 * min_side:
        if x_cells < 2 * min_side:
            return []
        split_x = True

    minx, miny, maxx, maxy = macro.bounds.get()
    if split_x:
        offset_cells = x_cells // 2
        if offset_cells < min_side or x_cells - offset_cells < min_side:
            return []
        split = minx + offset_cells * macro.resolution
        bounds = [
            Bounds(minx, miny, split, maxy),
            Bounds(split, miny, maxx, maxy),
        ]
    else:
        offset_cells = y_cells // 2
        if offset_cells < min_side or y_cells - offset_cells < min_side:
            return []
        split = miny + offset_cells * macro.resolution
        bounds = [
            Bounds(minx, miny, maxx, split),
            Bounds(minx, split, maxx, maxy),
        ]
    return [
        [
            Extents(
                child,
                macro.resolution,
                macro.alignment,
                macro.root,
            )
        ]
        for child in bounds
    ]


def plan_macro_blocks(
    macros: list[Extents],
    config: ShatterConfig,
    storage: Storage,
    data: Data,
) -> tuple[list[list[Extents]], dict]:
    """Plan compact publish blocks from bounded, calibrated PDAL reads.

    A coarse COPC/EPT ``resolution`` query intentionally provides a cheap
    spatial sample.  That count alone cannot be converted to raw source-point
    count using the storage-resolution area ratio: it estimates output cells,
    not the number of source returns contained by those cells.  Before planning
    we therefore make a few small, bounded native-resolution reads and derive
    both a raw-points-per-coarse-sample multiplier and a conservative density
    ceiling.  The planner uses the greater of those two estimates and
    recursively bisects only windows above the configured raw-point cap.
    ``None`` retains the explicitly requested fixed-side fallback.
    """
    planning_started = perf_counter()

    def processing_grid_digest(blocks: list[list[Extents]]) -> str:
        # A publish block may contain several processing macros. It is the
        # macro bounds, not the enclosing block bounds, that control PDAL's
        # SMRF/HAG neighbourhood and therefore the resulting pixel values.
        cores = sorted(
            tuple(macro.bounds.get()) for block in blocks for macro in block
        )
        return hashlib.sha256(
            json.dumps(cores, separators=(',', ':')).encode('utf8')
        ).hexdigest()

    input_grid = processing_grid_digest([macros])
    target = config.stage_shard_target_points
    if target is None:
        blocks = group_macro_blocks(macros, config, storage)
        return blocks, {
            'method': 'fixed-macro-side',
            'stage_shard_side_macros': config.stage_shard_side_macros,
            'macro_count': len(macros),
            'planned_shard_count': len(blocks),
            'input_processing_grid_sha256': input_grid,
            'processing_grid_sha256': processing_grid_digest(blocks),
            'processing_macro_count': sum(map(len, blocks)),
            'processing_macro_split_count': 0,
            'planner_wall_seconds': round(perf_counter() - planning_started, 6),
        }

    base_resolution = storage.config.resolution
    coarse_resolution = (
        base_resolution * config.stage_planner_resolution_multiple
    )
    collar = config.processing_halo_m or 0.0
    estimates: dict[str, dict] = {}
    # A local observation belongs to the part of the AOI that contains it,
    # rather than becoming a (very pessimistic) source-wide density ceiling.
    # Descendant candidates inherit intersecting observations, so a dense
    # parent sample cannot disappear merely because a child's centre sample
    # happens to fall between flightlines.
    local_calibration_map: list[dict] = []
    query_timing = {
        'native_sample_calls': 0,
        'native_sample_seconds': 0.0,
        'coarse_query_calls': 0,
        'coarse_query_seconds': 0.0,
        'coarse_queries_skipped_by_density': 0,
    }

    def report_progress(phase: str) -> None:
        print(
            'SILVIMETRIC_PLANNER_PROGRESS '
            + json.dumps(
                {
                    'phase': phase,
                    'elapsed_seconds': round(perf_counter() - planning_started, 3),
                    'candidate_count': len(estimates),
                    'native_sample_calls': query_timing['native_sample_calls'],
                    'coarse_query_calls': query_timing['coarse_query_calls'],
                },
                sort_keys=True,
            ),
            flush=True,
        )

    def native_count(bounds: Bounds) -> int:
        started = perf_counter()
        try:
            return int(data.count(bounds))
        finally:
            query_timing['native_sample_calls'] += 1
            query_timing['native_sample_seconds'] += perf_counter() - started

    def coarse_count(bounds: Bounds) -> int:
        started = perf_counter()
        try:
            return int(
                data.estimate_count(
                    bounds, reader_resolution=coarse_resolution
                )
            )
        finally:
            query_timing['coarse_query_calls'] += 1
            query_timing['coarse_query_seconds'] += perf_counter() - started

    plan_bounds = _block_bounds(macros)
    def calibrate_density(
        bounds: Bounds, sample_count: int, window_m: float
    ) -> list[dict]:
        """Return bounded native/coarse density observations for ``bounds``.

        The previous planner applied four observations from the whole input to
        every macro.  That misses localized overlap and dense flightlines: a
        low-density AOI can look safe while one macro requires several worker
        deaths before recovery splits it.  Sampling each candidate is a small
        bounded read compared with staging the entire candidate and makes the
        point cap a local, rather than global, contract.
        """
        sample_side = min(
            window_m,
            bounds.maxx - bounds.minx,
            bounds.maxy - bounds.miny,
        )
        sample_grid_side = math.ceil(math.sqrt(sample_count))
        samples = []
        for index in range(sample_count):
            x_index = index % sample_grid_side
            y_index = index // sample_grid_side
            center_x = bounds.minx + (
                (x_index + 0.5) / sample_grid_side
            ) * (bounds.maxx - bounds.minx)
            center_y = bounds.miny + (
                (y_index + 0.5) / sample_grid_side
            ) * (bounds.maxy - bounds.miny)
            sample_bounds = Bounds(
                max(bounds.minx, center_x - sample_side / 2),
                max(bounds.miny, center_y - sample_side / 2),
                min(bounds.maxx, center_x + sample_side / 2),
                min(bounds.maxy, center_y + sample_side / 2),
            )
            sample_area = (sample_bounds.maxx - sample_bounds.minx) * (
                sample_bounds.maxy - sample_bounds.miny
            )
            raw_points = native_count(sample_bounds)
            coarse_points = coarse_count(sample_bounds)
            samples.append(
                {
                    'bounds': sample_bounds.get(),
                    'area_m2': sample_area,
                    'raw_points': raw_points,
                    'coarse_points': coarse_points,
                    'raw_density_points_per_m2': (
                        raw_points / sample_area if sample_area else 0.0
                    ),
                    'raw_per_coarse_sample': (
                        raw_points / coarse_points if coarse_points else None
                    ),
                }
            )
        return samples

    calibration_samples = calibrate_density(
        plan_bounds,
        config.stage_planner_calibration_sample_count,
        config.stage_planner_calibration_window_m,
    )
    report_progress('source_calibration')

    measured_multipliers = [
        sample['raw_per_coarse_sample']
        for sample in calibration_samples
        if sample['raw_per_coarse_sample'] is not None
    ]
    if not measured_multipliers:
        raise RuntimeError(
            'macro-v3 planner could not calibrate a raw-point estimate: all '
            'bounded coarse samples were empty'
        )
    measured_density = max(
        sample['raw_density_points_per_m2'] for sample in calibration_samples
    )
    measured_multiplier = max(measured_multipliers)
    configured_multiplier = config.stage_planner_sample_point_multiplier
    point_multiplier = max(
        measured_multiplier,
        configured_multiplier or 0.0,
    ) * config.stage_planner_density_safety_factor
    density_ceiling = (
        measured_density * config.stage_planner_density_safety_factor
    )

    def estimate(block: list[Extents]) -> dict:
        name = _shard_name(block)
        if name in estimates:
            return estimates[name]
        bounds = _block_bounds(block)
        query_bounds = Bounds(
            bounds.minx - collar,
            bounds.miny - collar,
            bounds.maxx + collar,
            bounds.maxy + collar,
        )
        query_area = (query_bounds.maxx - query_bounds.minx) * (
            query_bounds.maxy - query_bounds.miny
        )
        local_samples = calibrate_density(
            bounds,
            config.stage_planner_local_calibration_sample_count,
            config.stage_planner_local_calibration_window_m,
        )
        local_calibration_map.extend(local_samples)

        def intersects(observation: dict) -> bool:
            minx, miny, maxx, maxy = observation['bounds']
            return not (
                maxx < query_bounds.minx
                or minx > query_bounds.maxx
                or maxy < query_bounds.miny
                or miny > query_bounds.maxy
            )

        # Include local reads collected for an ancestor or an adjacent
        # candidate only when their sampled footprint intersects this query.
        # That preserves a spatial density map through recursive splitting.
        inherited_samples = [
            observation
            for observation in local_calibration_map
            if observation not in local_samples and intersects(observation)
        ]
        applicable_samples = [*inherited_samples, *local_samples]
        local_multipliers = [
            observation['raw_per_coarse_sample']
            for observation in applicable_samples
            if observation['raw_per_coarse_sample'] is not None
        ]
        local_multiplier = max(
            [
                measured_multiplier,
                configured_multiplier or 0.0,
                *local_multipliers,
            ]
        ) * config.stage_planner_density_safety_factor
        local_density = max(
            measured_density,
            *(
                observation['raw_density_points_per_m2']
                for observation in applicable_samples
            ),
        ) * config.stage_planner_density_safety_factor
        density_estimate = int(math.ceil(query_area * local_density))
        # The conservative density estimate already selects a split for this
        # candidate. Reading a very large EPT hierarchy just to reach the
        # same decision is unnecessary and can fail on wide source queries.
        # Every eventual under-target leaf still receives a bounded coarse read.
        if density_estimate > target:
            sample = None
            coarse_estimate = None
            query_timing['coarse_queries_skipped_by_density'] += 1
        else:
            sample = coarse_count(query_bounds)
            coarse_estimate = int(math.ceil(sample * local_multiplier))
        details = {
            'name': name,
            'macro_count': len(block),
            'bounds': bounds.get(),
            'quickinfo_points': sample,
            'estimated_coarse_raw_points': coarse_estimate,
            'estimated_density_raw_points': density_estimate,
            'estimated_max_points': max(coarse_estimate or 0, density_estimate),
            'coarse_query_skipped_by_density': sample is None,
            'local_calibration_samples': local_samples,
            'inherited_local_density_observations': len(inherited_samples),
            'local_point_multiplier': local_multiplier,
            'local_density_ceiling_points_per_m2': local_density,
        }
        estimates[name] = details
        if len(estimates) % 25 == 0:
            report_progress('candidate_estimation')
        return details

    def split(block: list[Extents], depth: int = 0) -> list[list[Extents]]:
        details = estimate(block)
        if details['estimated_max_points'] <= target:
            return [block]
        children = split_macro_block_once(block, config)
        if depth >= config.build_max_split_depth or not children:
            details['split_limit_reached'] = True
            details['split_depth'] = depth
            return [block]
        return [
            descendant
            for child in children
            for descendant in split(child, depth + 1)
        ]

    blocks = split(macros)
    selected = [estimate(block) for block in blocks]
    report_progress('complete')
    return blocks, {
        'method': 'pdal-summary-coarse-resolution-upper-bound',
        'count_source': 'bounded-reader-execution',
        'base_resolution': base_resolution,
        'coarse_resolution': coarse_resolution,
        'resolution_multiple': config.stage_planner_resolution_multiple,
        'point_multiplier': point_multiplier,
        'point_multiplier_source': 'bounded-native-calibration',
        'configured_point_multiplier': configured_multiplier,
        'density_ceiling_points_per_m2': density_ceiling,
        'density_safety_factor': config.stage_planner_density_safety_factor,
        'calibration_samples': calibration_samples,
        'local_calibration_map': local_calibration_map,
        'local_calibration_sample_count': (
            config.stage_planner_local_calibration_sample_count
        ),
        'local_calibration_window_m': (
            config.stage_planner_local_calibration_window_m
        ),
        'processing_halo_m': collar,
        'target_points': target,
        'macro_count': len(macros),
        'planned_shard_count': len(blocks),
        'input_processing_grid_sha256': input_grid,
        'processing_grid_sha256': processing_grid_digest(blocks),
        'processing_macro_count': sum(map(len, blocks)),
        'processing_macro_split_count': sum(map(len, blocks)) - len(macros),
        'planner_wall_seconds': round(perf_counter() - planning_started, 6),
        'query_timing': {
            key: round(value, 6) if key.endswith('_seconds') else value
            for key, value in query_timing.items()
        },
        'estimated_max_points': sum(
            window['estimated_max_points'] for window in selected
        ),
        'largest_window_estimated_max_points': max(
            (window['estimated_max_points'] for window in selected), default=0
        ),
        'windows': selected,
    }


def summarize_macro_timing(
    results: list[MacroTaskResult],
    driver_seconds: float,
    strategy: str = 'macro-v2',
) -> dict:
    """Summarize per-task timings without retaining task DataFrames."""
    phases = (
        'read_seconds',
        'aggregate_seconds',
        'metric_seconds',
        'join_seconds',
        'write_seconds',
        'total_seconds',
    )
    nonempty = [result for result in results if result.point_count]
    timing = {
        'strategy': strategy,
        'task_count': len(results),
        'macro_count': sum(result.macro_count for result in results),
        'nonempty_task_count': len(nonempty),
        'point_count': sum(result.point_count for result in results),
        'cell_count': sum(result.cell_count for result in results),
        'driver_seconds': round(driver_seconds, 6),
        'worker_seconds': {
            phase: round(sum(getattr(result, phase) for result in results), 6)
            for phase in phases
        },
        'longest_task_seconds': {
            phase: round(
                max(
                    (getattr(result, phase) for result in results),
                    default=0.0,
                ),
                6,
            )
            for phase in phases
        },
        'stage': {
            'published_shard_count': sum(
                bool(result.shard_uri) for result in results
            ),
            'local_fragment_count': sum(
                result.stage_local_fragment_count for result in results
            ),
            'published_fragment_count': sum(
                result.published_fragment_count for result in results
            ),
            'published_bytes': sum(
                result.published_bytes for result in results
            ),
            'consolidation_count': sum(
                result.stage_consolidation_count for result in results
            ),
            'publish_seconds': round(
                sum(result.stage_publish_seconds for result in results), 6
            ),
        },
    }
    diagnostics = [
        diagnostic
        for result in results
        for diagnostic in result.macro_diagnostics
    ]
    if diagnostics:
        timing['macro_diagnostics'] = diagnostics
    return timing


def merge_macro_timing(existing: dict, batch: dict) -> dict:
    """Accumulate macro batches split at underlying TileDB tile boundaries."""
    if existing.get('strategy') != batch['strategy']:
        return batch

    return {
        'strategy': batch['strategy'],
        'task_count': existing['task_count'] + batch['task_count'],
        'macro_count': existing.get('macro_count', existing['task_count'])
        + batch.get('macro_count', batch['task_count']),
        'nonempty_task_count': (
            existing['nonempty_task_count'] + batch['nonempty_task_count']
        ),
        'point_count': existing['point_count'] + batch['point_count'],
        'cell_count': existing['cell_count'] + batch['cell_count'],
        'driver_seconds': round(
            existing['driver_seconds'] + batch['driver_seconds'], 6
        ),
        'run_macro_consolidation_seconds': round(
            existing.get('run_macro_consolidation_seconds', 0.0)
            + batch.get('run_macro_consolidation_seconds', 0.0),
            6,
        ),
        'worker_seconds': {
            phase: round(
                existing['worker_seconds'][phase]
                + batch['worker_seconds'][phase],
                6,
            )
            for phase in batch['worker_seconds']
        },
        'longest_task_seconds': {
            phase: max(
                existing['longest_task_seconds'][phase],
                batch['longest_task_seconds'][phase],
            )
            for phase in batch['longest_task_seconds']
        },
        'stage': {
            key: round(
                existing.get('stage', {}).get(key, 0)
                + batch.get('stage', {}).get(key, 0),
                6,
            )
            for key in {
                *existing.get('stage', {}),
                *batch.get('stage', {}),
            }
        },
        # macro-v3 presently plans once for the complete run, but retaining
        # the most recent plan keeps this merger safe if a caller batches it.
        'planner': batch.get('planner', existing.get('planner')),
    }


def shatter(config: ShatterConfig) -> int:
    """
    Handle setup and running of shatter process.
    Will look for a config that has already been run before and needs to be
    resumed.

    :param config: :class:`silvimetric.resources.config.ShatterConfig`.
    :return: Number of points processed.
    """

    shatter_started = perf_counter()
    maintenance_seconds = 0.0

    # get start time in milliseconds if not already set
    if config.start_timestamp is None:
        config.start_timestamp = int(datetime.now().timestamp() * 1000)

    # set up tiledb
    storage = Storage.from_db(config.tdb_dir)
    if config.usgs_albers != storage.config.usgs_albers:
        raise ValueError(
            '--usgs_albers must be used exactly when the destination was '
            'initialized with the USGS Albers grid profile'
        )
    data = Data(config.filename, storage.config, config.bounds)
    extents = Extents.from_sub(config.tdb_dir, data.bounds)
    # Persist the normalized planning bounds on this run's configuration.
    # ``Data`` owns a defensive copy, so this is now stable across a resume.
    config.bounds = data.bounds
    if config.processing_strategy == 'macro-v4-staged-publish':
        _assert_disjoint_macro_v4_append(config, storage, extents)

    # Let the normal exception path record a durable partial ledger state and
    # metadata before stopping. The prior handler only saved metadata and
    # then swallowed SIGINT, which made a requested interruption continue.
    def interrupt(signum, frame):
        raise KeyboardInterrupt(f'Shatter interrupted by signal {signum}')

    previous_sigint = signal.getsignal(signal.SIGINT)
    previous_sigterm = signal.getsignal(signal.SIGTERM)
    signal.signal(signal.SIGINT, interrupt)
    signal.signal(signal.SIGTERM, interrupt)

    def restore_signal_handlers():
        signal.signal(signal.SIGINT, previous_sigint)
        signal.signal(signal.SIGTERM, previous_sigterm)

    config.log.debug(f'Shatter Config: {config}')
    config.log.debug(f'Data: {data}')
    config.log.debug(f'Extents: {extents}')

    if not config.time_slot:  # defaults to 0, which is reserved for storage cfg
        if config.processing_strategy == 'macro-v4-staged-publish':
            # A recovery uses the original build's history slot.  Looking up
            # that immutable ledger receipt before reserving a new slot keeps
            # ``next_time_slot`` contiguous and, more importantly, prevents
            # an otherwise read-only sealed-build verification from mutating
            # canonical array metadata.
            ledger = BuildLedger(
                config.build_ledger_uri, Storage.get_tdb_context(storage)
            )
            original_manifest = ledger.build_manifest()
            if (
                original_manifest is not None
                and original_manifest.details.get('build_id') == str(config.name)
            ):
                original_time_slot = original_manifest.details.get('time_slot')
                if original_time_slot is None:
                    raise ValueError(
                        'The existing build ledger predates time-slot recovery '
                        'and cannot safely resume this canonical-array build.'
                    )
                config.time_slot = int(original_time_slot)
        if not config.time_slot:
            config.time_slot = storage.reserve_time_slot()

    if config.processing_strategy == 'macro-v3-stage-push':
        Storage.create_shard_group(storage.config, config.stage_publish_uri)
    elif config.processing_strategy != 'macro-v4-staged-publish':
        storage.save_shatter_meta(config)

    leaf_size = storage.config.ysize * storage.config.xsize
    # Enumerate only the root-aligned physical tiles around the input extent.
    # This is equivalent to the historical root-then-filter algorithm for a
    # local root, but it is what makes a full canonical CONUS root practical.
    potential_leaves = extents.get_root_aligned_leaf_children(leaf_size)
    tiled_leaves = [extents.get_overlap(leaf) for leaf in potential_leaves]
    full_count = len(tiled_leaves)
    if config.processing_strategy in {
        'macro-v3-stage-push',
        'macro-v4-single-array',
        'macro-v4-staged-publish',
    }:
        # Group across every underlying TileDB tile.  Grouping each tile
        # separately would reintroduce tiny published shards at tile edges.
        macros = [
            macro
            for extent in tiled_leaves
            for macro in extent.get_leaf_children(config.read_group_size)
        ]
        try:
            if config.processing_strategy == 'macro-v3-stage-push':
                run_macro_to_stage(iter(macros), config, storage, data)
            elif config.processing_strategy == 'macro-v4-staged-publish':
                run_macro_staged_publish(
                    iter(macros), config, storage, data
                )
            else:
                run_macro_to_single_array(
                    iter(macros), config, storage, data
                )
        except BaseException as e:
            restore_signal_handlers()
            _record_staged_build_interruption(config, storage, e)
            final(config, storage)
            raise
    else:
        count = 0
        for e in tiled_leaves:
            count = count + 1
            if config.processing_strategy == 'macro-v2':
                leaves = e.get_leaf_children(config.read_group_size)
            elif config.tile_size is not None:
                leaves = e.get_leaf_children(config.tile_size)
            else:
                leaves = e.chunk(data, pc_threshold=config.tile_point_count)

            config.log.debug(f'Shattering extent #{count}/{full_count}.')
            try:
                if config.processing_strategy == 'macro-v2':
                    run_macro(leaves, config, storage)
                else:
                    run(leaves, config, storage)
                maintenance_started = perf_counter()
                storage.vacuum()
                for mode in ['fragment_meta', 'commits', 'array_meta']:
                    storage.consolidate(mode)
                    storage.vacuum(mode)
                maintenance_seconds += perf_counter() - maintenance_started
            except BaseException as e:
                restore_signal_handlers()
                final(config, storage)
                raise

    if config.processing_strategy == 'macro-v3-stage-push':
        config.tdb_dir = config.stage_publish_uri
        config.end_timestamp = int(datetime.now().timestamp() * 1000)
        config.finished = True
        with tiledb.Group(config.tdb_dir, 'w') as group:
            group.meta[f'shatter_{config.time_slot}'] = json.dumps(
                config.to_json()
            )

    if config.processing_strategy in {
        'macro-v2',
        'macro-v3-stage-push',
        'macro-v4-single-array',
        'macro-v4-staged-publish',
    }:
        config.execution_timing['maintenance_seconds'] = round(
            maintenance_seconds, 6
        )
        config.execution_timing['shatter_total_seconds'] = round(
            perf_counter() - shatter_started, 6
        )

    if config.processing_strategy != 'macro-v3-stage-push':
        # The publish phase is intentionally a durable but unfinished history
        # entry. ``finalize_macro_v4_staged_build`` owns the finished marker
        # once scheduler-only TileDB maintenance has sealed the array.
        final(
            config,
            storage,
            finished=not (
                config.processing_strategy == 'macro-v4-staged-publish'
                and config.defer_build_finalization
            ),
        )
    restore_signal_handlers()
    return config.point_count
