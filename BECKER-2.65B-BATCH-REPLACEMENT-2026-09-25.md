# Becker 2.65B Batch replacement: corrected publish and L-moments

The replacement build `c1b65682-0081-4e14-bbc9-f19274502e64` reached
`SEALED` on 2026-09-25 at 00:22:07 UTC. Unlike its invalid predecessor, its
canonical count equals the immutable-stage receipts and an independent GDAL
read. It remains a calibration product; wider metric and scientific review
should precede public distribution.

## Identity and execution

- Canonical array:
  `s3://sm-becker-056176271256-c0f24425-9fcc-4464-a7fe-820ebeb7e8be/db/batch-v4-albers-yx-row-c1b65682`
- Immutable source and control receipts:
  `s3://sm-becker-056176271256-c0f24425-9fcc-4464-a7fe-820ebeb7e8be/builds/c1b65682-0081-4e14-bbc9-f19274502e64/`
- Private Silvimetric source commit: `7048476d1c06fdcac7a85b22d315d228dd0d2915`.
  `sm-distributed` request commit: `ef02a6c23d5ed540a507f4e53127c2246249d8b1`.
  ARM64 ECR image digest:
  `sha256:cba20f8ece73c484ed7ca2aad636ee7177b798ca2edd98f561f24be20ad26bd7`.
- The Becker boundary SHA-256 (`72747aa15a2fe5250b3d232911119ac86b75408234b365b574a6f255df26297a`),
  input PDAL pipeline SHA-256 (`2b551389c78a0625bf03c5c9a44c97e0553d67940d39bf160560a6794b08b961`),
  and processing-grid SHA-256 (`574dfc6571384ad79571f100f0df16a06889860cd49984e6f87aa564e43cdb5d`)
  match the predecessor. The plan used 56 root blocks, a 34-by-34-cell
  processing read, 20 m halo, and no density-based processing-grid split.
- The zero-work deployed-image canary passed on `aarch64`, Python 3.14.7,
  PDAL-Python 3.5.5, and TileDB 0.36.1. It checked PDAL reprojection,
  Albers pixel indexing, and GDAL's pixel-is-area TileDB georeferencing.
  One initial stage published 28,449,899 points, exactly matching the same
  stage in the predecessor; the remaining 55 roots were released afterward.
  Five roots needed adaptive recovery, leaving 61 published leaf blocks.
- The run's synchronous admission ledger reserved **$42.10 of $50**, leaving
  $7.90. This is not a posted AWS bill. Publisher jobs had a 600-second
  timeout and a 15-cent reservation; the stage timeout was 1,800 seconds.

## Quality checks

| Check | Result |
| --- | ---: |
| Published immutable-stage point total | 2,647,868,723 |
| Pre- and post-consolidation canonical `count` gates | 2,647,868,723 |
| Independent GDAL TileDB `count` read, 484 × 478 cells | 2,647,868,723 |
| Populated cells in independent GDAL read | 214,282 |
| Finalizer | Succeeded; approximately 241 s Batch runtime |

The GDAL read used the bounded window at global indices `(125639, 31985)`
with size `484 × 478`. It reported the USGS Albers outer-edge transform
`(-2493045, 20, 0, 3310005, 0, -20)` and `AREA_OR_POINT=Area`. The
predecessor's canonical array had only 2,451,307,927 points and 198,597
populated cells; the corrected publisher retained the 196,560,796 points and
15,685 cells previously erased by rectangular zero-fill.

A 72,999-point pixel at `(xi=125654, yi=32427)` exercises the old L-moment
overflow threshold. The new array stores `m_Z_l4=-0.4980237186` and
`m_Z_lkurtosis=-0.1304920763`; recalculating from the pixel's raw `Z` array
gives `-0.4980237264` and `-0.1304920754`, respectively. The L4 absolute
difference is `7.8e-9`, consistent with storage as `float32`.

A 10-by-10-cell `extract` at Albers bounds
`(20015, 2661345, 20215, 2661545)` produced `m_Z_mean` and `m_Z_l4`
GeoTIFFs with 20 m pixel-is-area georeferencing. Their dense-pixel values
were `8.0235080719` and `-0.4980237186`. The relevant local regression suite
passed 97 tests with two skips; Batch control-plane tests passed 34 tests.

At the final check, both Batch compute environments requested zero vCPUs and
EC2 reported no pending, running, stopping, or stopped instances. The failed
empty new-stack attempt was removed after the account's five-VPC/internet-
gateway quota prevented its creation. The idle prior Batch stack was updated
with the new image and `BillingRunId`, and a request preflight now rejects a
stack whose S3 bucket or billing identity does not match the new build.

## Remaining checks

Cost Explorer had not yet posted a per-run bill for this new RunId. Query
posted EC2, EBS, S3 request, storage, and network charges later; do not treat
the $42.10 admission reservation as spend. Before publishing this output for
collaborators, review additional metrics and representative spatial windows,
including high-density and processing-boundary cells, against the expected
scientific behavior.
