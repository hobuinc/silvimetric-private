"""A water mask must remove whole output pixels before any aggregation."""

from datetime import datetime

import numpy as np
from osgeo import gdal, osr
import pandas as pd
import pytest

from silvimetric.resources.config import ShatterConfig
from silvimetric.resources.water_mask import omit_water_pixels
from silvimetric.resources.usgs_albers import (
    USGS_ALBERS_TOP_LEFT_X,
    USGS_ALBERS_TOP_LEFT_Y,
)


def _mask(tmp_path, *, shift_x=0, water_value=1):
    path = tmp_path / 'water.tif'
    raster = gdal.GetDriverByName('GTiff').Create(
        str(path), 3, 2, 1, gdal.GDT_Byte,
    )
    raster.SetGeoTransform((
        USGS_ALBERS_TOP_LEFT_X + 100 * 20 + shift_x,
        20, 0, USGS_ALBERS_TOP_LEFT_Y - 200 * 20, 0, -20,
    ))
    srs = osr.SpatialReference()
    srs.ImportFromEPSG(5070)
    raster.SetProjection(srs.ExportToWkt())
    raster.SetMetadataItem('AREA_OR_POINT', 'Area')
    raster.GetRasterBand(1).SetNoDataValue(255)
    raster.GetRasterBand(1).WriteArray(np.array([
        [0, water_value, 0], [0, 0, 0],
    ], dtype=np.uint8))
    raster = None
    return str(path)


def _points():
    return pd.DataFrame({
        'xi': [100, 101, 101, 102],
        'yi': [200, 200, 200, 201],
        'Z': [2., 3., 4., 5.],
        'Intensity': [10, 20, 30, 40],
    })


def _omit(points, uri):
    return omit_water_pixels(
        points, uri,
        root_x=USGS_ALBERS_TOP_LEFT_X,
        root_y=USGS_ALBERS_TOP_LEFT_Y,
        resolution=20,
    )


def test_water_mask_removes_all_points_and_attributes_of_one_pixel(tmp_path):
    out = _omit(_points(), _mask(tmp_path))
    assert out[['xi', 'yi']].values.tolist() == [[100, 200], [102, 201]]
    assert out['Z'].tolist() == [2., 5.]
    assert out['Intensity'].tolist() == [10, 40]


def test_water_mask_rejects_misaligned_missing_and_unknown_pixels(tmp_path):
    with pytest.raises(ValueError, match='does not align'):
        _omit(_points(), _mask(tmp_path, shift_x=10))

    with pytest.raises(ValueError, match='does not cover'):
        _omit(pd.DataFrame({'xi': [99], 'yi': [200]}), _mask(tmp_path))

    with pytest.raises(ValueError, match='unknown/NoData'):
        _omit(_points(), _mask(tmp_path, water_value=255))


def test_water_mask_requires_usgs_albers_profile():
    with pytest.raises(ValueError, match='requires the USGS Albers profile'):
        ShatterConfig(
            tdb_dir='unused', filename='unused', date=datetime(2026, 1, 1),
            usgs_albers=False, water_mask_uri='water.tif',
        )
