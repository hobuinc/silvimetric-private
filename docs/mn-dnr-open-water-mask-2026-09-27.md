# Minnesota DNR open-water mask for Silvimetric

The immutable public artifact is a single-band cloud-optimized GeoTIFF:

- [water-mask.tif](https://silvimetric-public-masks-056176271256.s3.us-west-2.amazonaws.com/mn-dnr/open-water/20m-usgs-albers/2026-09-27/0f527d33c3f6/water-mask.tif)
- [manifest.json](https://silvimetric-public-masks-056176271256.s3.us-west-2.amazonaws.com/mn-dnr/open-water/20m-usgs-albers/2026-09-27/0f527d33c3f6/manifest.json)
- SHA-256: `0f527d33c3f61fbb6703f1e6ddd8750dc46abd4f964241d9e5f2d4752daa78e5`
- S3 VersionId: `Ify.r5zke2IosLwftJSg0WBjwKGbKqzF`

The bucket `silvimetric-public-masks-056176271256` is in `us-west-2`, has
versioning and SSE-S3 enabled, and grants public `GetObject` only beneath
`mn-dnr/`. It has no expiration rule.

The bucket's CORS rule permits browser `GET` and `HEAD` from
`https://eptium.com` and `https://www.eptium.com`, including range requests.
It exposes `Accept-Ranges`, `Content-Length`, `Content-Range`, `ETag`,
`Last-Modified`, and `x-amz-version-id`. A browser-style `OPTIONS` preflight
and an actual 1 KiB ranged `GET` were verified on 2026-09-27.

## Source and classification

The input is the Minnesota DNR's [DNR Hydro Features All polygon layer](https://enterprise.gisdata.mn.gov/aghost/rest/services/us_mn_state_dnr/water_dnr_hydrography/FeatureServer/1), which the [DNR Geospatial Water Resources Team](https://www.dnr.state.mn.us/watersheds/wrt.html) describes as its authoritative hydrography. The selected `wb_class` values are `Lake or Pond`, `Riverine polygon`, `Reservoir`, `Mine Pit Lake`, and `Mine Pit Lake (NF)`: 118,252 source polygons. We exclude wetlands, intermittent water, drained basins, and ambiguous artificial basins. The 2,281 `Island or Land` and `Riverine island` polygons are burned back to value 0 *after* water, correcting 196 otherwise-water raster cells. The source was fetched on 2026-09-27 at 0.1 m geometry precision in EPSG:5070. The manifest records both ID-set hashes and the exact class query.

This is a waterbody mask, not the DNR Public Waters Inventory's regulatory basin extent. The source contains delineations from multiple dates. A `0` means “not mapped as open water by these selected DNR classes,” **not** proof of dry land. Do not use this mask as a non-Minnesota land/water classification. Inspect shorelines and small ponds before applying it to a new collection.

## Grid and pixel convention

The mask uses horizontal EPSG:5070, 20 m square pixel-is-area cells and the
fixed Silvimetric USGS Albers root upper-left *outer edge*
`(-2493045, 3310005)`. Its cropped raster upper-left edge is
`(-91905, 2973605)`; geotransform is
`(-91905, 20, 0, 2973605, 0, -20)`; dimensions are
`49,439 × 35,540`. Those edges differ from the root by integer multiples of
20 m. Rasterization uses the GDAL pixel-center rule rather than
`ALL_TOUCHED`: a cell is water if its center falls within a selected water
polygon after islands are erased.

Byte band values are `0 = not source-mapped water`, `1 = water`, and
`255 = reserved NoData`. The raster currently has 246,146,512 water pixels,
1,510,915,548 zeros and no 255 pixels in its rectangular extent. Default
GDAL/PAM metadata embedded in the TIFF uses `WATER_MASK_*` keys for source,
class query, retrieval time, grid origin, pixel rule and attribution; there
is no required `.aux.xml` sidecar. It has seven nearest-neighbor overviews.

## `shatter` behavior

An initialized `--usgs_albers` database can use the mask with:

```shell
silvimetric --database <new-database-uri> shatter \
  --usgs_albers \
  --water-mask-uri 'https://silvimetric-public-masks-056176271256.s3.us-west-2.amazonaws.com/mn-dnr/open-water/20m-usgs-albers/2026-09-27/0f527d33c3f6/water-mask.tif' \
  --date 2021-01-01 \
  <point-cloud-or-pipeline>
```

After PDAL's collared read and HAG computation, `get_data` calculates output
pixel indices and discards *all points* in mask-1 pixels before point-attribute
aggregation and metric calculation. Consequently neither statistics nor
variable-length attributes are inserted for those cells; a fresh database
reads them as NoData. Water points remain available to the collared HAG
pipeline for neighboring land pixels. An absent, misaligned, non-5070,
unknown-valued, or out-of-coverage mask fails the work unit rather than
silently treating it as land. The mask URI is persisted in shatter history and
the resumable macro-v4 build signature.

This does **not** erase water cells already present in an existing database.
Use a fresh database or an explicit migration/cleanup for previously
unmasked builds. The published artifact is a source snapshot and should be
revalidated against DNR updates before future production collections.

## Validation

- GDAL's `validate_cloud_optimized_geotiff` reports a valid COG.
- The COG reports `AREA_OR_POINT=Area`, EPSG:5070, 20 m pixels, NoData 255,
  `LAYOUT=COG`, and the embedded provenance metadata.
- Anonymous `/vsicurl/` reading of the public object retained a known land
  pixel and rejected an adjacent known water pixel.
- `tests/test_water_mask.py` checks value and grid semantics; the
  `test_shatter_water_mask_omits_a_populated_pixel_before_aggregation`
  regression checks that a populated water pixel is absent from both the
  point-attribute aggregation and metrics, then runs a complete local
  `shatter`/`extract` cycle and checks that the exported water pixel is
  NoData. The relevant 2026-09-27 suite before that final assertion:
  86 passed, 2 skipped; the expanded end-to-end regression passed separately.

Rebuild locally with `tools/build_mn_dnr_water_mask.py <empty-output-dir>`.
The script downloads the selected DNR features by object ID, writes a
GeoPackage snapshot, rasterizes water then islands on the fixed grid,
embeds the provenance metadata and creates a DEFLATE COG. Its output manifest
records the full-file SHA-256; publish using a new content-addressed S3 key,
never replacing the artifact above.
