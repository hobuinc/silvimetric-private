"""Reproducible FUSION GridMetrics source-baseline support.

CI builds the vendored portable GridMetrics executable and supplies it through
``FUSION_GRIDMETRICS``. Developers can set the same variable to an executable
or wrapper command. ``FUSION_GRIDMETRICS_OUTPUT_DIR`` preserves a regenerated
baseline for inspection; otherwise pytest creates a temporary output folder.
"""

from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess


FUSION_GRIDMETRICS_ENV = 'FUSION_GRIDMETRICS'
# Desired output extent. GridMetrics treats the maximum /gridxy coordinate as
# the lower-left corner of one more cell, while Silvimetric's storage bounds
# are the outside edge of the last cell.  The command below therefore submits
# a maximum one cell inside this extent.
FUSION_GRID = (635535.0, 4402335.0, 635865.0, 4402815.0)
FUSION_HEIGHT_BREAK = 2.0
FUSION_MINIMUM_HEIGHT = 2.0
FUSION_MINIMUM_POINTS = 3
FUSION_CELL_SIZE = 30.0
FUSION_RASTER_PRODUCTS = ','.join(
    [
        'count',
        'min',
        'max',
        'mean',
        'mode',
        'stddev',
        'variance',
        'cv',
        'skewness',
        'kurtosis',
        'aad',
        'p01',
        'p05',
        'p10',
        'p20',
        'p25',
        'p30',
        'p40',
        'p50',
        'p60',
        'p70',
        'p75',
        'p80',
        'p90',
        'p95',
        'p99',
        'iq',
        '90m10',
        '95m05',
        'cover',
        'allcover',
        'afcover',
        'allcount',
        'allabovemean',
        'allabovemoder',
        'afabovemean',
        'afabovemode',
        'fcountmean',
        'fcountmode',
        'allcountmean',
        'allcountmode',
        'totalfirst',
        'totalall',
        'r1count',
        'r2count',
        'r3count',
        'r4count',
        'r5count',
        'r6count',
        'r7count',
        'r8count',
        'r9count',
        'rothercount',
    ]
)


def configured_gridmetrics_command() -> list[str] | None:
    """Return the configured executable/wrapper command, if available."""
    configured = os.environ.get(FUSION_GRIDMETRICS_ENV)
    if not configured:
        return None
    command = shlex.split(configured)
    if not command:
        return None
    executable = Path(command[0])
    if not executable.is_file() and shutil.which(command[0]) is None:
        return None
    return command


def gridmetrics_command(
    executable: list[str], source_las: Path, output_dir: Path
) -> list[str]:
    """Build the exact normalized-height FUSION GridMetrics invocation.

    GridMetrics parses a leading slash as an option delimiter.  Run from the
    output directory and deliberately use relative POSIX-safe paths here;
    Windows builds accept the same invocation.
    """
    minx, miny, maxx, maxy = FUSION_GRID
    grid = ','.join(
        str(int(value))
        for value in (minx, miny, maxx - FUSION_CELL_SIZE, maxy - FUSION_CELL_SIZE)
    )
    return [
        *executable,
        '/noground',
        '/nointdtm',
        '/ascii',
        f'/minht:{FUSION_MINIMUM_HEIGHT:g}',
        f'/minpts:{FUSION_MINIMUM_POINTS}',
        f'/gridxy:{grid}',
        f'/raster:{FUSION_RASTER_PRODUCTS}',
        f'{FUSION_HEIGHT_BREAK:g}',
        f'{FUSION_CELL_SIZE:g}',
        'fusion',
        source_las.name,
    ]


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()


def generate_fusion_gridmetrics(
    executable: list[str], copc_path: Path, output_dir: Path
) -> Path:
    """Generate ASCII FUSION rasters from the canonical COPC test input.

    FUSION's legacy LAS reader does not reliably understand the COPC
    hierarchy.  PDAL first expands the LFS COPC file to an ordinary LAS file,
    preserving the point records that GridMetrics receives.
    """
    output_dir.mkdir(parents=True, exist_ok=True)
    source_las = output_dir / 'fusion-source.las'
    pdal = shutil.which('pdal')
    if pdal is None:
        raise RuntimeError(
            'pdal executable is required to generate FUSION data'
        )

    subprocess.run(
        [pdal, 'translate', str(copc_path), str(source_las)],
        check=True,
        capture_output=True,
        text=True,
    )
    command = gridmetrics_command(executable, source_las, output_dir)
    result = subprocess.run(
        command,
        cwd=output_dir,
        check=False,
        capture_output=True,
        text=True,
    )
    manifest = {
        'copc': copc_path.name,
        'copc_sha256': _sha256(copc_path),
        'source_las': source_las.name,
        'gridmetrics_command': command,
        'returncode': result.returncode,
        'stdout': result.stdout,
        'stderr': result.stderr,
    }
    (output_dir / 'fusion-gridmetrics-manifest.json').write_text(
        json.dumps(manifest, indent=2) + '\n', encoding='utf8'
    )
    if result.returncode:
        raise RuntimeError(
            'FUSION GridMetrics failed; see '
            f'{output_dir / "fusion-gridmetrics-manifest.json"}'
        )
    if not list(output_dir.glob('fusion_*_metrics_*.asc')):
        raise RuntimeError('FUSION GridMetrics completed without ASCII rasters')
    return output_dir


def generated_metric_map(
    fusion_dir: Path, silvimetric_dir: Path
) -> dict[Path, Path]:
    """Map generated FUSION ASCII grids to equivalent extracted SM rasters."""
    elevation = {
        'min': 'm_Z_min.tif',
        'max': 'm_Z_max.tif',
        'mean': 'm_Z_mean.tif',
        'mode': 'm_Z_mode.tif',
        'stddev': 'm_Z_stddev.tif',
        'variance': 'm_Z_variance.tif',
        'cv': 'm_Z_cv.tif',
        'skewness': 'm_Z_skewness.tif',
        'kurtosis': 'm_Z_kurtosis.tif',
        'aad': 'm_Z_aad.tif',
        'IQ': 'm_Z_iq.tif',
        'p90_minus_p10': 'm_Z_90m10.tif',
        'p95_minus_p05': 'm_Z_95m05.tif',
    }
    percentile_values = (
        1,
        5,
        10,
        20,
        25,
        30,
        40,
        50,
        60,
        70,
        75,
        80,
        90,
        95,
        99,
    )
    elevation.update(
        {
            f'p{value:02d}': f'm_Z_p{value:02d}.tif'
            for value in percentile_values
        }
    )
    intensity = {
        key: value.replace('m_Z_', 'm_Intensity_')
        for key, value in elevation.items()
        if key not in {'p90_minus_p10', 'p95_minus_p05'}
    }
    counts = {
        'count': 'm_ReturnNumber_all_count_above_minht.tif',
        'all_cover': 'm_ReturnNumber_all_cover_above_htbreak.tif',
        'all_first_cover': 'm_ReturnNumber_all_1st_cover_above_htbreak.tif',
        'all_cover_count': 'm_ReturnNumber_all_count_above_htbreak.tif',
        'first_above_mean_count': 'm_Z_1st_count_above_mean.tif',
        'first_above_mode_count': 'm_Z_1st_count_above_mode.tif',
        'all_above_mean_count': 'm_Z_all_count_above_mean.tif',
        'all_above_mode_count': 'm_Z_all_count_above_mode.tif',
        'total_first_count': 'm_ReturnNumber_1st_count.tif',
        'total_count': 'm_ReturnNumber_all_count.tif',
        **{
            f'return_{number}_count': f'm_ReturnNumber_r{number}_count.tif'
            for number in range(1, 10)
        },
        'return_other_count': 'm_ReturnNumber_rother_count.tif',
    }
    result = {
        fusion_dir / f'fusion_all_returns_all_metrics_elevation_{name}.asc': (
            silvimetric_dir / output
        )
        for name, output in elevation.items()
    }
    result.update(
        {
            (
                fusion_dir
                / f'fusion_all_returns_all_metrics_intensity_{name}.asc'
            ): (silvimetric_dir / output)
            for name, output in intensity.items()
        }
    )
    result.update(
        {
            (
                fusion_dir
                / f'fusion_all_returns_all_metrics_elevation_{name}.asc'
            ): (silvimetric_dir / output)
            for name, output in counts.items()
        }
    )
    return result
