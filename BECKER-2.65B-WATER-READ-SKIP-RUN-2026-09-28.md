# Becker 2.65B water-only EPT-read skip comparison

This run compares against the sealed water-mask baseline
`7084bbb8-9cac-41d8-8a78-21ed58e89139`. It keeps the exact 14 km Becker
boundary, frozen PDAL pipeline, USGS Albers 20 m grid, mask, ARM64 Batch
fleet, and $50 admission cap. The only processing change is Silvimetric commit
`30f3f8139c274f73c1829536d0cfffeb70879d7b`: a processing core whose
*every* output pixel is known water skips its EPT/PDAL read. Mixed cores keep
their full collared reads and post-read pixel masking so land-cell metrics
are unchanged.

- Run ID: `86e24154-912c-4c5d-bb20-3fbd52f2276c`.
- Canonical array:
  `s3://sm-becker-056176271256-c0f24425-9fcc-4464-a7fe-820ebeb7e8be/db/batch-v4-albers-water-skip-86e24154`.
- Immutable request:
  `s3://sm-becker-056176271256-c0f24425-9fcc-4464-a7fe-820ebeb7e8be/builds/86e24154-912c-4c5d-bb20-3fbd52f2276c/batch-v4-request.json`.
- ARM64 image digest:
  `sha256:b18cbe9e475a4ca5b49c71ad833dc8b39f001a7426c308acd66ccab3779c0326`.
- Frozen pipeline SHA-256:
  `2b551389c78a0625bf03c5c9a44c97e0553d67940d39bf160560a6794b08b961`.
- Private CI [run 36437273192](https://github.com/hobuinc/silvimetric-private/actions/runs/36437273192)
  passed; local focused tests had 78 passed and 2 skipped. The zero-work
  Batch canary `3c1444fb-752c-41aa-9c40-8470f022854c` succeeded.
- Plan job: `efc0d5d5-ae7f-4ebc-8b27-28d05f8eee2d`.
- The plan reproduced 56 roots and 256 cores. The first stage
  `81da198a-6bd0-450d-8f05-99ac4a83949a` and publisher
  `61847e44-893e-4107-9ec3-798305d28765` succeeded. A GDAL read of its
  exact 34 x 79 global-pixel window (`125639,32384,34,79`) matched the
  prior masked array: 28,414,285 points in 2,532 populated land pixels,
  zero count or `m_Z_mean` mismatches. The other 55 roots were released at
  2026-09-28 15:13:52 UTC.
- CloudWatch stage logs already show the 13 expected
  `SILVIMETRIC_WATER_SKIP` events, one for each wholly water core.
- Tail recovery: two dense roots timed out at the 30-minute Batch limit and
  split automatically. A third root (`20415-2662325-22155-2663605`)
  exited natively with codes 139, 139, and 133 across three attempts.
  Its automatically submitted fourth identical attempt was terminated
  after Batch confirmed it had no staged receipt. The ordinary failure
  handler marked the parent `DEAD`; an operator recovery then recorded
  `worker_lost_after_retries`, changed it to `SPLIT`, and released two
  disjoint children. This is a reliability/performance confounder and its
  failed EC2 occupancy must be included in the cost analysis.

## Read-elision ceiling from preflight

The prior masked run's 56 root blocks contain 256 unique processing cores.
The published mask marks 13 cores wholly water, 120 partly water, and 123
without water. The 13 skippable cores contained 6,647,414 baseline points
in 11,776 cells: 0.251% of its 2,647,868,723 points and 13.89% of its
47,844,871 water points. The remaining water points lie in mixed cores.
This optimization therefore has a small expected upper bound on EPT-read
time and cost. EPT octree nodes can contain land and water together; the
change does **not** guarantee zero water bytes transferred.

## Results

The build **SEALED** at 2026-09-28 17:34:28 UTC. All 59 active leaves
published and the independent `tools/validate_batch_v4.py` quality gate
passed. The exact 484 x 478 global-pixel window contained 2,600,023,852
points in 193,037 populated land cells, exactly matching the prior masked
build. Full-window GDAL reads found zero `count` mismatches, zero land
`m_Z_mean` mismatches, and zero present water `m_Z_mean` cells. The maximum
land mean difference was 0. The validator also found no multiply owned
stage cells and confirmed sampled GDAL, `extract`, and dense-cell raw-point
results. Its full diagnostic is
`/private/tmp/becker-86e24154-quality.json` on the local host.

| Measure | Prior masked run | Water-read-skip run | Interpretation |
| --- | ---: | ---: | --- |
| Published points | 2,600,023,852 | 2,600,023,852 | Pixel-exact output |
| Published leaves / split parents | 61 / 5 | 59 / 3 | Different recovery shape |
| Successful-stage read phase sum | 37,030 s | 30,175 s | 18.5% lower, **not** attributable entirely to the skip |
| Successful-stage metric phase sum | 9,916 s | 8,862 s | 10.6% lower; partition/fleet confounding |
| Successful-stage write phase sum | 1,464 s | 1,452 s | Essentially unchanged |
| Publisher phase sum | 1,381 s | 1,404 s | Essentially unchanged |
| Stage Batch job occupancy, including failures | 66.25 allocated vCPU-h | 56.26 allocated vCPU-h | 15.1% lower; not EC2 host-hours or a bill |
| Failed stage attempts / occupied time | 5 / 9,137 s | 6 / 8,182 s | Different failure timing and causes |
| Build-record creation to seal | 77m 03s | 2h 35m 55s | New run 2.02x slower wall time |
| Release of remaining roots to seal | 61m 54s | 2h 20m 36s | New run 2.27x slower after gate |
| Admission reservation | $39.85 | $39.10 | Neither value is AWS billed cost |

The receipt sums are **application times accumulated across concurrently
running jobs**. They exclude work done by a process that died before writing
its staged receipt. The Batch occupancy row includes those failures but does
not include EC2 host launch/idle time, packing inefficiency, EBS, or S3.
The new run's much longer wall time came from the serial retries of one
six-macro root (three native exits) and repeated cold starts, not from
the water-mask read decision. The prior run had five timeout failures that
split earlier and ran their children concurrently.

### Isolating the read-skip effect

Thirteen `SILVIMETRIC_WATER_SKIP` events confirm the intended cores skipped
PDAL/EPT. Their baseline point population was 6,647,414, only **0.251%**
of all baseline points. Fifty-one identically shaped published root blocks
had no skipped water core, yet their read sum fell from 33,470 to 27,311
seconds (**18.4%**). Six common root blocks containing the skipped cores
fell from 1,368 to 898 seconds (**34.4%**) with identical output point
counts. Applying the no-skip group's read-time ratio to those six roots
gives a heuristic **219 seconds** of incremental read work avoided, about
**0.6%** of the prior run's total successful-stage read time. This is not a
controlled estimate of EPT bytes or GETs; Spot placement, public S3
response time, and split geometry varied between runs. The 18.5% aggregate
read-phase reduction must not be presented as the effect of water elision.

### S3 and billing checkpoint

Both run-prefix S3 request-metric configurations were enabled before their
canaries. For like-for-like build windows (the earlier run 12:15–13:40 UTC,
this run 14:55–17:35 UTC), the **build prefix** recorded 121,291 versus
124,319 GETs and 254.81 versus 267.55 GB downloaded. Thus its recorded
output-side read traffic **increased** about 5.0%, despite the water-core
skip. These metrics describe TileDB stages/control in our bucket, **not**
requests to the USGS EPT bucket. The new canonical prefix had fewer GETs
(17,385 versus 19,875), consistent with fewer published fragments/leaves,
but that is not evidence of reduced source EPT I/O.

| S3 prefix inventory after seal | Prior masked run | Water-read-skip run |
| --- | ---: | ---: |
| Build/stage bytes | 5,778,499,686 | 5,836,318,650 |
| Canonical bytes | 4,978,514,414 | 4,977,037,017 |
| Combined bytes | 10,757,014,100 | 10,813,355,667 |

The new combined footprint is 56,341,567 bytes (0.52%) larger, primarily
in immutable stages. Prefix inventory is a snapshot, **not** a storage bill.
Cost Explorer currently returns **$0 tagged** for either run on 2026-09-28
and marks that day estimated; an earlier broader query showed only $0.027
for the prior run. Those figures are incomplete billing, not free compute.
Shared-bucket S3 lines cannot be attributed to one RunId by Cost Explorer.
The **actual dollar-cost difference remains pending** until EC2/Spot and EBS
charges post. Do not use the $39.10/$39.85 admission reservations as bills.

Both Batch compute environments returned to desired zero vCPUs, and no EC2
instance tagged with this run ID remained pending, running, or stopping at
the final check.

**Recommendation:** Keep the correctness-preserving skip, but do not expect
it to materially lower 2.65B-footprint cost by itself. Larger savings would
require a source-reader query that prunes *mixed* water/land EPT nodes while
retaining sufficient water/ground collar context for unchanged land HAG and
metrics. That requires a separate bounded A/B validation; the current
optimization intentionally does not alter mixed-core reads.
