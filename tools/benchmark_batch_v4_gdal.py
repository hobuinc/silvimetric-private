"""Generate named-attribute TileDB VRTs and benchmark bounded GDAL reads.

Use the Conda environment containing GDAL and libgdal-tiledb. This tool only
reads the canonical array and writes local VRT/JSON files; it never scans the
whole array or modifies S3. Each timed read uses a fresh process. Example::

    python tools/benchmark_batch_v4_gdal.py make-vrt \
      --canonical-uri s3://bucket/db/array --output-dir /tmp/becker-vrt \
      --attribute count --attribute m_Z_mean
    python tools/benchmark_batch_v4_gdal.py benchmark \
      --canonical-uri s3://bucket/db/array --vrt-dir /tmp/becker-vrt \
      --attribute count --attribute m_Z_mean \
      --window dense:125980:32422:8:8 --window broad:125950:32400:256:256 \
      --repeats 3 --output-json /tmp/becker-gdal-benchmark.json
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import statistics
import subprocess
import sys
from time import perf_counter
from urllib.parse import urlparse
from xml.etree import ElementTree as ET


def named_attribute_uri(canonical_uri: str, attribute: str) -> str:
    parsed = urlparse(canonical_uri)
    if parsed.scheme != 's3' or not parsed.netloc or not parsed.path.strip('/'):
        raise ValueError('Canonical URI must be an s3://bucket/array URI.')
    # FUSION-aligned metric names include ``Strata-1``. Keep URI delimiters
    # and path syntax excluded while accepting that legitimate hyphen.
    if re.fullmatch(r'[A-Za-z0-9_][A-Za-z0-9_-]*', attribute) is None:
        raise ValueError(f'Invalid TileDB attribute: {attribute!r}')
    return f'TILEDB:/vsis3/{parsed.netloc}{parsed.path.rstrip("/")}:{attribute}'


def fix_vrt_source(xml: str, source_name: str) -> str:
    """Preserve the complete named-attribute URI discarded by GDAL Translate."""
    document = ET.fromstring(xml)
    names = document.findall('.//SourceFilename')
    if len(names) != 1:
        raise RuntimeError(f'Expected one VRT source, found {len(names)}')
    # GDAL Translate otherwise emits only s3://bucket/array here, losing the
    # named attribute and /vsis3 driver path required for direct reads.
    names[0].text = source_name
    names[0].set('relativeToVRT', '0')
    ET.indent(document)
    return ET.tostring(document, encoding='unicode') + '\n'


def make_vrt(canonical_uri: str, attribute: str, output: Path) -> Path:
    from osgeo import gdal

    gdal.UseExceptions()
    source_name = named_attribute_uri(canonical_uri, attribute)
    source = gdal.OpenEx(source_name, gdal.OF_RASTER)
    if source is None or source.RasterCount != 1:
        raise RuntimeError(f'Expected one raster band at {source_name}')
    vrt = gdal.Translate('', source, format='VRT')
    if vrt is None:
        raise RuntimeError(f'Could not create VRT for {source_name}')
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(fix_vrt_source(vrt.GetMetadata('xml:VRT')[0], source_name))
    return output


def parse_window(value: str) -> tuple[str, tuple[int, int, int, int]]:
    name, x, y, width, height = value.split(':')
    window = tuple(map(int, (x, y, width, height)))
    if not name or any(v < 0 for v in window[:2]) or any(v <= 0 for v in window[2:]):
        raise ValueError(f'Invalid positive raster window: {value}')
    return name, window


def read_worker(path: str, window: tuple[int, int, int, int]) -> dict:
    from osgeo import gdal

    gdal.UseExceptions()
    started = perf_counter()
    source = gdal.OpenEx(path, gdal.OF_RASTER)
    if source is None or source.RasterCount != 1:
        raise RuntimeError(f'Could not open one-band raster {path}')
    x, y, width, height = window
    if x + width > source.RasterXSize or y + height > source.RasterYSize:
        raise ValueError(f'Window {window} exceeds {source.RasterXSize}x{source.RasterYSize}')
    values = source.GetRasterBand(1).ReadAsArray(x, y, width, height)
    if values is None:
        raise RuntimeError(f'GDAL returned no pixels for {path} in {window}')
    return {
        'seconds': round(perf_counter() - started, 6),
        'sha256': hashlib.sha256(values.tobytes()).hexdigest(),
        'dtype': str(values.dtype),
        'shape': list(values.shape),
        'geotransform': list(source.GetGeoTransform()),
        'area_or_point': source.GetMetadataItem('AREA_OR_POINT'),
        'nodata': source.GetRasterBand(1).GetNoDataValue(),
    }


def benchmark(args) -> dict:
    records = []
    for attribute in args.attribute:
        direct = named_attribute_uri(args.canonical_uri, attribute)
        vrt = str(Path(args.vrt_dir) / f'{attribute}.vrt')
        if not Path(vrt).is_file():
            raise FileNotFoundError(vrt)
        for window_spec in args.window:
            window_name, window = parse_window(window_spec)
            for repeat in range(args.repeats):
                pair = {}
                # Alternate order to expose cache/order effects; each read
                # still starts a clean Python/GDAL process.
                for mode in (('direct', 'vrt') if repeat % 2 == 0 else ('vrt', 'direct')):
                    path = direct if mode == 'direct' else vrt
                    output = subprocess.check_output([
                        sys.executable, __file__, '_read', path,
                        *map(str, window),
                    ], text=True)
                    pair[mode] = json.loads(output)
                compared = ('sha256', 'dtype', 'shape', 'geotransform', 'area_or_point', 'nodata')
                if any(pair['direct'][key] != pair['vrt'][key] for key in compared):
                    raise AssertionError(
                        f'GDAL direct/VRT mismatch: {attribute} {window_name} repeat={repeat}'
                    )
                records.append({
                    'attribute': attribute, 'window_name': window_name,
                    'window': window, 'repeat': repeat, **pair,
                })
                print(
                    f'{attribute} {window_name} #{repeat + 1}: '
                    f'direct={pair["direct"]["seconds"]:.3f}s '
                    f'vrt={pair["vrt"]["seconds"]:.3f}s equal',
                    file=sys.stderr,
                )
    groups = {}
    for attribute in args.attribute:
        for window_spec in args.window:
            name, _ = parse_window(window_spec)
            group = [r for r in records if r['attribute'] == attribute and r['window_name'] == name]
            groups[f'{attribute}:{name}'] = {
                mode: round(statistics.median(r[mode]['seconds'] for r in group), 6)
                for mode in ('direct', 'vrt')
            }
    return {'canonical_uri': args.canonical_uri, 'records': records, 'median_seconds': groups}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    vrt = sub.add_parser('make-vrt')
    vrt.add_argument('--canonical-uri', required=True)
    vrt.add_argument('--output-dir', required=True)
    vrt.add_argument('--attribute', action='append', required=True)
    bench = sub.add_parser('benchmark')
    bench.add_argument('--canonical-uri', required=True)
    bench.add_argument('--vrt-dir', required=True)
    bench.add_argument('--attribute', action='append', required=True)
    bench.add_argument('--window', action='append', required=True)
    bench.add_argument('--repeats', type=int, default=3)
    bench.add_argument('--output-json', required=True)
    worker = sub.add_parser('_read')
    worker.add_argument('path')
    worker.add_argument('x', type=int)
    worker.add_argument('y', type=int)
    worker.add_argument('width', type=int)
    worker.add_argument('height', type=int)
    args = parser.parse_args()
    if args.command == 'make-vrt':
        for attribute in args.attribute:
            print(make_vrt(args.canonical_uri, attribute, Path(args.output_dir) / f'{attribute}.vrt'))
    elif args.command == '_read':
        print(json.dumps(read_worker(args.path, (args.x, args.y, args.width, args.height))))
    else:
        if args.repeats < 1:
            parser.error('--repeats must be positive')
        result = benchmark(args)
        Path(args.output_json).write_text(json.dumps(result, indent=2) + '\n')
        print(json.dumps(result['median_seconds'], indent=2))


if __name__ == '__main__':
    main()
