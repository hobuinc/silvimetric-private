# Becker incremental water-mask canary — 2026-09-29

## Outcome

The two adjacent, disjoint Batch-v4 phases sealed into **one private USGS
Albers TileDB array**. The first phase remained unchanged after the second
phase appended and after collection-level consolidation. This is a small
recoverability and water-mask proof, **not** the planned 20-billion-point run.

| Phase | RunId | Pixel bounds `[x1,y1,x2,y2)` | Retained points | Populated land cells | Masked source-water points |
| --- | --- | --- | ---: | ---: | ---: |
| Base | `7acd5784-7370-4800-91d1-a96b3d975a2c` | `[125832,32370,125864,32402)` | 14,336,815 | 910 | 1,202,729 |
| Adjacent append | `942b4a4f-b3cb-4649-98d6-8c167892fe5a` | `[125864,32370,125896,32402)` | 10,099,247 | 473 | 3,689,749 |
| Combined | | `[125832,32370,125896,32402)` | **24,436,062** | **1,383** | **4,892,478** |

Canonical array:
`s3://sm-becker-056176271256-c0f24425-9fcc-4464-a7fe-820ebeb7e8be/builds/incremental-canary-7acd5784/array`.
The immutable first-phase snapshot and its consolidation benchmark are under
`s3://sm-becker-056176271256-c0f24425-9fcc-4464-a7fe-820ebeb7e8be/builds/incremental-canary-7acd5784/snapshot-phase1/`.

Both phases used the same pinned water mask, whose SHA-256 is
`0f527d33c3f61fbb6703f1e6ddd8750dc46abd4f964241d9e5f2d4752daa78e5`,
and the same collared PDAL pipeline SHA-256
`8738c30a1a044f769724418dffd4334349ff93d123c0eb46c33802923c23f5d9`.
The pipeline treats source water as ground **only** for HAG computation;
water-mask cells do not contribute metrics and read as NoData.

## Recovery exercised

The append phase initially ran from Silvimetric commit `ce22bd54` and failed
on fully water-covered blocks: its empty stage was read across the entire
CONUS-sized dense domain, causing a repeatable 282 GiB allocation attempt.
Splitting could not solve a fixed full-domain read. The run was paused with
six blocks published and its receipt ledger preserved.

Silvimetric commit `8ba0527` bounds stage reads to the block's owned core.
The regression test creates an empty stage with a full-CONUS schema and reads
only a small core. The local suite passed: 67 tests, 3 skipped. The replacement
ARM64 image is
`056176271256.dkr.ecr.us-west-2.amazonaws.com/silvimetric-batch-v4-canary-942b4a4f@sha256:9145efa0300fd49790a0fa30bd891754fb846eedc6b0fa44d2b4e6bf601f055b`.
An EC2 preflight passed on that image. A previously failed all-water leaf then
staged and published with zero points/cells. The remaining unfinished leaves
resumed from their existing ledger: no completed block was rerun. All 28 active
append leaves published, including 21 zero-point leaves.

The CloudFormation run budget and durable admission maximum were raised from
$10 to the operator-approved **$30**. The original S3 request was not
overwritten: the immutable amendment is
`s3://sm-becker-056176271256-c0f24425-9fcc-4464-a7fe-820ebeb7e8be/builds/942b4a4f-b3cb-4649-98d6-8c167892fe5a/batch-v4-request-cap-30.json`.
The operational control-plane change is in sm-distributed commit `de95cf8`.

## Verification and maintenance

Independent GDAL comparisons to the earlier unmasked Becker canonical array
found **zero count-mismatch pixels** and **zero m_Z_mean values in water
pixels** for either phase, both before and after final consolidation. Land
`m_Z_mean` values differ from the older unmasked build in 393 and 352 cells,
by at most 0.0191 m and 0.0329 m respectively; the processing/HAG recipe and
macro layout differ, so these comparisons are not a test of exact metric
equivalence with that older build.

The Phase 2 quality gate passed for all 28 immutable stages: stage counts and
five sampled metrics matched the canonical array, sampled GDAL TileDB reads
matched, `extract` matched, and a 75,720-point cell's raw Z values reproduced
its stored mean and L-moments. Its local report is
`/private/tmp/becker-incremental-canary-phase2-quality.json`.

Collection-level consolidation verified the two sealed, non-overlapping
phase footprints and their point counts, then reduced the canonical array
from **19 to 1 active fragment** in **64.28 seconds**. It rechecked both
phase point counts afterward. The independent GDAL water/count comparisons
also passed after consolidation.

## Cost interpretation and next step

The base phase consumed $7.16 and the append phase $17.48 of their respective
admission reservations, or **$24.64 combined**. These are conservative
dispatch reservations, **not posted AWS charges**. The append phase finished
with $12.52 of its $30 cap unreserved. Reconcile actual tagged EC2, EBS,
S3, and network charges when billing posts before using this canary as a
per-point price estimate.

The next build can treat the first 20B as the canonical base, retain an
immutable copy for the isolated consolidation benchmark, then append another
disjoint phase. Keep the pinned mask, source recipe, resume receipts,
zero-work EC2 preflight, and bounded empty-stage regression in the release
gate. The 20B run itself has not been launched.
