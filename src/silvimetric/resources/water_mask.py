"""Read an aligned water-mask COG and omit water pixels before aggregation."""

from __future__ import annotations

import math

import numpy as np
import pandas as pd


def _gdal_uri(uri: str) -> str:
    if uri.startswith('s3://'):
        return '/vsis3/' + uri[5:]
    if uri.startswith(('http://', 'https://')):
        return '/vsicurl/' + uri
    return uri


def _integral_offset(value: float, *, label: str) -> int:
    rounded = round(value)
    if not math.isclose(value, rounded, rel_tol=0.0, abs_tol=1e-7):
        raise ValueError(f'Water mask {label} does not align to the storage grid')
    return int(rounded)


def omit_water_pixels(
    points: pd.DataFrame,
    mask_uri: str,
    *,
    root_x: float,
    root_y: float,
    resolution: float,
) -> pd.DataFrame:
    """Return only points whose 20 m grid pixel is explicitly non-water.

    Pixel values are 0=not source-mapped water, 1=water, 255=unknown/NoData.
    An uncovered or unknown pixel is an error: silently treating missing mask
    coverage as land would make a resumable build non-reproducible.
    """
    if points.empty:
        return points
    if not mask_uri:
        raise ValueError('mask_uri must not be empty')

    from osgeo import gdal, osr

    # The public, self-contained COG has no PAM sidecar. Avoid a series of
    # expensive S3 403/404 probes for .aux.xml and related sibling names.
    with gdal.config_option('GDAL_DISABLE_READDIR_ON_OPEN', 'EMPTY_DIR'):
        dataset = gdal.OpenEx(
            _gdal_uri(mask_uri), gdal.OF_RASTER | gdal.OF_READONLY
        )
    if dataset is None:
        raise ValueError(f'Cannot open water mask {mask_uri!r}')
    if dataset.RasterCount != 1:
        raise ValueError('Water mask must have exactly one raster band')
    gt = dataset.GetGeoTransform()
    if (
        not math.isclose(gt[1], resolution, abs_tol=1e-9)
        or not math.isclose(gt[5], -resolution, abs_tol=1e-9)
        or not math.isclose(gt[2], 0, abs_tol=1e-9)
        or not math.isclose(gt[4], 0, abs_tol=1e-9)
    ):
        raise ValueError('Water mask must have the database pixel size and axis')
    srs = dataset.GetSpatialRef()
    expected = osr.SpatialReference()
    expected.ImportFromEPSG(5070)
    if srs is None or not bool(srs.IsSame(expected)):
        raise ValueError('Water mask must use horizontal EPSG:5070')
    if dataset.GetMetadataItem('AREA_OR_POINT') != 'Area':
        raise ValueError('Water mask must use pixel-is-area georeferencing')
    band = dataset.GetRasterBand(1)
    if band.DataType != gdal.GDT_Byte or band.GetNoDataValue() != 255:
        raise ValueError('Water mask must be Byte with NoData=255')

    col_origin = _integral_offset(
        (gt[0] - root_x) / resolution, label='left edge'
    )
    row_origin = _integral_offset(
        (root_y - gt[3]) / resolution, label='top edge'
    )
    columns = points['xi'].to_numpy(dtype=np.int64) - col_origin
    rows = points['yi'].to_numpy(dtype=np.int64) - row_origin
    if (
        (columns < 0).any() or (columns >= dataset.RasterXSize).any()
        or (rows < 0).any() or (rows >= dataset.RasterYSize).any()
    ):
        raise ValueError('Water mask does not cover every shatter output pixel')

    c0, c1 = int(columns.min()), int(columns.max()) + 1
    r0, r1 = int(rows.min()), int(rows.max()) + 1
    window = band.ReadAsArray(c0, r0, c1 - c0, r1 - r0)
    if window is None or window.shape != (r1 - r0, c1 - c0):
        raise RuntimeError('Could not read the requested water-mask window')
    values = window[rows - r0, columns - c0]
    if np.any((values != 0) & (values != 1)):
        raise ValueError('Water mask has unknown/NoData output pixels')
    return points.loc[values == 0].copy()
