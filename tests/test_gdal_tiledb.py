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
    Bounds,
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


def test_gdal_cli_reads_usgs_albers_profile(tmp_path):
    """Direct attribute opens retain the global Albers pixel-is-area grid."""
    gdalinfo = _gdalinfo()
    storage = Storage.create(
        StorageConfig(
            tdb_dir=str(tmp_path / 'usgs-albers.tdb'),
            root=Bounds(1, 2, 3, 4),
            crs='EPSG:3857',
            resolution=20,
            xsize=64,
            ysize=64,
            attrs=[copy.deepcopy(Attributes['Z'])],
            metrics=[get_grid_metrics()['mean']],
            usgs_albers=True,
        )
    )
    result = _run(
        [gdalinfo, '-json', f'TILEDB:{storage.config.tdb_dir}:m_Z_mean']
    )
    assert result.returncode == 0, result.stderr
    dataset = json.loads(result.stdout)
    assert dataset['geoTransform'] == [
        -2493045.0, 20.0, 0.0, 3310005.0, 0.0, -20.0
    ]
    assert 'Conus Albers' in dataset['coordinateSystem']['wkt']
    assert 'NAVD88' in dataset['coordinateSystem']['wkt']
    assert dataset['metadata']['']['AREA_OR_POINT'] == 'Area'


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
    assert 'coordinateSystem' in dataset

    # The explicit TileDB attribute path loads a separate PAM subdataset.
    # It must carry the same spatial reference and logical (unpadded) extent.
    attribute_uri = f'TILEDB:{storage.config.tdb_dir}:m_Z_mean'
    attribute_result = _run([gdalinfo, '-json', attribute_uri])
    assert attribute_result.returncode == 0, attribute_result.stderr
    attribute_dataset = json.loads(attribute_result.stdout)
    assert attribute_dataset['size'] == dataset['size']
    assert attribute_dataset['geoTransform'] == dataset['geoTransform']
    assert attribute_dataset['coordinateSystem'] == dataset['coordinateSystem']
    assert len(attribute_dataset['bands']) == 1
    assert attribute_dataset['bands'][0]['description'] == 'm_Z_mean'

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
