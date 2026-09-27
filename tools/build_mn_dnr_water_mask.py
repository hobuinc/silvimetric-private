"""Build a pixel-is-area MN DNR water COG on Silvimetric's 20 m grid.

The feature snapshot is fetched by object ID from the public MN DNR ArcGIS
hydrography service. Only explicit open-water body classes are included; the
source's broad hydrography layer also contains wetlands, drained basins and
islands, which must not become water-mask pixels.
"""

from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
import math
from pathlib import Path
import time

from osgeo import gdal, ogr, osr
import requests


SERVICE = (
    'https://enterprise.gisdata.mn.gov/aghost/rest/services/'
    'us_mn_state_dnr/water_dnr_hydrography/FeatureServer/1'
)
SOURCE_METADATA = (
    'https://www.dnr.state.mn.us/watersheds/wrt.html'
)
WATER_CLASSES = (
    'Lake or Pond',
    'Riverine polygon',
    'Reservoir',
    'Mine Pit Lake',
    'Mine Pit Lake (NF)',
)
WHERE = 'wb_class IN (' + ','.join(repr(value) for value in WATER_CLASSES) + ')'
ISLAND_CLASSES = ('Island or Land', 'Riverine island')
ISLAND_WHERE = 'wb_class IN (' + ','.join(repr(value) for value in ISLAND_CLASSES) + ')'
ROOT_X = -2493045.0
ROOT_Y = 3310005.0
RESOLUTION = 20.0
PAGE_SIZE = 500


def request_json(session: requests.Session, params: dict) -> dict:
    for attempt in range(6):
        try:
            response = session.get(
                SERVICE + '/query', params={**params, 'f': 'geojson'
                                            if params.get('returnGeometry')
                                            else 'json'}, timeout=180,
            )
            response.raise_for_status()
            payload = response.json()
            if 'error' in payload:
                raise RuntimeError(f'ArcGIS query error: {payload["error"]}')
            return payload
        except (requests.RequestException, ValueError, RuntimeError):
            if attempt == 5:
                raise
            time.sleep(min(30, 2 ** attempt))
    raise AssertionError('unreachable')


def fetch_page(session: requests.Session, ids: list[int]) -> list[dict]:
    result = request_json(session, {
        'objectIds': ','.join(map(str, ids)),
        'outFields': 'objectid,wb_class',
        'returnGeometry': 'true',
        'outSR': 5070,
        'geometryPrecision': 1,  # 0.1 m in target CRS
    })
    features = result.get('features', [])
    received = {int(feature['properties']['objectid']) for feature in features}
    if received == set(ids) and not result.get('exceededTransferLimit'):
        return features
    if len(ids) == 1:
        raise RuntimeError(f'ArcGIS did not return object ID {ids[0]}')
    midpoint = len(ids) // 2
    return fetch_page(session, ids[:midpoint]) + fetch_page(session, ids[midpoint:])


def download_vectors(session: requests.Session, output: Path) -> dict:
    response = request_json(session, {
        'where': WHERE, 'returnIdsOnly': 'true',
    })
    ids = sorted(int(value) for value in response['objectIds'])
    if not ids:
        raise RuntimeError('No MN DNR open-water polygons were returned')
    id_digest = hashlib.sha256(','.join(map(str, ids)).encode()).hexdigest()
    srs = osr.SpatialReference()
    srs.ImportFromEPSG(5070)
    driver = ogr.GetDriverByName('GPKG')
    ds = driver.CreateDataSource(str(output))
    if ds is None:
        raise RuntimeError(f'Cannot create {output}')
    layer = ds.CreateLayer('mn_dnr_water', srs, ogr.wkbUnknown)
    layer.CreateField(ogr.FieldDefn('source_oid', ogr.OFTInteger64))
    layer.CreateField(ogr.FieldDefn('wb_class', ogr.OFTString))
    count = 0
    for start in range(0, len(ids), PAGE_SIZE):
        chunk = ids[start:start + PAGE_SIZE]
        features = fetch_page(session, chunk)
        layer.StartTransaction()
        for feature in features:
            geometry = ogr.CreateGeometryFromJson(json.dumps(feature['geometry']))
            if geometry is None or geometry.IsEmpty():
                raise RuntimeError(f'Empty geometry for {feature["id"]}')
            item = ogr.Feature(layer.GetLayerDefn())
            item.SetField('source_oid', int(feature['properties']['objectid']))
            item.SetField('wb_class', feature['properties']['wb_class'])
            item.SetGeometry(geometry)
            if layer.CreateFeature(item) != ogr.OGRERR_NONE:
                raise RuntimeError(f'Failed inserting object ID {feature["id"]}')
            count += 1
        layer.CommitTransaction()
        if start == 0 or (start // PAGE_SIZE + 1) % 10 == 0:
            print(f'MN DNR polygons: {count:,}/{len(ids):,}', flush=True)
    extent = layer.GetExtent()  # xmin, xmax, ymin, ymax
    ds = None
    if count != len(ids):
        raise RuntimeError(f'Incomplete source snapshot: {count} != {len(ids)}')
    return {
        'source_service': SERVICE,
        'source_metadata': SOURCE_METADATA,
        'source_where': WHERE,
        'water_classes': list(WATER_CLASSES),
        'source_feature_count': count,
        'source_object_ids_sha256': id_digest,
        'source_retrieved_utc': datetime.now(timezone.utc).isoformat(),
        'source_geometry_precision_m': 0.1,
        'source_bounds_5070': list(extent),
    }


def download_islands(session: requests.Session, vectors: Path) -> dict:
    """Add explicit DNR land/island polygons to erase overlapping water."""
    dataset = ogr.Open(str(vectors), update=1)
    existing = dataset.GetLayerByName('mn_dnr_islands')
    if existing is not None:
        ids = sorted(int(item.GetField('source_oid')) for item in existing)
        dataset = None
        return {
            'island_where': ISLAND_WHERE,
            'island_feature_count': len(ids),
            'island_object_ids_sha256': hashlib.sha256(
                ','.join(map(str, ids)).encode()
            ).hexdigest(),
            'island_classes': list(ISLAND_CLASSES),
        }
    dataset = None
    response = request_json(session, {
        'where': ISLAND_WHERE, 'returnIdsOnly': 'true',
    })
    ids = sorted(int(value) for value in response['objectIds'])
    if not ids:
        raise RuntimeError('No MN DNR island polygons were returned')
    dataset = ogr.Open(str(vectors), update=1)
    srs = osr.SpatialReference()
    srs.ImportFromEPSG(5070)
    layer = dataset.CreateLayer('mn_dnr_islands', srs, ogr.wkbUnknown)
    layer.CreateField(ogr.FieldDefn('source_oid', ogr.OFTInteger64))
    layer.CreateField(ogr.FieldDefn('wb_class', ogr.OFTString))
    count = 0
    for start in range(0, len(ids), PAGE_SIZE):
        features = fetch_page(session, ids[start:start + PAGE_SIZE])
        layer.StartTransaction()
        for feature in features:
            geometry = ogr.CreateGeometryFromJson(json.dumps(feature['geometry']))
            if geometry is None or geometry.IsEmpty():
                raise RuntimeError(f'Empty island geometry for {feature["id"]}')
            item = ogr.Feature(layer.GetLayerDefn())
            item.SetField('source_oid', int(feature['properties']['objectid']))
            item.SetField('wb_class', feature['properties']['wb_class'])
            item.SetGeometry(geometry)
            if layer.CreateFeature(item) != ogr.OGRERR_NONE:
                raise RuntimeError(f'Failed inserting island {feature["id"]}')
            count += 1
        layer.CommitTransaction()
    dataset = None
    if count != len(ids):
        raise RuntimeError(f'Incomplete island snapshot: {count} != {len(ids)}')
    print(f'MN DNR islands: {count:,}', flush=True)
    return {
        'island_where': ISLAND_WHERE,
        'island_feature_count': count,
        'island_object_ids_sha256': hashlib.sha256(
            ','.join(map(str, ids)).encode()
        ).hexdigest(),
        'island_classes': list(ISLAND_CLASSES),
    }


def grid_bounds(extent: list[float]) -> tuple[float, float, float, float, int, int]:
    minx, maxx, miny, maxy = extent
    xmin = ROOT_X + math.floor((minx - ROOT_X) / RESOLUTION) * RESOLUTION
    xmax = ROOT_X + math.ceil((maxx - ROOT_X) / RESOLUTION) * RESOLUTION
    ymax = ROOT_Y - math.floor((ROOT_Y - maxy) / RESOLUTION) * RESOLUTION
    ymin = ROOT_Y - math.ceil((ROOT_Y - miny) / RESOLUTION) * RESOLUTION
    width = round((xmax - xmin) / RESOLUTION)
    height = round((ymax - ymin) / RESOLUTION)
    return xmin, ymin, xmax, ymax, width, height


def make_cog(
    vectors: Path, cog: Path, manifest: dict, *, resume_raster: bool = False
) -> None:
    xmin, ymin, xmax, ymax, width, height = grid_bounds(
        manifest['source_bounds_5070']
    )
    work = (
        vectors.parent / 'mn-dnr-open-water-20m-usgs-albers.rasterize.tif'
        if resume_raster else cog.with_suffix('.rasterize.tif')
    )
    gdal.UseExceptions()
    if resume_raster:
        ds = gdal.Open(str(work), gdal.GA_Update)
        if ds is None:
            raise RuntimeError(f'No reusable raster at {work}')
        applied_islands = ds.GetMetadataItem(
            'ISLAND_FEATURE_COUNT', 'SILVIMETRIC_WATER_MASK'
        )
        if applied_islands is None:
            applied_islands = ds.GetMetadataItem(
                'WATER_MASK_ISLAND_FEATURE_COUNT'
            )
        if applied_islands != str(manifest.get('island_feature_count', 0)):
            raise RuntimeError(
                'Reusable raster does not record the current island erase; '
                'rerasterize instead of using --resume-raster'
            )
    else:
        options = gdal.RasterizeOptions(
            format='GTiff', outputType=gdal.GDT_Byte,
            outputBounds=(xmin, ymin, xmax, ymax), width=width, height=height,
            outputSRS='EPSG:5070', layers=['mn_dnr_water'], burnValues=[1],
            initValues=[0], noData=255,
            creationOptions=['TILED=YES', 'BLOCKXSIZE=512', 'BLOCKYSIZE=512',
                             'COMPRESS=DEFLATE', 'BIGTIFF=IF_SAFER',
                             'NUM_THREADS=ALL_CPUS'],
        )
        print(f'Rasterizing {width:,} × {height:,} pixel grid', flush=True)
        ds = gdal.Rasterize(str(work), str(vectors), options=options)
        island_count = manifest.get('island_feature_count', 0)
        if island_count:
            print(f'Erasing {island_count:,} island polygons from water', flush=True)
            vector_ds = ogr.Open(str(vectors))
            island_layer = vector_ds.GetLayerByName('mn_dnr_islands')
            if gdal.RasterizeLayer(ds, [1], island_layer, burn_values=[0]) != 0:
                raise RuntimeError('Failed to erase island polygons')
            vector_ds = None
    ds.SetMetadataItem('AREA_OR_POINT', 'Area')
    ds.SetMetadataItem('TIFFTAG_IMAGEDESCRIPTION',
                       'MN DNR explicit open-water polygons on the fixed '
                       'Silvimetric USGS Albers 20 m pixel-is-area grid.')
    metadata = {
        'SOURCE_SERVICE': SERVICE,
        'SOURCE_METADATA': SOURCE_METADATA,
        'SOURCE_WHERE': WHERE,
        'SOURCE_RETRIEVED_UTC': manifest['source_retrieved_utc'],
        'SOURCE_FEATURE_COUNT': str(manifest['source_feature_count']),
        'SOURCE_OBJECT_IDS_SHA256': manifest['source_object_ids_sha256'],
        'ISLAND_FEATURE_COUNT': str(manifest.get('island_feature_count', 0)),
        'ISLAND_OBJECT_IDS_SHA256': manifest.get('island_object_ids_sha256', ''),
        'ISLAND_RULE': 'island/land polygons overwrite water with 0',
        'SOURCE_GEOMETRY_PRECISION_M': '0.1',
        'CRS': 'EPSG:5070',
        'PIXEL_SIZE_M': '20',
        'ROOT_UPPER_LEFT_OUTER_EDGE': f'{ROOT_X},{ROOT_Y}',
        'PIXEL_RULE': 'water if the 20 m pixel center is inside a source polygon',
        'PIXEL_VALUES': '0=not source-mapped water;1=water;255=nodata/reserved',
        'EXCLUDED_CLASSES': 'wetland,drained,intermittent,ambiguous basin',
        'ATTRIBUTION': 'Minnesota Department of Natural Resources',
    }
    ds.SetMetadata(metadata, 'SILVIMETRIC_WATER_MASK')
    # The COG driver preserves GDAL's default-domain PAM metadata, but not
    # arbitrary auxiliary domains. Prefix the keys so all provenance survives
    # inside the one public TIFF, without an .aux.xml sidecar.
    for key, value in metadata.items():
        ds.SetMetadataItem('WATER_MASK_' + key, value)
    band = ds.GetRasterBand(1)
    band.SetDescription('water_mask')
    band.SetNoDataValue(255)
    ds.FlushCache()
    ds = None
    print('Converting to cloud-optimized GeoTIFF', flush=True)
    result = gdal.Translate(
        str(cog), str(work),
        options=gdal.TranslateOptions(format='COG', creationOptions=[
            'COMPRESS=DEFLATE', 'BLOCKSIZE=512',
            'OVERVIEW_RESAMPLING=NEAREST', 'NUM_THREADS=ALL_CPUS',
            'BIGTIFF=IF_SAFER',
        ]),
    )
    result = None
    source = gdal.Open(str(cog))
    if any(
        source.GetMetadataItem('WATER_MASK_' + key) != value
        for key, value in metadata.items()
    ):
        raise RuntimeError('COG lost the water-mask provenance metadata')
    if source.GetRasterBand(1).GetNoDataValue() != 255:
        raise RuntimeError('COG lost the reserved NoData value')
    if source.GetGeoTransform() != (xmin, 20.0, 0.0, ymax, 0.0, -20.0):
        raise RuntimeError('COG grid differs from the canonical 20 m grid')
    manifest.update({
        'raster_bounds_5070': [xmin, ymin, xmax, ymax],
        'raster_size': [width, height],
        'raster_transform': list(source.GetGeoTransform()),
        'cog_bytes': cog.stat().st_size,
        'cog_sha256': file_sha256(cog),
        'cog_uri_local': str(cog),
    })
    source = None


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output_dir', type=Path)
    parser.add_argument('--resume-raster', action='store_true',
                        help='Reuse an already downloaded GPKG and raster.')
    parser.add_argument('--augment-islands', action='store_true',
                        help='Reuse a downloaded water GPKG, add islands, rerasterize.')
    parser.add_argument('--cog-name', default='mn-dnr-open-water-20m-usgs-albers.tif')
    args = parser.parse_args()
    output_dir = args.output_dir.resolve()
    output_dir.mkdir(parents=True, exist_ok=True)
    vectors = output_dir / 'mn-dnr-open-water-source.gpkg'
    cog = output_dir / args.cog_name
    manifest_path = cog.with_suffix('.json')
    if args.resume_raster or args.augment_islands:
        if cog.exists() or manifest_path.exists():
            raise FileExistsError('COG or manifest already exists')
        dataset = ogr.Open(str(vectors))
        if dataset is None:
            raise FileNotFoundError(vectors)
        extent = dataset.GetLayerByName('mn_dnr_water').GetExtent()
        source = gdal.Open(str(output_dir / 'mn-dnr-open-water-20m-usgs-albers.rasterize.tif'))
        old = source.GetMetadata('SILVIMETRIC_WATER_MASK')
        manifest = {
            'source_service': SERVICE,
            'source_metadata': SOURCE_METADATA,
            'source_where': WHERE,
            'water_classes': list(WATER_CLASSES),
            'source_feature_count': int(old['SOURCE_FEATURE_COUNT']),
            'source_object_ids_sha256': old['SOURCE_OBJECT_IDS_SHA256'],
            'source_retrieved_utc': old['SOURCE_RETRIEVED_UTC'],
            'source_geometry_precision_m': 0.1,
            'source_bounds_5070': list(extent),
        }
        source = None
        dataset = None
        if args.augment_islands:
            with requests.Session() as session:
                manifest.update(download_islands(session, vectors))
    else:
        if any(path.exists() for path in (vectors, cog, manifest_path)):
            raise FileExistsError('Output already exists; choose a fresh directory')
        with requests.Session() as session:
            manifest = download_vectors(session, vectors)
            manifest.update(download_islands(session, vectors))
    make_cog(vectors, cog, manifest, resume_raster=args.resume_raster)
    manifest_path.write_text(json.dumps(manifest, indent=2) + '\n')
    print(json.dumps(manifest, indent=2), flush=True)


if __name__ == '__main__':
    main()
