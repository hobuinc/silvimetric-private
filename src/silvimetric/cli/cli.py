import click
from dask.distributed import performance_report
import pyproj
import logging


from .. import __version__
from .. import Attribute, Metric, Bounds, Log
from .. import Storage, StorageConfig, ShatterConfig, ExtractConfig, ApplicationConfig
from ..commands import shatter, extract, scan, info, initialize, manage
from .common import (
    BoundsParamType,
    CRSParamType,
    AttrParamType,
    MetricParamType,
)
from .common import dask_handle, close_dask


@click.group()
@click.option(
    '--database', '-d', type=click.Path(exists=False), help='Database path'
)
@click.option(
    '--debug',
    is_flag=True,
    default=False,
    help='Changes logging level from INFO to DEBUG.',
)
@click.option(
    '--log-dir', default=None, help='Directory for log output', type=str
)
@click.option('--workers', type=int, help='Number of workers for Dask')
@click.option(
    '--threads', type=int, help='Number of threads per worker for Dask'
)
@click.option(
    '--watch',
    is_flag=True,
    default=False,
    type=bool,
    help='Open dask diagnostic page in default web browser.',
)
@click.option(
    '--dasktype',
    default='processes',
    type=click.Choice(['threads', 'processes']),
    help='What Dask uses for parallelization. For more information see here'
    ' https://docs.dask.org/en/stable/scheduling.html#local-threads',
)
@click.option(
    '--scheduler',
    default='local',
    type=click.Choice(['distributed', 'local', 'single-threaded']),
    help='Type of dask scheduler. Both are '
    'local, but are run with different dask libraries. See more here '
    'https://docs.dask.org/en/stable/scheduling.html.',
)
@click.version_option(__version__)
@click.pass_context
def cli(
    ctx,
    database,
    debug,
    log_dir,
    dasktype,
    scheduler,
    workers,
    threads,
    watch,
):
    # Set up logging
    if debug:
        log_level = 'DEBUG'
    else:
        log_level = 'INFO'

    numeric_level = getattr(logging, log_level.upper(), None)
    if not isinstance(numeric_level, int):
        raise ValueError(f'Invalid log level: {log_level}')

    log = Log(log_level, log_dir)
    app = ApplicationConfig(
        tdb_dir=database,
        log=log,
        debug=debug,
        scheduler=scheduler,
        dasktype=dasktype,
        workers=workers,
        threads=threads,
        watch=watch,
    )
    ctx.obj = app
    ctx.call_on_close(close_dask)


@cli.command('info')
@click.option(
    '--bounds', type=BoundsParamType(), default=None, help='Bounds to filter by'
)
@click.option(
    '--date',
    type=click.DateTime(['%Y-%m-%d', '%Y-%m-%dT%H:%M:%SZ']),
    help='Select processes with this date',
)
@click.option(
    '--history',
    is_flag=True,
    help='Show the history section of the output.',
)
@click.option(
    '--metadata',
    is_flag=True,
    help='Show the metadata section of the output.',
)
@click.option(
    '--metrics',
    is_flag=True,
    help='Show the metrics section of the output.',
)
@click.option(
    '--attributes',
    is_flag=True,
    help='Show the attributes section of the output.',
)
@click.option(
    '--dates',
    type=click.Tuple(
        [
            click.DateTime(['%Y-%m-%d', '%Y-%m-%dT%H:%M:%SZ']),
            click.DateTime(['%Y-%m-%d', '%Y-%m-%dT%H:%M:%SZ']),
        ]
    ),
    nargs=2,
    help='Select processes within this date range',
)
@click.option(
    '--name', type=str, default=None, help='Select processes with this name'
)
@click.pass_obj
def info_cmd(
    app, bounds, date, dates, name, history, metadata, attributes, metrics
):
    import json

    if date is not None and dates is not None:
        app.log.warning(
            "Both 'date' and 'dates' specified. Prioritizing'dates'"
        )

    start_date = dates[0] if dates else date
    end_date = dates[1] if dates else date
    if start_date is None and end_date is None:
        info_dates=None
    else:
        info_dates = tuple(start_date, end_date)

    i = info.info(
        app.tdb_dir,
        bounds=bounds,
        dates = info_dates,
        name=name,
        concise=True,
    )

    ms = [
        {
            'name': v['name'],
            'dtype': v['dtype'],
            'dependencies': [dep['name'] for dep in  v['dependencies']],
        }
        for v in i['metadata']['metrics']
    ]

    i['metadata'].pop('metrics')
    if any([history, metadata, attributes, metrics]):
        filtered = {}
        if history:
            filtered['history'] = i['history']
        if metadata:
            filtered['metadata'] = i['metadata']
        if attributes:
            filtered['attributes'] = i['attributes']
        if metrics:
            filtered['metrics'] = ms

        print(json.dumps(filtered, indent=2))

    else:
        print(json.dumps(i, indent=2))
        return


@cli.command('scan')
@click.argument('pointcloud', type=str)
@click.option(
    '--resolution', type=float, default=100, help='Summary pixel resolution'
)
@click.option(
    '--point_count', type=int, default=600000, help='Point count threshold.'
)
@click.option('--depth', type=int, default=6, help='Quadtree depth threshold.')
@click.option(
    '--bounds', type=BoundsParamType(), default=None, help='Bounds to scan.'
)
@click.pass_obj
def scan_cmd(
    app, resolution, point_count, pointcloud, bounds, depth
):
    """Scan point cloud, output information on it, and determine the optimal
    tile size."""
    dask_handle(
        app.dasktype,
        app.scheduler,
        app.workers,
        app.threads,
        app.watch,
    )
    return scan.scan(
        app.tdb_dir,
        pointcloud,
        bounds,
        point_count,
        resolution,
        depth,
        log=app.log,
    )


@cli.command('initialize')
@click.option(
    '--bounds',
    type=BoundsParamType(),
    required=False,
    help='Root bounds that encapsulates all data',
)
@click.option(
    '--crs',
    type=CRSParamType(),
    required=False,
    help='Coordinate system of data',
)
@click.option(
    '--attributes',
    '-a',
    type=AttrParamType(),
    default=[],
    help='List of attributes to include in Database, eg. -a Z,Intensity',
)
@click.option(
    '--metrics',
    '-m',
    type=MetricParamType(),
    default=[],
    help="List of metrics to include in output, eg. '-m stats,percentiles'",
)
@click.option(
    '--resolution', type=float, default=30.0, help='Summary pixel resolution'
)
@click.option(
    '--xsize', type=float, default=1000, help='TileDB X Tile size.'
)
@click.option(
    '--ysize', type=float, default=1000, help='TileDB Y Tile size.'
)
@click.option(
    '--alignment',
    type=str,
    default='AlignToCenter',
    help="Pixel alignment: 'AlignToCenter' or 'AlignToCorner'",
)
@click.option(
    '--usgs_albers', '--usgs-albers',
    'usgs_albers',
    flag_value=True,
    default=None,
    help='Use the fixed pixel-is-area EPSG:5070+5703 CONUS grid profile (default unless bounds/CRS are specified).',
)
@click.option(
    '--no-usgs-albers', 'usgs_albers', flag_value=False,
    help='Use an explicitly supplied bounds and CRS instead of USGS Albers.',
)
@click.pass_obj
def initialize_cmd(
    app: ApplicationConfig,
    bounds: Bounds,
    crs: pyproj.CRS,
    attributes: list[Attribute],
    resolution: float,
    metrics: list[Metric],
    alignment: str,
    xsize: int,
    ysize: int,
    usgs_albers: bool,
):
    """Initialize silvimetrics DATABASE"""

    if usgs_albers is None:
        usgs_albers = bounds is None and crs is None
    if not usgs_albers and (bounds is None or crs is None):
        raise click.UsageError(
            '--bounds and --crs are required for a non-USGS-Albers database'
        )

    storageconfig = StorageConfig(
        tdb_dir=app.tdb_dir,
        log=app.log,
        root=bounds,
        crs=crs,
        attrs=attributes,
        metrics=metrics,
        resolution=resolution,
        alignment=alignment,
        xsize=xsize,
        ysize=ysize,
        usgs_albers=usgs_albers,
    )
    return initialize.initialize(storageconfig)


@cli.command('shatter')
@click.argument('pointcloud', type=str)
@click.option(
    '--bounds',
    type=BoundsParamType(),
    default=None,
    help='Bounds for data to include in processing',
)
@click.option(
    '--usgs_albers', '--usgs-albers',
    'usgs_albers',
    flag_value=True,
    default=None,
    help=(
        'Use a USGS-Albers-profile database and interpret --bounds as '
        'EPSG:5070 target bounds.'
    ),
)
@click.option(
    '--no-usgs-albers', 'usgs_albers', flag_value=False,
    help='Explicitly use a legacy source-CRS database.',
)
@click.option(
    '--water-mask-uri',
    type=str,
    default=None,
    help=(
        'Public HTTPS, S3, or local URI of a 20 m EPSG:5070 pixel-is-area '
        'water COG. Pixels marked water are omitted before all statistics and '
        'attributes. Entirely water processing cores skip their source read; '
        'mixed cores still read their full PDAL collar for land-pixel HAG. '
        'Requires a USGS Albers database.'
    ),
)
@click.option(
    '--tilesize',
    type=int,
    default=None,
    help='Number of cells to include per tile',
)
@click.option(
    '--processing-strategy',
    type=click.Choice(
        [
            'leaf-v1',
            'macro-v2',
            'macro-v3-stage-push',
            'macro-v4-single-array',
            'macro-v4-staged-publish',
        ]
    ),
    default='leaf-v1',
    show_default=True,
    help='Shatter execution strategy.',
)
@click.option(
    '--read-group-size',
    type=int,
    default=None,
    help='Square-cell count per macro-v2 PDAL read group.',
)
@click.option(
    '--processing-halo-m',
    type=float,
    default=None,
    help='PDAL reader collar in CRS units (default: one storage cell).',
)
@click.option(
    '--stage-tiledb-dir',
    type=str,
    default=None,
    help='Local TileDB URI for macro-v3 stage writes (stage writer only).',
)
@click.option(
    '--stage-publish-uri',
    type=str,
    default=None,
    help='New immutable URI to receive the validated macro-v3 array.',
)
@click.option(
    '--stage-fragment-size-mb',
    type=int,
    default=300,
    show_default=True,
    help=(
        'Desired consolidation-plan fragment size in MiB for macro-v3 local '
        'stages or macro-v4 canonical-array finalization.'
    ),
)
@click.option(
    '--stage-worker-address',
    type=str,
    default=None,
    help='Optional Dask worker address for the macro-v3 stage writer actor.',
)
@click.option(
    '--stage-planner-calibration-sample-count',
    type=click.IntRange(min=1),
    default=4,
    show_default=True,
    help='Whole-AOI bounded native reads used to establish a density floor.',
)
@click.option(
    '--stage-planner-calibration-window-m',
    type=click.FloatRange(min=0, min_open=True),
    default=250.0,
    show_default=True,
    help='Side length in CRS units for each bounded planner density read.',
)
@click.option(
    '--stage-planner-local-calibration-sample-count',
    type=click.IntRange(min=1),
    default=1,
    show_default=True,
    help=(
        'Bounded density reads per candidate macro; detects localized dense '
        'flightlines before staging.'
    ),
)
@click.option(
    '--stage-planner-local-calibration-window-m',
    type=click.FloatRange(min=0, min_open=True),
    default=50.0,
    show_default=True,
    help=(
        'Side length in CRS units for each per-candidate density read; '
        'smaller than the whole-AOI calibration window by design.'
    ),
)
@click.option(
    '--build-stage-uri',
    type=str,
    default=None,
    help='Durable local or S3 prefix for immutable macro-v4 stage attempts.',
)
@click.option(
    '--build-ledger-uri',
    type=str,
    default=None,
    help='Durable local or S3 prefix for macro-v4 append-only build records.',
)
@click.option(
    '--build-publish-concurrency',
    type=int,
    default=4,
    show_default=True,
    help='Maximum concurrent canonical-array publishers for staged macro-v4.',
)
@click.option(
    '--build-stage-retries',
    type=click.IntRange(min=0),
    default=0,
    show_default=True,
    help=(
        'Dask retries for one immutable macro-v4 stage task. Keep zero for '
        'memory-bound work so the failed block splits immediately.'
    ),
)
@click.option(
    '--build-max-split-depth',
    type=int,
    default=8,
    show_default=True,
    help='Maximum adaptive spatial subdivisions after a macro-v4 stage failure.',
)
@click.option(
    '--build-min-cells-per-side',
    type=int,
    default=None,
    help='Smallest adaptive macro-v4 child side in output cells.',
)
@click.option(
    '--build-publish-vfs-parallel-ops',
    type=int,
    default=4,
    show_default=True,
    help='TileDB S3 operations allowed per canonical-array publisher.',
)
@click.option(
    '--build-stage-vfs-parallel-ops',
    type=int,
    default=4,
    show_default=True,
    help='TileDB S3 operations allowed per independent stage writer.',
)
@click.option(
    '--defer-build-finalization',
    is_flag=True,
    default=False,
    help=(
        'Publish macro-v4 blocks but defer canonical-array consolidation to '
        'a scheduler-only finalization step.'
    ),
)
@click.option(
    '--report',
    is_flag=True,
    default=False,
    type=bool,
    help='Whether or not to write a report of the process for debugging',
)
@click.option(
    '--date',
    type=click.DateTime(['%Y-%m-%d', '%Y-%m-%dT%H:%M:%SZ']),
    help='Date the data was produced.',
)
@click.option(
    '--dates',
    type=click.Tuple(
        [
            click.DateTime(['%Y-%m-%d', '%Y-%m-%dT%H:%M:%SZ']),
            click.DateTime(['%Y-%m-%d', '%Y-%m-%dT%H:%M:%SZ']),
        ]
    ),
    nargs=2,
    help='Date range the data was produced during',
)
@click.pass_obj
def shatter_cmd(
    app,
    pointcloud,
    bounds,
    usgs_albers,
    water_mask_uri,
    report,
    tilesize,
    processing_strategy,
    read_group_size,
    processing_halo_m,
    stage_tiledb_dir,
    stage_publish_uri,
    stage_fragment_size_mb,
    stage_worker_address,
    stage_planner_calibration_sample_count,
    stage_planner_calibration_window_m,
    stage_planner_local_calibration_sample_count,
    stage_planner_local_calibration_window_m,
    build_stage_uri,
    build_ledger_uri,
    build_publish_concurrency,
    build_stage_retries,
    build_max_split_depth,
    build_min_cells_per_side,
    build_publish_vfs_parallel_ops,
    build_stage_vfs_parallel_ops,
    defer_build_finalization,
    date,
    dates,
):
    """Insert data provided by POINTCLOUD into the silvimetric DATABASE"""

    dask_handle(
        app.dasktype,
        app.scheduler,
        app.workers,
        app.threads,
        app.watch,
    )

    if date is not None and dates is not None:
        app.log.warning(
            "Both 'date' and 'dates' specified. Prioritizing 'dates'"
        )

    if date is None and dates is None:
        raise ValueError("One of '--date' or '--dates' must be provided.")

    if usgs_albers is None:
        # A shatter must follow its existing destination's spatial profile.
        # This also keeps old source-CRS databases usable without requiring
        # an explicit compatibility flag on every subsequent insertion.
        usgs_albers = Storage.from_db(app.tdb_dir).config.usgs_albers

    config = ShatterConfig(
        tdb_dir=app.tdb_dir,
        date=dates if dates else tuple([date]),
        log=app.log,
        filename=pointcloud,
        bounds=bounds,
        usgs_albers=usgs_albers,
        water_mask_uri=water_mask_uri,
        tile_size=tilesize,
        processing_strategy=processing_strategy,
        read_group_size=read_group_size,
        processing_halo_m=processing_halo_m,
        stage_tdb_dir=stage_tiledb_dir,
        stage_publish_uri=stage_publish_uri,
        stage_fragment_size_mb=stage_fragment_size_mb,
        stage_worker_address=stage_worker_address,
        stage_planner_calibration_sample_count=(
            stage_planner_calibration_sample_count
        ),
        stage_planner_calibration_window_m=(
            stage_planner_calibration_window_m
        ),
        stage_planner_local_calibration_sample_count=(
            stage_planner_local_calibration_sample_count
        ),
        stage_planner_local_calibration_window_m=(
            stage_planner_local_calibration_window_m
        ),
        build_stage_uri=build_stage_uri,
        build_ledger_uri=build_ledger_uri,
        build_publish_concurrency=build_publish_concurrency,
        build_stage_retries=build_stage_retries,
        build_max_split_depth=build_max_split_depth,
        build_min_cells_per_side=build_min_cells_per_side,
        build_publish_vfs_parallel_ops=build_publish_vfs_parallel_ops,
        build_stage_vfs_parallel_ops=build_stage_vfs_parallel_ops,
        defer_build_finalization=defer_build_finalization,
    )

    if report:
        if app.scheduler != 'distributed':
            app.log.warning(
                'Report option is incompatible with scheduler'
                '{scheduler}, skipping.'
            )
            shatter.shatter(config)
        else:
            report_path = f'reports/{config.name}.html'
            with performance_report(report_path):
                shatter.shatter(config)
            app.log.debug(f'Writing report to {report_path}.')
    else:
        shatter.shatter(config)


@cli.command('extract')
@click.option(
    '--attributes',
    '-a',
    type=AttrParamType(),
    default=[],
    help='List of attributes to include output, eg -a Z,Intensity',
)
@click.option(
    '--metrics',
    '-m',
    type=MetricParamType(),
    default=[],
    help="List of metrics to include in output, eg. '-m stats,percentiles'",
)
@click.option(
    '--bounds',
    type=BoundsParamType(),
    default=None,
    help='Bounds for data to include in output',
)
@click.option(
    '--outdir',
    '-o',
    type=click.Path(exists=False),
    required=True,
    help='Output directory.',
)
@click.pass_obj
def extract_cmd(app, attributes, metrics, outdir, bounds):
    """Extract silvimetric metrics from DATABASE"""

    dask_handle(
        app.dasktype,
        app.scheduler,
        app.workers,
        app.threads,
        app.watch,
    )

    config = ExtractConfig(
        tdb_dir=app.tdb_dir,
        log=app.log,
        out_dir=outdir,
        attrs=attributes,
        metrics=metrics,
        bounds=bounds,
    )
    extract.extract(config)


@cli.command('delete')
@click.option(
    '--task_id',
    '--id',
    type=click.UUID,
    required=True,
    help='Shatter Task UUID.',
)
@click.pass_obj
def delete_cmd(app, task_id):
    manage.delete(storage=app.tdb_dir, name=task_id, log=app.log)


@cli.command('restart')
@click.option(
    '--task_id',
    '--id',
    type=click.UUID,
    required=True,
    help='Shatter Task UUID.',
)
@click.pass_obj
def restart_cmd(app, task_id):
    manage.restart(storage=app.tdb_dir, name=task_id, log=app.log)


@cli.command('resume')
@click.option(
    '--task_id',
    '--id',
    type=click.UUID,
    required=True,
    help='Shatter Task UUID.',
)
@click.pass_obj
def resume_cmd(app, task_id):
    manage.resume(storage=app.tdb_dir, name=task_id, log=app.log)


if __name__ == '__main__':
    cli()
