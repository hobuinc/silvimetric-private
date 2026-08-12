"""End-to-end checks for GDAL's TileDB raster driver."""

import json
import os
import copy
from math import floor
from pathlib import Path
import shutil
import subprocess

import numpy as np
import pytest

from silvimetric import (
    Attributes,
    Log,
    ShatterConfig,
    Storage,
    StorageConfig,
    shatter,
)
from silvimetric.resources.metrics.grid_metrics import get_grid_metrics
from silvimetric.resources.storage import _GDAL_DATA_TYPES


def _gdalinfo() -> str:
    """Return a TileDB-capable GDAL CLI, or skip the smoke test."""
    executable = os.environ.get('GDALINFO') or shutil.which('gdalinfo')
    if executable is None:
        pytest.skip('gdalinfo is not installed')
    probe = subprocess.run(
        [executable, '--format', 'TileDB'],
        check=False,
        capture_output=True,
        text=True,
    )
    if probe.returncode:
        pytest.skip('gdalinfo was built without the TileDB driver')
    return executable


def _run(command: list[str]) -> subprocess.CompletedProcess:
    return subprocess.run(
        command,
        check=False,
        capture_output=True,
        text=True,
    )


def test_gdal_cli_reads_every_metric(
    copc_filepath, bounds, crs, date, alignment, tmp_path
):
    """GDAL can open and translate every configured raster metric."""
    gdalinfo = _gdalinfo()
    gdal_translate = str(Path(gdalinfo).with_name('gdal_translate'))
    if not os.path.isfile(gdal_translate):
        pytest.skip('gdal_translate is not installed alongside gdalinfo')

    database = tmp_path / 'silvimetric.tdb'
    storage = Storage.create(
        StorageConfig(
            tdb_dir=str(database),
            log=Log('INFO'),
            root=copy.deepcopy(bounds),
            crs=crs,
            resolution=30,
            alignment=alignment,
            attrs=[
                copy.deepcopy(Attributes[name])
                for name in (
                    'Z',
                    'NumberOfReturns',
                    'ReturnNumber',
                    'Intensity',
                )
            ],
            metrics=list(get_grid_metrics().values()),
            xsize=5,
            ysize=5,
        )
    )
    shatter(
        ShatterConfig(
            tdb_dir=storage.config.tdb_dir,
            log=Log('INFO'),
            filename=copc_filepath,
            bounds=copy.deepcopy(bounds),
            date=date,
            tile_size=10,
        )
    )

    result = _run([gdalinfo, '-json', storage.config.tdb_dir])
    assert result.returncode == 0, result.stderr
    dataset = json.loads(result.stdout)

    root = storage.config.root
    resolution = storage.config.resolution
    assert dataset['size'] == [
        floor((root.maxx - root.minx) / resolution),
        floor((root.maxy - root.miny) / resolution),
    ]
    assert dataset['geoTransform'] == [
        root.minx,
        resolution,
        0.0,
        root.maxy,
        0.0,
        -resolution,
    ]

    bands = {band.get('description'): band for band in dataset['bands']}
    expected_names = ['count', *storage.get_derived_names()]
    with storage.open('r') as array:
        for name in expected_names:
            assert name in bands
            attr = array.schema.attr(name)
            assert bands[name]['type'] == _GDAL_DATA_TYPES[np.dtype(attr.dtype)]

            translate = _run(
                [
                    gdal_translate,
                    '-b',
                    str(bands[name]['band']),
                    '-of',
                    'MEM',
                    storage.config.tdb_dir,
                    f'/vsimem/{name}',
                ]
            )
            assert translate.returncode == 0, translate.stderr
