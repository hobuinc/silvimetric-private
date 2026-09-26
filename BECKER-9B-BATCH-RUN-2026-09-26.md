# Becker Batch-v4 9B pilot: sealed and public preview

RunId: `01702520-0d3e-41e4-9128-dc8b6fa0b133`  
Dataset: `MN_BeckerCo_1_2021`  
Region/profile: `us-west-2`, 20 m USGS Albers (`EPSG:5070+5703`), pixel-is-area  
Run cap: $200 admission reservation and RunId-filtered AWS Budget alert

The run sealed **8,524,575,503 points** in 750,637 populated cells. Its
256 planned active blocks became 294 published leaves after 38 recoverable
memory splits. Every leaf published, and the finalizer succeeded. The
canonical array has 251 active fragments after consolidation (294 before),
with a 300 MB fragment target. Finalization took 582.5 seconds, including
453.0 seconds of fragment consolidation. Both Batch compute environments
subsequently returned to zero desired vCPUs, with no active RunId-tagged EC2
instances.

The read-only quality gate [report](BECKER-9B-BATCH-QUALITY-2026-09-25.json)
passed: each immutable stage count matched its receipt; canonical and stage
count rasters were pixel-equal over the full footprint; no populated cell had
multiple stage owners; selected metric windows matched stages; GDAL reads
were PixelIsArea and matched TileDB; and `extract` TIFFs matched the dense
and boundary windows. A dense 152,068-point cell's stored mean and L-moments
also matched raw-Z recomputation within the stated tolerances.

The public preview is at:

- Manifest: `https://silvimetric-becker-9b-preview-056176271256.s3.us-west-2.amazonaws.com/published/01702520-0d3e-41e4-9128-dc8b6fa0b133/manifest.json`
- Array: `s3://silvimetric-becker-9b-preview-056176271256/published/01702520-0d3e-41e4-9128-dc8b6fa0b133/db/array`
- Example VRT: `https://silvimetric-becker-9b-preview-056176271256.s3.us-west-2.amazonaws.com/published/01702520-0d3e-41e4-9128-dc8b6fa0b133/gdal/bands/m_Z_mean.vrt`

The private source and public copy each have exactly 34,140 TileDB objects
and 14,373,560,870 bytes. The preview has VRTs for all 126 fixed-length
attributes. A no-credential GDAL read through the public `m_Z_mean.vrt`
returned an 8×8 window with 64 finite cells, 20 m pixels, PixelIsArea, and
the canonical upper-left outer edge `(-2493045, 3310005)`.

For a small COG sample with `gdal` and `libgdal-tiledb` installed:

```sh
export AWS_NO_SIGN_REQUEST=YES
export AWS_REGION=us-west-2
export TILEDB_VFS_S3_NO_SIGN_REQUEST=true
export TILEDB_VFS_S3_REGION=us-west-2
gdal_translate -srcwin 126312 32421 8 8 -of COG \
  /vsicurl/https://silvimetric-becker-9b-preview-056176271256.s3.us-west-2.amazonaws.com/published/01702520-0d3e-41e4-9128-dc8b6fa0b133/gdal/bands/m_Z_mean.vrt \
  becker-z-mean-sample.tif
```

## Preview-publication recovery

No `shatter` or TileDB rewrite was needed after sealing. The first preview
publisher attempt failed because the VRT tool rejected FUSION's valid
hyphenated attribute `m_Z_Strata-1`. Its name validator now accepts safe
ASCII hyphens while still rejecting path and URI delimiters; a regression
test and direct/VRT pixel comparison cover it. The next attempt encountered
a transient `curlCode: 6` DNS-resolution failure after many sequential
GDAL/TileDB attribute opens in one process. The publisher now generates
VRTs in eight-attribute subprocesses and retries only DNS failures in a
fresh process. It reused the already-exact private copy, generated and
validated all 126 VRTs, then wrote the manifest last. Only then was the
preview bucket switched to public read. Anonymous S3 listing, manifest GET,
and a GDAL/VRT raster read were verified.

## Cost status (not final billing)

The build admission ledger reserved **$195.85** of its approved $200, leaving
$4.15. These are conservative dispatch reservations, **not AWS charges**.
At 2026-09-26 12:24 UTC, Cost Explorer's RunId-tagged, estimated 2026-09-25
lines were $8.6863 EC2 compute, $0.6124 EC2-Other, and $0.2002 S3, totaling
about **$9.50**. The day remains estimated; later postings or adjustments
may change these numbers. Shared-bucket or untagged S3 activity and any
network usage not allocated to RunId are not included, and the new public
preview's future storage/access costs require separate observation. Do not
interpret the tagged estimate as the final total bill.
