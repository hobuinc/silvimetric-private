# Silvimetric / Minnesota processing recap

Last updated: 2026-09-09

This is the continuation record for the private Silvimetric work on
`codex/gdal-metadata-uint8`. It covers the AWS/Dask investigation maintained
in the companion private `sm-distributed` repository and the resulting
Silvimetric changes. It intentionally contains no credentials.

## Executive summary

We have moved from a small local/S3 compatibility smoke test to a
correctness-tested, S3-native, distributed `macro-v3-stage-push` path. The
path reads collared EPT/COPC windows with PDAL on Dask workers, writes and
consolidates temporary TileDB arrays on worker-local disk, then publishes
validated, immutable TileDB arrays to S3 and registers them in a TileDB group.
It avoids the original scheduler-side DataFrame concatenation and the prior
one-small-array-per-macro S3 layout.

The work is not yet a production full-Minnesota execution system. The two most
important remaining gates are a current-code, large arm64 fleet calibration
and an efficient group-level `info`/full-coverage `extract` path. Cost Explorer
has historically not delivered trustworthy `RunId`-attributed EC2 costs, so
all large-run amounts below are usage-model estimates rather than invoices.

The best current estimate for building `MN_BeckerCo_1_2021` is **about
$85 on demand for shatter plus the first month of output storage**, with a
recommended authorization ceiling of **$125**. That assumes the 320-vCPU
arm64 fleet is configured to use its worker capacity and that its 1.65B-point
pilot throughput scales reasonably. The expected `shatter` wall time is
**about 4.7 hours under ideal scaling; plan 5–7.5 hours** until a Becker
calibration proves otherwise. A whole-database `extract` is deliberately not
included in this estimate: it is not an efficient production validation
operation with the current group reader.

## What has been done

### 1. S3 and distributed smoke-test foundation

The companion AWS repository established the safety and observability
framework before larger runs:

- all compute, buckets, and configuration are in `us-west-2`;
- each meaningful run receives a `RunId`, change note, immutable source and
  runner hashes, a manifest, and S3 object/request/byte evidence;
- initial smoke runs had a $50 ceiling and project budget alerts went to
  `sivimetric@hobu.co`;
- CloudFormation creates encrypted EBS, tightly scoped instance roles, a
  private worker/scheduler path, self-termination, and a public Dask dashboard
  only while the scheduler is running;
- cleanup was corrected so the Auto Scaling group's minimum, maximum, and
  desired capacity are all set to zero, preventing replacement-worker churn;
- completed smoke stacks/instances are removed while evidence is retained for
  billing and validation.

The first successful local/S3 smoke run processed 1,407,473 Arrowhead B2
points and produced 104 matching extracted rasters. The serial EC2 and
two-worker Dask repeats subsequently proved that the same storage workflow
works remotely.

### 2. Legacy PDAL pipeline and collection dates

The initial equivalent-looking local/S3 runs had matching point counts but
different HAG-derived values. Investigation proved that the variability was
not S3 persistence: SMRF and HAG-NN were receiving points in nondeterministic
order. The accepted pipeline now performs the legacy processing sequence:

1. `readers.ept` with bounded `bounds` and controlled request concurrency;
2. normalize classification and invalid return-number fields;
3. stable-sort `X`, `Y`, `Z`, return fields, and intensity;
4. `filters.smrf` then `filters.hag_nn`;
5. ferry `HeightAboveGround` into `Z`; and
6. crop to the approved transformed geometry.

The stable sort preserves the source point population while making the
order-sensitive stages reproducible. It resolved the local/S3 discrepancies:
raw vectors, metric data, and all 104 extraction rasters matched exactly.
Collection dates are sourced from the USGS WESM records, rather than STAC
publication dates.

### 3. Dask scaling and failure handling

The B22 (`MN_UpperMissRiver_2_B22`) 4 km2 reference crop processed
129,088,506 points and produced identical 104-raster signatures at every
accepted scale point:

| Dask worker hosts | Elapsed | Speedup vs. two workers | Result |
|---:|---:|---:|---|
| 2 | 537.266 s | 1.00x | Accepted |
| 4 | 303.207 s | 1.77x | Accepted |
| 8 | 196.687 s | 2.73x | Accepted |
| 16 | 144.928 s | 3.71x | Accepted |

The 16-worker scheduler required 16 GiB; the 8 GiB master terminated under
load. A later 80-local-worker single-host run was only 1.11x faster than the
comparable 16-local-worker result, demonstrating that simply increasing Dask
process count does not solve the PDAL/TileDB bottleneck.

The code now fails closed for Dask task errors, avoids serializing live TileDB
readers into Dask tasks, and constrains TileDB context concurrency. Failed
partial work is no longer accepted as a valid performance point.

### 4. Macro-v2 and macro-v3 stage-and-push

`leaf-v1` accumulated task DataFrames on the driver, which made the driver a
memory and data-transfer bottleneck. `macro-v2` moved metric calculation and
writing to workers. `macro-v3-stage-push` then changed the durable-output
layout:

```text
collared EPT/COPC read -> worker-local partial TileDB arrays
                      -> local merge/consolidation
                      -> new committed S3 TileDB array
                      -> TileDB group member
```

The design was necessary because copying a local TileDB directory directly to
S3 copied fragment objects but not the object-store commit records TileDB
requires. The corrected publisher materializes a new immutable S3 array using
TileDB, reopens it, confirms readable fragments, and only then adds it to the
group.

On the B22 macro-v3 architecture probe, all results had the expected
129,088,506 points and passed a fresh S3 `extract`:

| Layout | Shatter wall time | Extract wall time | Published arrays | Lesson |
|---|---:|---:|---:|---|
| One S3 array per macro | 161.3 s | 473.9 s | 100 | Write fast, read fan-out unacceptable. |
| Serial 3x3 local blocks | 440.0 s | 56.1 s | 12 | Fewer arrays help reads but serialize work. |
| Concurrent owner-pinned local partials, then 3x3 merge | 203.1 s | 52.7 s | 12 | Accepted balanced design. |

The production-quality behavior is the last row: compact spatial shards,
local consolidation, and only lightweight task-result records moved through
Dask.

### 5. Adaptive shard planning and performance instrumentation

`macro-v3` now uses a bounded coarse PDAL read plus a few native-resolution
calibration samples to estimate point density per spatial window. It supplies
both `bounds` and a coarser `resolution` to EPT/COPC. For `readers.tindex`, it
pre-filters with bounds and forwards bounds/resolution to the selected COPC
reader through `reader_args`.

The planner retains its calibration samples, point multiplier, density ceiling,
planned shards, estimated load per worker, actual point counts, published
bytes, and phase timings in the run manifest. It assigns the largest estimated
blocks first to the least-loaded Dask worker, avoiding a dense final block
stranding a fleet.

The accepted 99.33 km2 B22 arm64 pilot was the most useful current calibration:

| Measurement | Value |
|---|---:|
| Source points processed (including the normal reader collar) | 1,650,681,447 |
| Fleet | 16 `m9g.xlarge` workers + 1 `m9g.xlarge` scheduler |
| Worker configuration | 4 threads, 12 GiB managed memory per worker |
| Published readable shards | 59 |
| Published TileDB bytes | 4,123,624,653 (3.84 GiB) |
| Output density | 2.33 GiB/Gpoint (2.50 bytes/point) |
| End-to-end wall time | 1,228.136 s |
| Fully allocated pilot usage model | $1.36 |

The corresponding x86 pilot took 1,525.6 s and modeled at $2.15. Arm64 had
the same point count and passed its own S3 extraction, but differed from x86
in 100 of 104 raster fingerprints. The project has accepted those small
platform numerical variations for performance investigation, but every arm64
production run must retain platform/dependency provenance and use an explicit
tolerance-based validation policy.

### 6. TileDB, GDAL, and extraction compatibility

The Silvimetric code changes now include:

- a TileDB group-aware output model, group-member discovery, and group-aware
  `extract`; `info` aggregates member histories but still takes its
  configuration/attribute description from a member rather than a group-level
  catalog;
- dense-array physical padding to complete TileDB tile blocks, while retaining
  the logical Silvimetric bounds as the externally visible extent;
- GDAL-compatible TileDB schema metadata, date representation, and `_gdal`
  metadata written as `UINT8` bytes rather than a Python string representation;
- retention of point attributes required by metric dependencies, including
  direct attribute dependencies and RGB/NIR inputs;
- individual array/shard publication validation before group registration; and
- GeoTIFF output that explicitly declares `AREA_OR_POINT=Area`.

The most recent extraction regression test verifies that TIFF geotransforms are
at *outer pixel edges*, not centers; each pixel center is half a cell inside the
declared bounds, no rotation/skew is present, the raster dimensions reconstruct
the requested extent, and output values remain correct. The extract suite
passed 10 tests, and the FUSION GridMetrics comparison path passed 10 tests.

The separate GDAL work is aimed at letting a user open a VRT over the S3 TileDB
shards and efficiently request a subset. It includes an upstreamable minimal
TileDB MDIM compatibility patch and associated autotests, but no public pull
request or public source publication was made by this work.

### 7. FUSION metric alignment and private CI

Silvimetric's GridMetrics implementation was audited against FUSION. It now
includes FUSION-style min-height/min-points behavior, return 1–9 and other
counts, density products, optional KDE/fuel outputs, RGB/NIR intensity input
selection, configurable elevation/intensity strata, and metric provenance and
definitions. `canopy_relief_ratio` supports a configurable NODATA value, with
zero as the default.

The FUSION source is vendored privately and has a portable CMake target for
GridMetrics only on macOS/Linux. The LFS COPC fixture is converted to LAS for
the reference program. CI builds GridMetrics on macOS and Linux, passes the
binary to downstream pytest jobs, regenerates reference rasters, and makes
per-metric comparison output deliberately noisy. The test matrix exercises
Python 3.12 and 3.14. The optional S3 integration test uses private secrets,
a stable base bucket, a SHA/run/UUID-specific child prefix, and explicit
cleanup isolation.

## Current code state

The current private branch is `codex/gdal-metadata-uint8`; `origin` is
`git@github.com:hobuinc/silvimetric-private.git`. The most recent commit is
`fffb40f test(extract): verify GeoTIFF pixel geometry`.

The current supported shatter strategies are `leaf-v1`, `macro-v2`, and
`macro-v3-stage-push`. The last is the recommended strategy for S3 production
work. Its key controls are `read_group_size`, `processing_halo_m`,
`stage_fragment_size_mb`, `stage_shard_target_points`, the coarse-planner
resolution multiplier, calibration sample controls, and the stage worker
address.

## What remains before a production build

1. **Run a current-code Becker calibration.** The proven 1.65B-point arm64
   result predates the latest metric, TileDB/GDAL, and extraction changes. Run
   a 1–2B-point Becker County polygon with the exact current source archive,
   Python 3.14, python-pdal 3.5.5, `macro-v3-stage-push`, and macro diagnostics
   enabled. This is the required first step before authorizing the full run.
2. **Configure, then measure, the 320-vCPU fleet.** The account's confirmed
   on-demand Standard-instance quota is 320 vCPUs. The current template can
   consume it with nine `m9g.8xlarge` worker hosts and one `m9g.8xlarge`
   scheduler. The template defaults are smoke defaults, not production
   settings: use eight four-thread Dask worker processes per worker host
   (72 processes / 288 worker vCPUs), `ExpectedDaskWorkers=72`, and leave
   memory headroom for PDAL/TileDB. Confirm the local-stage paths and disk
   pressure with a calibration before a full build.
3. **Measure, do not assume, large-fleet efficiency.** The 16-to-80 local
   worker result showed that Dask process count alone is not a linear scaling
   lever. Record PDAL read, aggregation, metric, merge, consolidation, publish,
   scheduler saturation, RSS, S3 request, and worker-idle timings in the
   calibration.
4. **Make shard sizing data-driven for this source.** Confirm the Becker
   density samples and actual output bytes. Adjust the point target rather
   than forcing every shard to 300 MiB; dense TileDB write amplification and
   non-empty domains make 300 MiB a target, not a requirement.
5. **Avoid whole-database extraction as the completion gate.** The current
   group reader still opens each intersecting shard and collects DataFrames
   before concatenation. A full 104-raster extract over a roughly 240 GiB
   database is both expensive and a poor validation pattern. Use fresh S3
   reopens plus a deterministic representative-shard/subset extraction gate;
   separately implement group-level metadata/catalog access and an efficient
   spatial mosaic for customer queries.
6. **Complete product delivery design.** The final paid-access service should
   broker short-lived, prefix-scoped S3 access or a query API after payment;
   it must not distribute reusable bucket-wide credentials or an unrestricted
   static access code. Add authentication, payments, entitlement auditing,
   rate limiting, egress accounting, backups, and replication to the product
   budget rather than the shatter budget.
7. **Finish cost observability.** `RunId` and `CostCeilingUsd` tags were
   activated/backfilled, but Cost Explorer did not initially expose EC2
   `BoxUsage` or per-instance data. Enable payer-account EC2 resource-level
   data and a Cost and Usage Report/Data Export with resource IDs and tags;
   reconcile each run manifest with posted EC2, EBS, S3 request/storage, and
   network records.

## MN_BeckerCo_1_2021 estimate

### Dataset and work estimate

The supplied [EPT metadata](https://s3-us-west-2.amazonaws.com/usgs-lidar-public/MN_BeckerCo_1_2021/ept.json)
declares **97,225,507,932 points** in EPSG:3857. The USGS WESM record gives a
collection interval of 2022-05-15 through 2022-09-18. The bounded Minnesota
density survey observed a 7.64 points/m2 P95 and selected a 1,360 m macro
window (4,624 20 m cells) for this collection.

With the required 20 m collar on every 1,360 m macro read, the modeled reader
amplification is:

```text
((1,360 m + 2 x 20 m) / 1,360 m)^2 = 1.0596886
97.2255B declared points x 1.0596886 = 103.029B collared reader points
```

This is deliberately a first-order model: it accounts for repeated collar
area, not the exact per-window EPT hierarchy behavior. The Becker P95 density
is lower than the B22 arm64 pilot's 14.61 points/m2, so point-linear timing is
not a guarantee; the larger planned macro can reduce fetch overhead but may
increase peak memory per task.

### Fleet and time model

The account has a confirmed 320-vCPU on-demand Standard-instance quota. The
recommended fleet is:

| Role | Instance type | Hosts | vCPUs | Configuration |
|---|---|---:|---:|---|
| Dask workers | `m9g.8xlarge` | 9 | 288 | 8 processes/host x 4 threads; 12 GiB managed memory/process initially |
| Scheduler/runner | `m9g.8xlarge` | 1 | 32 | Scheduler only; 80 GiB gp3 root volume |
| Fleet total | — | 10 | 320 | Worker hosts use 240 GiB gp3 stage volumes |

The accepted 16-worker arm64 pilot supplied 64 worker vCPUs and processed
1.650681B collared points in 1,228.136 seconds. Scaling that measured rate by
the 288/64 = 4.5 worker-vCPU ratio gives an optimistic full-workflow time of
4.73 hours for 103.029B collared points. This assumes similar worker efficiency
and excludes a full-database extract. The defensible planning range is **5–7.5
hours**, because PDAL/S3 request behavior, final local merges, shard count,
and scheduler overhead have not been measured at 72 processes.

### On-demand cost model

The AWS Price List query made on 2026-09-09 reports `m9g.8xlarge` Linux shared
on-demand in `us-west-2` at **$1.56544/hour** (effective 2026-09-01). The
ten-host fleet is therefore $15.65440/hour. The following is a transparent
usage model, not Cost Explorer billing:

| Component | Basis | Estimate |
|---|---|---:|
| EC2 | 10 x `m9g.8xlarge` x 4.73 h | $74.07 |
| gp3 EBS during build | 9 x 240 GiB workers + 80 GiB scheduler, 4.73 h at $0.08/GB-month | $1.16 |
| Public IPv4 during build | 10 addresses x 4.73 h at $0.005/hour | $0.24 |
| S3 request lower-bound | Scale the accepted arm64 pilot's $0.07 request model by collared points | $4.37 |
| First month of final S3 Standard storage | 2.5064 bytes/collared point = 240.5 GiB at $0.023/GiB-month | $5.53 |
| **Modeled build plus first storage month** | Excludes full coverage extract, source-owner request charges, egress, backup, and tax | **$85.37** |

Use **$125** as the run authorization ceiling: it covers approximately 25–45%
more wall time, request amplification, and output-size variation. Continuing
storage is modeled at **about $5.53/month** for the TileDB database alone;
budget a wider $4.60–$6.90/month range until the current metric set is measured.
The newly added metrics may change bytes per point from the B22 pilot, which
is why storage must be recalibrated in the Becker pilot.

### Recommended execution sequence

1. Run a bounded 1–2B collared-point Becker calibration on the nine-worker
   arm64 fleet, with a $25–$35 cap, a 1,360 m initial macro, 20 m collar,
   `readers.ept.requests=4`, and current-code source hashes.
2. Require the calibration to validate a fresh S3 group open, published
   fragments, date metadata, deterministic subset `extract` GeoTIFFs, shard
   size distribution, and metric tolerance policy. Record the manifest's
   phase timings and true worker saturation.
3. Refit points/hour, bytes/point, request counts, and peak local staging
   bytes. If forecast cost remains within $125 and all workers are usefully
   occupied, launch the complete collection to a new immutable S3 prefix.
4. Promote that prefix only after validation. Retain representative extracts
   and manifests as evidence; do not generate all 104 full-coverage TIFFs as
   the normal completion check.

## Evidence and related records

- `AGENTS.md` records the private repository, CI, GridMetrics, and TileDB/GDAL
  continuation rules.
- `FUSION-SM-ATTRIBUTE-BREAKDOWN.md` and `FUSION-SM-GAP-ANALYSIS.md` record
  the metric mapping and gap closure work.
- The companion private `sm-distributed` records contain the detailed run
  ledger, macro-v3 report, density survey, CloudFormation templates, and
  immutable S3 manifests.
