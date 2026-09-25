"""Read-only quality gate for a sealed macro-v4 Batch canonical array.

Run with an environment containing Silvimetric, TileDB-Py, and boto3. GDAL's
TileDB plugin may live in a separate Conda environment; pass that interpreter
with ``--gdal-python``. No S3 objects or TileDB metadata are modified.
"""

from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
from urllib.parse import urlparse

import numpy as np


METRICS = (
    'm_Z_mean',
    'm_Z_p95',
    'm_Z_l4',
    'm_Z_lkurtosis',
    'm_Intensity_mean',
)
GDAL_METRICS = ('count', 'm_Z_mean', 'm_Z_l4')
EXTRACT_METRICS = ('m_Z_mean', 'm_Z_l4')


def s3_parts(uri: str) -> tuple[str, str]:
    parsed = urlparse(uri)
    if parsed.scheme != 's3' or not parsed.netloc or not parsed.path.strip('/'):
        raise ValueError(f'Expected an S3 object/prefix URI, got {uri!r}')
    return parsed.netloc, parsed.path.strip('/')


def half_open_indices(bounds, root, resolution: float) -> tuple[int, int, int, int]:
    """Convert projected outer pixel edges to X/Y half-open grid indices."""
    minx, miny, maxx, maxy = map(float, bounds)
    root_minx, _root_miny, _root_maxx, root_maxy = map(float, root)
    raw = (
        (minx - root_minx) / resolution,
        (root_maxy - maxy) / resolution,
        (maxx - root_minx) / resolution,
        (root_maxy - miny) / resolution,
    )
    indices = tuple(round(value) for value in raw)
    if any(abs(value - index) > 1e-6 for value, index in zip(raw, indices)):
        raise ValueError(f'Bounds are not aligned to the output grid: {bounds}')
    x1, y1, x2, y2 = indices
    if x1 >= x2 or y1 >= y2:
        raise ValueError(f'Empty index rectangle: {indices}')
    return indices


def window_around(x: int, y: int, footprint, side: int = 8):
    fx1, fy1, fx2, fy2 = footprint
    side = min(side, fx2 - fx1, fy2 - fy1)
    x1 = min(max(x - side // 2, fx1), fx2 - side)
    y1 = min(max(y - side // 2, fy1), fy2 - side)
    return x1, y1, x1 + side, y1 + side


def select_windows(count: np.ndarray, owners: np.ndarray, footprint):
    """Choose dense, cross-stage-boundary, and occupied outer-edge samples."""
    fx1, fy1, fx2, fy2 = footprint
    if count.shape != (fy2 - fy1, fx2 - fx1) or not np.any(count):
        raise ValueError('The bounded canonical footprint has no populated cells.')
    dense_y, dense_x = np.unravel_index(np.argmax(count), count.shape)
    dense = (fx1 + int(dense_x), fy1 + int(dense_y))

    # An owner change between occupied neighbors is a true processing-block
    # boundary, not merely the edge of a receipt bounding box.
    right = (owners[:, :-1] != owners[:, 1:]) & (owners[:, :-1] > 0) & (owners[:, 1:] > 0)
    down = (owners[:-1, :] != owners[1:, :]) & (owners[:-1, :] > 0) & (owners[1:, :] > 0)
    candidates = []
    for yy, xx in np.argwhere(right):
        candidates.append((int(count[yy, xx]) + int(count[yy, xx + 1]), int(xx), int(yy)))
    for yy, xx in np.argwhere(down):
        candidates.append((int(count[yy, xx]) + int(count[yy + 1, xx]), int(xx), int(yy)))
    if not candidates:
        raise ValueError('No occupied cells straddle different published stages.')
    _, bx, by = max(candidates)
    boundary = (fx1 + bx, fy1 + by)

    yy, xx = np.nonzero(count)
    distance = np.minimum.reduce((xx, yy, count.shape[1] - xx - 1, count.shape[0] - yy - 1))
    edge_at = int(np.argmin(distance))
    edge = (fx1 + int(xx[edge_at]), fy1 + int(yy[edge_at]))
    return {
        name: window_around(x, y, footprint)
        for name, (x, y) in [('dense', dense), ('boundary', boundary), ('edge', edge)]
    }, dense


def _get_json(s3, bucket: str, key: str):
    return json.loads(s3.get_object(Bucket=bucket, Key=key)['Body'].read())


def load_receipts(s3, ledger_uri: str, build_id: str, canonical_uri: str):
    bucket, prefix = s3_parts(ledger_uri)
    keys = [
        entry['Key']
        for page in s3.get_paginator('list_objects_v2').paginate(
            Bucket=bucket, Prefix=f'{prefix}/blocks/'
        )
        for entry in page.get('Contents', [])
    ]
    published_keys = [key for key in keys if '-published-' in key.rsplit('/', 1)[-1]]
    build_keys = [
        key for key in keys if '/__build__/' in key and (
            '-build_started-' in key or '-build_sealed-' in key
        )
    ]
    if not published_keys or len(build_keys) < 2:
        raise ValueError('Build is missing published-stage or seal receipts.')
    with ThreadPoolExecutor(max_workers=12) as pool:
        documents = list(pool.map(lambda key: _get_json(s3, bucket, key), published_keys + build_keys))
    published = documents[:len(published_keys)]
    build = documents[len(published_keys):]
    latest = {}
    for receipt in published:
        if receipt['state'] != 'published':
            raise ValueError('Unexpected published receipt state.')
        block = receipt['block_id']
        if block not in latest or receipt['created_at_ns'] > latest[block]['created_at_ns']:
            latest[block] = receipt
    manifest = next((record for record in build if record['state'] == 'build_started'), None)
    seal = max(
        (record for record in build if record['state'] == 'build_sealed'),
        key=lambda record: record['created_at_ns'],
        default=None,
    )
    if manifest is None or seal is None:
        raise ValueError('Build manifest or seal receipt is missing.')
    if (manifest['details']['build_id'] != build_id
            or manifest['details']['canonical_uri'].rstrip('/') != canonical_uri.rstrip('/')
            or seal['details']['build_id'] != build_id):
        raise ValueError('Ledger identity does not match the requested build/array.')
    if len(latest) != int(seal['details']['published_block_count']):
        raise ValueError('Published block count does not match the seal.')
    receipt_points = sum(int(item['details']['point_count']) for item in latest.values())
    if receipt_points != int(seal['details']['point_count']):
        raise ValueError('Published point count does not match the seal.')
    return list(latest.values()), seal['details']


def read_attrs(array, window, attrs):
    x1, y1, x2, y2 = window
    values = array.query(attrs=list(attrs))[y1:y2, x1:x2]
    expected_shape = (y2 - y1, x2 - x1)
    if any(value.shape != expected_shape for value in values.values()):
        raise ValueError(f'TileDB returned a wrong shape for {window}.')
    return values


def _gdal_worker(args):
    from osgeo import gdal

    gdal.UseExceptions()
    uri = args.canonical_uri.replace('s3://', '/vsis3/', 1)
    x1, y1, x2, y2 = args.window
    raster = gdal.OpenEx(f'TILEDB:{uri}:{args.attribute}')
    if raster is None:
        raise RuntimeError(f'GDAL did not open {args.attribute}.')
    values = raster.GetRasterBand(1).ReadAsArray(x1, y1, x2 - x1, y2 - y1)
    print(json.dumps({
        'values': values.tolist(),
        'geotransform': raster.GetGeoTransform(),
        'area_or_point': raster.GetMetadataItem('AREA_OR_POINT'),
        'raster_size': [raster.RasterXSize, raster.RasterYSize],
    }, allow_nan=True))


def check_gdal(python: str, driver_path: str, script: Path, uri: str, window, attr, expected, root, resolution):
    env = os.environ.copy()
    env['GDAL_DRIVER_PATH'] = driver_path
    command = [python, str(script), '--gdal-worker', '--canonical-uri', uri,
               '--attribute', attr, '--window', *map(str, window)]
    result = subprocess.run(command, env=env, capture_output=True, text=True, check=True)
    document = json.loads(result.stdout)
    actual = np.asarray(document['values'])
    if not np.allclose(actual, expected, rtol=0, atol=1e-5, equal_nan=True):
        raise AssertionError(f'GDAL {attr} differs from TileDB in {window}.')
    transform = document['geotransform']
    if not np.allclose(transform, (root[0], resolution, 0, root[3], 0, -resolution)):
        raise AssertionError(f'GDAL geotransform is wrong: {transform}')
    if document['area_or_point'] != 'Area':
        raise AssertionError('GDAL does not report PixelIsArea.')
    return {'attribute': attr, 'cells': int(actual.size), 'matching': True}


def check_extract(uri, window, storage, expected, output_dir: Path):
    from osgeo import gdal
    from silvimetric import Bounds, ExtractConfig
    from silvimetric.commands.extract import extract

    x1, y1, x2, y2 = window
    root = storage.config.root
    resolution = storage.config.resolution
    bounds = Bounds(
        root.minx + x1 * resolution,
        root.maxy - y2 * resolution,
        root.minx + x2 * resolution,
        root.maxy - y1 * resolution,
    )
    attrs = [a for a in storage.config.attrs if a.name == 'Z']
    metrics = [m for m in storage.config.metrics if m.name in {'mean', 'l4'}]
    if len(attrs) != 1 or len(metrics) != 2:
        raise ValueError('The array lacks the Z/mean/l4 extract definitions.')
    config = ExtractConfig(
        tdb_dir=uri, out_dir=str(output_dir), bounds=bounds,
        attrs=attrs, metrics=metrics,
    )
    extract(config)
    result = []
    for attr in EXTRACT_METRICS:
        raster = gdal.Open(str(output_dir / f'{attr}.tif'))
        if raster is None:
            raise AssertionError(f'extract did not create {attr}.tif')
        values = raster.GetRasterBand(1).ReadAsArray()
        if values.shape != expected[attr].shape or not np.allclose(
            values, expected[attr], rtol=0, atol=1e-5, equal_nan=True
        ):
            raise AssertionError(f'extract {attr} differs from TileDB in {window}.')
        transform = raster.GetGeoTransform()
        correct = (bounds.minx, resolution, 0, bounds.maxy, 0, -resolution)
        if not np.allclose(transform, correct) or raster.GetMetadataItem('AREA_OR_POINT') != 'Area':
            raise AssertionError(f'extract {attr} has wrong pixel-is-area georeferencing.')
        result.append({'attribute': attr, 'cells': int(values.size), 'matching': True})
    return result


def validate(args):
    import boto3
    import tiledb
    from silvimetric import Storage
    from silvimetric.resources.metrics.l_moments import lmom4

    storage = Storage.from_db(args.canonical_uri)
    if not storage.config.usgs_albers or storage.config.dimension_order != 'YX':
        raise ValueError('Expected a YX, USGS-Albers canonical array.')
    root = storage.config.root.get()
    resolution = storage.config.resolution
    receipts, seal = load_receipts(
        boto3.client('s3'), args.ledger_uri, args.build_id, args.canonical_uri
    )
    stage_windows = [
        half_open_indices(receipt['details']['bounds'], root, resolution)
        for receipt in receipts
    ]
    footprint = (
        min(window[0] for window in stage_windows),
        min(window[1] for window in stage_windows),
        max(window[2] for window in stage_windows),
        max(window[3] for window in stage_windows),
    )
    fx1, fy1, fx2, fy2 = footprint
    expected = np.zeros((fy2 - fy1, fx2 - fx1), dtype=np.uint64)
    ownership = np.zeros(expected.shape, dtype=np.uint16)
    owner_id = np.zeros(expected.shape, dtype=np.uint16)
    ctx = Storage.get_tdb_context(storage)
    for number, (receipt, window) in enumerate(zip(receipts, stage_windows), start=1):
        stage_uri = receipt['details']['stage_uri']
        with tiledb.open(stage_uri, mode='r', ctx=ctx) as stage:
            counts = read_attrs(stage, window, ('count',))['count']
        x1, y1, x2, y2 = window
        target = np.s_[y1 - fy1:y2 - fy1, x1 - fx1:x2 - fx1]
        if int(np.sum(counts, dtype=np.uint64)) != int(receipt['details']['point_count']):
            raise AssertionError(f'Stage count differs from receipt: {receipt["block_id"]}')
        occupied = counts > 0
        if int(np.count_nonzero(occupied)) != int(receipt['details']['cell_count']):
            raise AssertionError(f'Stage cell count differs from receipt: {receipt["block_id"]}')
        expected[target] += counts
        ownership[target] += occupied
        owner_id[target][occupied] = number
        if number % 10 == 0:
            print(f'Checked {number}/{len(receipts)} immutable stages', file=sys.stderr, flush=True)
    if np.any(ownership > 1):
        raise AssertionError(f'{np.count_nonzero(ownership > 1)} cells have multiple stage owners.')
    with tiledb.open(args.canonical_uri, mode='r', ctx=ctx) as canonical:
        actual = read_attrs(canonical, footprint, ('count',))['count']
        if not np.array_equal(actual, expected):
            raise AssertionError(f'{np.count_nonzero(actual != expected)} canonical count cells differ from stages.')
        point_count = int(np.sum(actual, dtype=np.uint64))
        if point_count != int(seal['point_count']):
            raise AssertionError('Canonical point total differs from seal.')
        windows, dense_cell = select_windows(actual, owner_id, footprint)
        window_results = {}
        for name, window in windows.items():
            canonical_data = read_attrs(canonical, window, ('count', *METRICS))
            x1, y1, x2, y2 = window
            stage_expected = {
                metric: np.zeros((y2 - y1, x2 - x1), dtype=canonical_data[metric].dtype)
                for metric in METRICS
            }
            for receipt, stage_window in zip(receipts, stage_windows):
                ix1, iy1 = max(x1, stage_window[0]), max(y1, stage_window[1])
                ix2, iy2 = min(x2, stage_window[2]), min(y2, stage_window[3])
                if ix1 >= ix2 or iy1 >= iy2:
                    continue
                with tiledb.open(receipt['details']['stage_uri'], mode='r', ctx=ctx) as stage:
                    stage_data = read_attrs(stage, (ix1, iy1, ix2, iy2), ('count', *METRICS))
                local = np.s_[iy1 - y1:iy2 - y1, ix1 - x1:ix2 - x1]
                occupied = stage_data['count'] > 0
                for metric in METRICS:
                    stage_expected[metric][local][occupied] = stage_data[metric][occupied]
            populated = canonical_data['count'] > 0
            for metric in METRICS:
                if not np.array_equal(
                    canonical_data[metric][populated], stage_expected[metric][populated],
                    equal_nan=True,
                ):
                    raise AssertionError(f'{name}: canonical {metric} differs from immutable stages.')
            gdal_results = [
                check_gdal(
                    args.gdal_python, args.gdal_driver_path, Path(__file__),
                    args.canonical_uri, window, metric, canonical_data[metric], root, resolution,
                )
                for metric in GDAL_METRICS
            ]
            window_results[name] = {
                'indices': window,
                'populated_cells': int(np.count_nonzero(populated)),
                'stage_metric_matches': list(METRICS),
                'gdal': gdal_results,
            }
            if name in ('dense', 'boundary'):
                with tempfile.TemporaryDirectory(prefix=f'sm-qa-{name}-') as directory:
                    window_results[name]['extract'] = check_extract(
                        args.canonical_uri, window, storage, canonical_data, Path(directory)
                    )
        dense_x, dense_y = dense_cell
        dense_data = read_attrs(
            canonical, (dense_x, dense_y, dense_x + 1, dense_y + 1),
            ('Z', 'count', 'm_Z_mean', 'm_Z_l4', 'm_Z_lkurtosis'),
        )
        z = np.ma.array(dense_data['Z'][0, 0], dtype=np.float64)
        l1, l2, _l3, l4 = lmom4(z)
        observed_l4 = float(dense_data['m_Z_l4'][0, 0])
        observed_lkurt = float(dense_data['m_Z_lkurtosis'][0, 0])
        observed_mean = float(dense_data['m_Z_mean'][0, 0])
        if len(z) != int(dense_data['count'][0, 0]) or not np.isclose(
            observed_l4, l4, rtol=0, atol=2e-5
        ) or not np.isclose(observed_lkurt, l4 / l2, rtol=0, atol=2e-5) or not np.isclose(
            observed_mean, z.mean(), rtol=0, atol=2e-5
        ):
            raise AssertionError('Dense-cell raw-Z metric recomputation failed.')
    return {
        'status': 'passed',
        'build_id': args.build_id,
        'canonical_uri': args.canonical_uri,
        'ledger_uri': args.ledger_uri,
        'published_stages': len(receipts),
        'point_count': point_count,
        'populated_cells': int(np.count_nonzero(actual)),
        'footprint_indices': footprint,
        'overlapping_stage_cells': int(np.count_nonzero(ownership > 1)),
        'dense_cell': {
            'indices': [dense_x, dense_y], 'points': int(len(z)),
            'stored_mean': observed_mean, 'recomputed_mean': float(z.mean()),
            'stored_l4': observed_l4, 'recomputed_l4': float(l4),
            'stored_lkurtosis': observed_lkurt, 'recomputed_lkurtosis': float(l4 / l2),
        },
        'windows': window_results,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--canonical-uri', required=True)
    parser.add_argument('--ledger-uri')
    parser.add_argument('--build-id')
    parser.add_argument('--gdal-python')
    parser.add_argument('--gdal-driver-path')
    parser.add_argument('--window', nargs=4, type=int)
    parser.add_argument('--attribute')
    parser.add_argument('--gdal-worker', action='store_true')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    if args.gdal_worker:
        _gdal_worker(args)
        return
    if not all((args.ledger_uri, args.build_id, args.gdal_python, args.gdal_driver_path)):
        parser.error('validation requires ledger URI, build ID, GDAL Python, and driver path')
    result = validate(args)
    serialized = json.dumps(result, indent=2, sort_keys=True)
    if args.output:
        args.output.write_text(serialized + '\n')
    print(serialized)


if __name__ == '__main__':
    main()
