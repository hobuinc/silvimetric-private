"""Compare a masked Batch-v4 TileDB window with its unmasked predecessor.

Run with an environment containing the GDAL TileDB driver. A full-footprint
window verifies that water cells disappeared while every previously populated
non-water cell retained its count and mean-height value.
"""

from __future__ import annotations

import argparse
import json

import numpy as np
from osgeo import gdal


ROOT_X = -2493045.0
ROOT_Y = 3310005.0
PIXEL_SIZE = 20.0


def read_tiledb(uri: str, attribute: str, window: tuple[int, ...]):
    path = '/vsis3/' + uri[5:] if uri.startswith('s3://') else uri
    dataset = gdal.OpenEx(f'TILEDB:{path}:{attribute}', gdal.OF_RASTER)
    if dataset is None:
        raise RuntimeError(f'Cannot open {attribute} in {uri}')
    band = dataset.GetRasterBand(1)
    values = band.ReadAsArray(*window)
    if values is None or values.shape != (window[3], window[2]):
        raise RuntimeError(f'Incomplete {attribute} read in {uri}')
    return values, band.GetNoDataValue()


def read_mask(uri: str, window: tuple[int, ...]) -> np.ndarray:
    path = '/vsicurl/' + uri if uri.startswith('https://') else uri
    if uri.startswith('s3://'):
        path = '/vsis3/' + uri[5:]
    with gdal.config_option('GDAL_DISABLE_READDIR_ON_OPEN', 'EMPTY_DIR'):
        dataset = gdal.OpenEx(path, gdal.OF_RASTER)
    if dataset is None:
        raise RuntimeError(f'Cannot open mask {uri}')
    gt = dataset.GetGeoTransform()
    col_origin = (gt[0] - ROOT_X) / PIXEL_SIZE
    row_origin = (ROOT_Y - gt[3]) / PIXEL_SIZE
    if (
        gt[1] != PIXEL_SIZE or gt[5] != -PIXEL_SIZE
        or gt[2] != 0 or gt[4] != 0
        or col_origin != round(col_origin)
        or row_origin != round(row_origin)
    ):
        raise RuntimeError('Mask is not aligned to the canonical grid')
    xoff, yoff, width, height = window
    values = dataset.ReadAsArray(
        xoff - round(col_origin), yoff - round(row_origin), width, height
    )
    if values is None or values.shape != (height, width):
        raise RuntimeError('Mask does not cover the requested window')
    if np.any((values != 0) & (values != 1)):
        raise RuntimeError('Mask contains unknown/NoData pixels')
    return values


def is_nodata(values: np.ndarray, nodata) -> np.ndarray:
    missing = np.isnan(values)
    if nodata is not None:
        missing |= values == nodata
    return missing


def validate(
    baseline_uri: str, masked_uri: str, mask_uri: str,
    window: tuple[int, int, int, int],
    *, allow_land_mean_differences: bool = False,
) -> dict:
    water = read_mask(mask_uri, window) == 1
    old_count, _ = read_tiledb(baseline_uri, 'count', window)
    new_count, _ = read_tiledb(masked_uri, 'count', window)
    old_mean, _ = read_tiledb(baseline_uri, 'm_Z_mean', window)
    new_mean, new_nodata = read_tiledb(masked_uri, 'm_Z_mean', window)

    expected = np.where(water, 0, old_count)
    count_mismatch = (new_count != expected)
    land_populated = (old_count > 0) & ~water
    mean_mismatch = land_populated & ~np.isclose(
        new_mean, old_mean, rtol=0, atol=1e-5, equal_nan=True
    )
    water_mean_present = water & ~is_nodata(new_mean, new_nodata)
    result = {
        'window': list(window),
        'baseline_points': int(old_count.sum()),
        'masked_expected_points': int(expected.sum()),
        'masked_actual_points': int(new_count.sum()),
        'excluded_water_points': int(old_count[water].sum()),
        'excluded_populated_water_pixels': int(np.count_nonzero(water & (old_count > 0))),
        'retained_populated_land_pixels': int(np.count_nonzero(land_populated)),
        'count_mismatch_pixels': int(np.count_nonzero(count_mismatch)),
        'land_mean_mismatch_pixels': int(np.count_nonzero(mean_mismatch)),
        'water_mean_present_pixels': int(np.count_nonzero(water_mean_present)),
        'maximum_land_mean_difference': float(
            np.max(np.abs(new_mean[land_populated] - old_mean[land_populated]))
        ) if np.any(land_populated) else None,
    }
    failures = ('count_mismatch_pixels', 'water_mean_present_pixels')
    if not allow_land_mean_differences:
        failures += ('land_mean_mismatch_pixels',)
    if any(result[key] for key in failures):
        raise RuntimeError(json.dumps(result, indent=2))
    return result


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline-uri', required=True)
    parser.add_argument('--masked-uri', required=True)
    parser.add_argument('--mask-uri', required=True)
    parser.add_argument('--window', required=True,
                        help='Global pixel-space xoff,yoff,width,height')
    parser.add_argument(
        '--allow-land-mean-differences', action='store_true',
        help='For a changed HAG recipe, report land mean differences without '
             'treating them as a mask failure; counts and water NoData remain strict.',
    )
    args = parser.parse_args()
    window = tuple(int(value) for value in args.window.split(','))
    if len(window) != 4:
        parser.error('--window requires four comma-separated integers')
    gdal.UseExceptions()
    print(json.dumps(validate(
        args.baseline_uri, args.masked_uri, args.mask_uri, window,
        allow_land_mean_differences=args.allow_land_mean_differences,
    ), indent=2), flush=True)


if __name__ == '__main__':
    main()
