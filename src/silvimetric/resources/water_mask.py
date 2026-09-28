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


class WaterMask:
    """One validated mask handle for pre-read checks and output filtering.

    Only a *wholly water* processing core may be skipped before PDAL. Mixed
    cores still need their complete collared point neighborhood for SMRF/HAG;
    masking individual source pixels in the reader would change land metrics.
    """

    def __init__(
        self, mask_uri: str, *, root_x: float, root_y: float,
        resolution: float,
    ) -> None:
        if not mask_uri:
            raise ValueError('mask_uri must not be empty')
        from osgeo import gdal, osr

        # The published COG has no PAM sidecar. Avoid S3 sibling probes.
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

        self.dataset = dataset
        self.band = band
        self.col_origin = _integral_offset(
            (gt[0] - root_x) / resolution, label='left edge'
        )
        self.row_origin = _integral_offset(
            (root_y - gt[3]) / resolution, label='top edge'
        )

    def core_is_fully_water(
        self, x1: int, y1: int, x2: int, y2: int
    ) -> bool:
        """Skip a read only when every output cell in its core is known water.

        An uncovered or unknown cell prevents the optimization. The ordinary
        post-read filter retains its existing error behavior for populated
        cells in such a region.
        """
        c0, c1 = x1 - self.col_origin, x2 - self.col_origin
        r0, r1 = y1 - self.row_origin, y2 - self.row_origin
        if c1 <= c0 or r1 <= r0:
            return False
        if (
            c0 < 0 or r0 < 0
            or c1 > self.dataset.RasterXSize
            or r1 > self.dataset.RasterYSize
        ):
            return False
        window = self.band.ReadAsArray(c0, r0, c1 - c0, r1 - r0)
        if window is None or window.shape != (r1 - r0, c1 - c0):
            raise RuntimeError('Could not read the requested water-mask window')
        return bool(np.all(window == 1))

    def omit_pixels(self, points: pd.DataFrame) -> pd.DataFrame:
        """Return only points whose output pixel is explicitly non-water."""
        if points.empty:
            return points
        columns = points['xi'].to_numpy(dtype=np.int64) - self.col_origin
        rows = points['yi'].to_numpy(dtype=np.int64) - self.row_origin
        if (
            (columns < 0).any() or (columns >= self.dataset.RasterXSize).any()
            or (rows < 0).any() or (rows >= self.dataset.RasterYSize).any()
        ):
            raise ValueError('Water mask does not cover every shatter output pixel')

        c0, c1 = int(columns.min()), int(columns.max()) + 1
        r0, r1 = int(rows.min()), int(rows.max()) + 1
        window = self.band.ReadAsArray(c0, r0, c1 - c0, r1 - r0)
        if window is None or window.shape != (r1 - r0, c1 - c0):
            raise RuntimeError('Could not read the requested water-mask window')
        values = window[rows - r0, columns - c0]
        if np.any((values != 0) & (values != 1)):
            raise ValueError('Water mask has unknown/NoData output pixels')
        return points.loc[values == 0].copy()


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
    An uncovered or unknown populated pixel is an error.
    """
    if points.empty:
        return points
    return WaterMask(
        mask_uri, root_x=root_x, root_y=root_y, resolution=resolution
    ).omit_pixels(points)
