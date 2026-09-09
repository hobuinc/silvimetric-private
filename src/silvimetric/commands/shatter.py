import numpy as np
import signal
import json
import math
import os
import resource
import sys
import uuid
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


def get_data(
    extents: Extents,
    filename: str,
    storage: Storage,
    reader_collar: float | None = None,
) -> pd.DataFrame:
    """
    Execute pipeline and retrieve point cloud data for this extent

    :param extents: :class:`silvimetric.resources.extents.Extents` being used.
    :param filename: Path to either PDAL pipeline or point cloud.
    :param storage: :class:`silvimetric.resources.storage.Storage` database.
    :return: Point data array from PDAL.
    """
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
    # ceil for y because origin is at top left
    points.loc[:, 'yi'] = np.ceil(points.yi).astype(np.int32)

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
    points = get_data(leaf, config.filename, storage)
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
    macro_count: int = 1
    stage_consolidation_count: int = 0
    stage_local_fragment_count: int = 0
    published_fragment_count: int = 0
    published_bytes: int = 0
    stage_publish_seconds: float = 0.0
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
    target = config.stage_shard_target_points
    if target is None:
        blocks = group_macro_blocks(macros, config, storage)
        return blocks, {
            'method': 'fixed-macro-side',
            'stage_shard_side_macros': config.stage_shard_side_macros,
            'macro_count': len(macros),
            'planned_shard_count': len(blocks),
        }

    base_resolution = storage.config.resolution
    coarse_resolution = (
        base_resolution * config.stage_planner_resolution_multiple
    )
    collar = config.processing_halo_m or 0.0
    estimates: dict[str, dict] = {}

    plan_bounds = _block_bounds(macros)
    sample_side = min(
        config.stage_planner_calibration_window_m,
        plan_bounds.maxx - plan_bounds.minx,
        plan_bounds.maxy - plan_bounds.miny,
    )
    sample_grid_side = math.ceil(
        math.sqrt(config.stage_planner_calibration_sample_count)
    )
    calibration_samples = []
    for index in range(config.stage_planner_calibration_sample_count):
        x_index = index % sample_grid_side
        y_index = index // sample_grid_side
        center_x = plan_bounds.minx + (
            (x_index + 0.5) / sample_grid_side
        ) * (plan_bounds.maxx - plan_bounds.minx)
        center_y = plan_bounds.miny + (
            (y_index + 0.5) / sample_grid_side
        ) * (plan_bounds.maxy - plan_bounds.miny)
        sample_bounds = Bounds(
            max(plan_bounds.minx, center_x - sample_side / 2),
            max(plan_bounds.miny, center_y - sample_side / 2),
            min(plan_bounds.maxx, center_x + sample_side / 2),
            min(plan_bounds.maxy, center_y + sample_side / 2),
        )
        sample_area = (sample_bounds.maxx - sample_bounds.minx) * (
            sample_bounds.maxy - sample_bounds.miny
        )
        raw_points = int(data.count(sample_bounds))
        coarse_points = int(
            data.estimate_count(
                sample_bounds, reader_resolution=coarse_resolution
            )
        )
        calibration_samples.append(
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
        sample = int(
            data.estimate_count(
                query_bounds, reader_resolution=coarse_resolution
            )
        )
        query_area = (query_bounds.maxx - query_bounds.minx) * (
            query_bounds.maxy - query_bounds.miny
        )
        coarse_estimate = int(math.ceil(sample * point_multiplier))
        density_estimate = int(math.ceil(query_area * density_ceiling))
        details = {
            'name': name,
            'macro_count': len(block),
            'bounds': bounds.get(),
            'quickinfo_points': sample,
            'estimated_coarse_raw_points': coarse_estimate,
            'estimated_density_raw_points': density_estimate,
            'estimated_max_points': max(coarse_estimate, density_estimate),
        }
        estimates[name] = details
        return details

    def split(block: list[Extents]) -> list[list[Extents]]:
        details = estimate(block)
        if (
            len(block) == 1
            or details['estimated_max_points'] <= target
        ):
            return [block]
        bounds = _block_bounds(block)
        split_x = (bounds.maxx - bounds.minx) >= (bounds.maxy - bounds.miny)
        key = (
            (lambda macro: (macro.bounds.minx, macro.bounds.miny))
            if split_x
            else (lambda macro: (macro.bounds.miny, macro.bounds.minx))
        )
        ordered = sorted(block, key=key)
        midpoint = len(ordered) // 2
        return split(ordered[:midpoint]) + split(ordered[midpoint:])

    blocks = split(macros)
    selected = [estimate(block) for block in blocks]
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
        'processing_halo_m': collar,
        'target_points': target,
        'macro_count': len(macros),
        'planned_shard_count': len(blocks),
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
    data = Data(config.filename, storage.config, config.bounds)
    extents = Extents.from_sub(config.tdb_dir, data.bounds)

    # try to catch sigints and save info about work that has been done so far
    signal.signal(
        signal.SIGINT, lambda signum, frame, c=config, s=storage: final(c, s)
    )

    config.log.debug(f'Shatter Config: {config}')
    config.log.debug(f'Data: {data}')
    config.log.debug(f'Extents: {extents}')

    if not config.time_slot:  # defaults to 0, which is reserved for storage cfg
        config.time_slot = storage.reserve_time_slot()

    if config.bounds is None:
        config.bounds = extents.bounds
    if config.processing_strategy == 'macro-v3-stage-push':
        Storage.create_shard_group(storage.config, config.stage_publish_uri)
    else:
        storage.save_shatter_meta(config)

    leaf_size = storage.config.ysize * storage.config.xsize
    root_ext = Extents(
        bounds=extents.root,
        resolution=extents.resolution,
        alignment=extents.alignment,
        root=extents.root,
    )
    # get leaves reflecting TileDB tile bounds
    potential_leaves = root_ext.get_leaf_children(leaf_size)
    # filter by tiles that overlap, and get the overlapping extent
    filtered_leaves = [
        leaf
        for leaf in potential_leaves if not extents.disjoint(leaf)
    ]
    tiled_leaves = [extents.get_overlap(leaf) for leaf in filtered_leaves]
    full_count = len(tiled_leaves)
    if config.processing_strategy == 'macro-v3-stage-push':
        # Group across every underlying TileDB tile.  Grouping each tile
        # separately would reintroduce tiny published shards at tile edges.
        macros = [
            macro
            for extent in tiled_leaves
            for macro in extent.get_leaf_children(config.read_group_size)
        ]
        try:
            run_macro_to_stage(iter(macros), config, storage, data)
        except Exception as e:
            final(config, storage)
            raise e
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
            except Exception as e:
                final(config, storage)
                raise e

    if config.processing_strategy == 'macro-v3-stage-push':
        config.tdb_dir = config.stage_publish_uri
        config.end_timestamp = int(datetime.now().timestamp() * 1000)
        config.finished = True
        with tiledb.Group(config.tdb_dir, 'w') as group:
            group.meta[f'shatter_{config.time_slot}'] = json.dumps(
                config.to_json()
            )

    if config.processing_strategy in {'macro-v2', 'macro-v3-stage-push'}:
        config.execution_timing['maintenance_seconds'] = round(
            maintenance_seconds, 6
        )
        config.execution_timing['shatter_total_seconds'] = round(
            perf_counter() - shatter_started, 6
        )

    if config.processing_strategy != 'macro-v3-stage-push':
        final(config, storage, finished=True)
    return config.point_count
