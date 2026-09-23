"""End-to-end checks for GDAL's TileDB raster driver."""

import json
import os
import copy
from math import floor
from pathlib import Path
import shutil
import subprocess
from datetime import datetime

import numpy as np
import pandas as pd
import pytest

from silvimetric import (
    Attributes,
    Bounds,
    ExtractConfig,
    Log,
    ShatterConfig,
    Storage,
    StorageConfig,
    shatter,
)
from silvimetric.resources.metrics.grid_metrics import get_grid_metrics
from silvimetric.resources.storage import _GDAL_DATA_TYPES
from silvimetric.resources.extents import Extents
from silvimetric.commands.extract import get_data as extract_data


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


def test_gdal_reads_albers_pixel_values_at_their_world_location(tmp_path):
    """An asymmetric window catches a physical X,Y / raster Y,X swap."""
    gdalinfo = _gdalinfo()
    gdal_translate = str(Path(gdalinfo).with_name('gdal_translate'))
    mean = copy.deepcopy(get_grid_metrics()['mean'])
    mean.attributes = [copy.deepcopy(Attributes['Z'])]
    storage = Storage.create(StorageConfig(
        tdb_dir=str(tmp_path / 'albers-values.tdb'),
        root=Bounds(1, 2, 3, 4),
        crs='EPSG:3857',
        resolution=20,
        xsize=64,
        ysize=64,
        attrs=[copy.deepcopy(Attributes['Z'])],
        metrics=[mean],
        usgs_albers=True,
    ))
    assert storage.config.dimension_order == 'YX'
    date = datetime(2026, 1, 1)
    storage.write(pd.DataFrame({
        'xi': [120, 121],
        'yi': [220, 220],
        'Z': [np.array([10.0]), np.array([20.0])],
        'm_Z_mean': [10.0, 20.0],
        'count': [1, 1],
        'shatter_process_num': [1, 1],
    }), (date, date))

    with storage.open('r') as array:
        assert [array.schema.domain.dim(i).name for i in range(2)] == ['Y', 'X']
        cells = array.query(attrs=['m_Z_mean'], coords=True).df[220:220, 120:121]
        assert cells['m_Z_mean'].tolist() == [10.0, 20.0]

    result = _run([
        gdal_translate, '-q', '-of', 'XYZ', '-srcwin', '120', '220', '2', '1',
        f'TILEDB:{storage.config.tdb_dir}:m_Z_mean', '/vsistdout/',
    ])
    assert result.returncode == 0, result.stderr
    values = [float(line.split()[-1]) for line in result.stdout.splitlines()]
    assert values == [10.0, 20.0]


@pytest.mark.parametrize('dimension_order', ['XY', 'YX'])
def test_extract_reads_legacy_and_gdal_dimension_orders(tmp_path, dimension_order):
    mean = copy.deepcopy(get_grid_metrics()['mean'])
    mean.attributes = [copy.deepcopy(Attributes['Z'])]
    uri = str(tmp_path / f'{dimension_order}.tdb')
    storage = Storage.create(StorageConfig(
        tdb_dir=uri,
        root=Bounds(1, 2, 3, 4),
        crs='EPSG:3857',
        resolution=20,
        xsize=64,
        ysize=64,
        attrs=[copy.deepcopy(Attributes['Z'])],
        metrics=[mean],
        usgs_albers=True,
        dimension_order=dimension_order,
    ))
    date = datetime(2026, 1, 1)
    storage.write(pd.DataFrame({
        'xi': [120, 121], 'yi': [220, 220],
        'Z': [np.array([10.0]), np.array([20.0])],
        'm_Z_mean': [10.0, 20.0],
        'count': [1, 1],
        'shatter_process_num': [1, 1],
    }), (date, date))
    if dimension_order == 'XY':
        # Pre-migration arrays do not have the field at all.
        metadata = json.loads(storage.get_metadata('config'))
        metadata.pop('dimension_order')
        with storage.open('w') as array:
            array.meta['config'] = json.dumps(metadata)

    reopened = Storage.from_db(uri)
    assert reopened.config.dimension_order == dimension_order
    root = reopened.config.root
    bounds = Bounds(
        root.minx + 120 * 20, root.maxy - 221 * 20,
        root.minx + 122 * 20, root.maxy - 220 * 20,
    )
    config = ExtractConfig(
        tdb_dir=uri, out_dir=str(tmp_path / f'{dimension_order}-extract'),
        attrs=reopened.config.attrs, metrics=reopened.config.metrics,
        bounds=bounds, date=(date, date),
    )
    cells = extract_data(config, reopened, Extents.from_sub(reopened, bounds))
    assert cells.reset_index().sort_values('X')['m_Z_mean'].tolist() == [10.0, 20.0]


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
