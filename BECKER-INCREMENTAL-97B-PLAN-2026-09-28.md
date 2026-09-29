# Becker 97B incremental Batch-v4 build plan (2026-09-28)

## Density-calibration amendment (2026-09-29)

The bounded full-resolution source sampling is complete. The authoritative
phase manifest is now
`/Users/hobu/dev/git/sm-distributed/config/becker-incremental-grid-plan.json`;
the former candidate is preserved as `becker-incremental-grid-plan-provisional.json`
alongside the query-by-query `becker-density-calibration-2026-09-29.json`.
The new plan has **seven** complete, gapless phases rather than the five
provisional rectangles described in the original cost sketch below. Its
first phase is `[125639,31979,126988,33104)`, estimated at **17.987B raw
EPT points** (nominal upper 21.743B). It includes the frozen water mask in
the subsequent build, but these raw estimates are before masking/filtering.

The 192 direct 100 m sample windows extrapolated to 99.524B points before
normalization, 2.36% above the EPT header; the spatial model is normalized
by 0.976909 to the advertised 97.226B. The earlier 40 m hierarchy-only
multiplier varied too much and was rejected. The sampling evidence and
per-phase estimates are described in
`/Users/hobu/dev/git/sm-distributed/docs/becker-density-calibration-2026-09-29.md`.
Run the complete-plan validator before deployment. The first-phase admission
cap remains **$175**, but the nominal upper workload estimate is not a hard
bound; pause and review if admission approaches that cap. The old five-phase
and three-20B cost rows below are historical modeling assumptions, **not**
the current dispatch schedule. Re-estimate after the first sealed phase and
snapshot benchmark before scheduling phase 2.

## Decision and current state

The first approximately 18B-raw-point phase becomes the **base** of one private,
permanent canonical TileDB array. After it is sealed without whole-array
consolidation, freeze an independent private S3 copy and benchmark
consolidation on that copy. Leave the base array untouched, then append a
different disjoint phase. Continue through the seven calibrated, disjoint
pixel rectangles. Consolidate the complete canonical array once and copy that final
result to one publication prefix. The snapshot copy is not the append target.

No 20B job has been launched by this plan.

Live AWS checks in `us-west-2` using `silvimetric-smoke`:

| Control | Verified value |
| --- | ---: |
| Standard Spot request quota (`L-34B43A08`) | 756 vCPUs |
| On-Demand Standard quota | 512 vCPUs |
| September account budget alert | $1,250 |
| September calculated spend at check | $480.488 |
| September forecast at check | $526.918 |

The private Becker storage stack
`silvimetric-becker-storage-c0f24425` is `UPDATE_COMPLETE`. Its `builds/`
prefix no longer expires after 30 days; `runs/` and `artifacts/` retain their
30-day evidence lifecycle. The Batch template now permits up to 720 Spot
vCPUs for stages, leaving quota margin, but a first 20B deployment should
start with the proven 480-vCPU ceiling and increase only if stage saturation
rather than publishing is limiting throughput. AWS Budgets alerts are **not**
a hard spend stop. The Batch admission ledger controls dispatch; watch actual
account spend, S3, and any untagged charges separately.

## Required phase invariants

1. Use one new canonical URI under `builds/collection-canonical/array` in the
   permanent private Becker bucket. The existing collaborator reader policy
   cannot read `builds/`; only after full validation copy it to a single
   `db/becker-full/array` publication URI that policy can read. Every
   phase has a unique RunId, Batch stack/control table, immutable PDAL input
   pipeline, `builds/<RunId>/` stages and ledger, and RunId cost tags. Do not
   reuse a phase RunId for a different rectangle. Do not delete its Batch
   stack until that phase is sealed and its receipts are exported.
2. Freeze the science recipe before phase 1: source EPT, date range,
   `filters.smrf`/HAG treatment of water, metric definitions, water mask,
   20 m USGS Albers grid, and PDAL reader settings. Appends reject a changed
   storage schema, date/mask/filter recipe, or an unfinished predecessor.
   The MN DNR open-water COG is required for target-grid Becker requests:
   source SHA-256
   `0f527d33c3f61fbb6703f1e6ddd8750dc46abd4f964241d9e5f2d4752daa78e5`.
   Request preflight checks its bytes, 20 m PixelIsArea EPSG:5070 grid,
   NoData=255, and whole-phase coverage, then pins the exact bytes at
   `inputs/water-mask/<sha256>.tif` in the private non-expiring bucket.
   Workers use that private S3 key; the zero-work Batch canary rehashes it.
   Appends reject a changed mask URL **or** SHA. The live source mask covers
   the complete Becker target rectangle. The 10,393,261-byte object has
   already been pinned at
   `s3://sm-becker-056176271256-c0f24425-9fcc-4464-a7fe-820ebeb7e8be/inputs/water-mask/0f527d33c3f61fbb6703f1e6ddd8750dc46abd4f964241d9e5f2d4752daa78e5.tif`;
   a credentialed `WaterMask` GDAL open succeeded. Only the final crop polygon may
   differ for legacy GeoJSON requests; target-grid phases use no phase-wide
   source crop. Record both repository commits,
   container digest, pipeline hash, phase bounds, and RunId in every request.
3. Partition the **target pixel grid**, not just latitude/longitude polygons.
   Use nonoverlapping EPSG:5070 pixel-edge rectangles snapped to the fixed
   `(-2493045, 3310005)` origin and 20 m resolution. The candidate
   `mn-beckerco-consolidation-20b.geojson` is only a point-count hint; its
   projected bounding rectangle has been converted into a provisional
   phase-1 pixel window. The seven calibrated rectangles exactly tile the
   declared full Becker target rectangle. Preserve the plan manifest and its SHA-256.
   The request generator now accepts `--target-pixel-bounds`; that pathway
   omits the phase-wide source crop so per-macro collared reads survive at
   phase edges, then crops after reprojection in Silvimetric. The saved
   `config/becker-incremental-grid-plan.json` now has no unallocated
   rectangles; the original candidate remains in the `-provisional` file.
4. Run one phase at a time. The core planner rejects overlap with any prior
   phase and refuses an append while another phase is unfinished. The Batch
   request requires explicit `--append-to-existing-canonical` and checks the
   existing array and processing recipe. A persistent S3 conditional-create
   lease under `builds/_collection_leases/` admits only one RunId to the
   Batch planner at a time. A failed phase keeps its lease for same-RunId
   recovery. After sealing and quality review, stop its Batch compute stack
   and use `release_batch_v4_collection_lease.py`; phase 1 additionally
   requires a completed private snapshot benchmark manifest. Do not start
   duplicate planners with the *same* RunId concurrently.
5. Use `--defer-array-consolidation` on all incremental phase requests. The
   finalizer verifies receipt-to-canonical point counts, writes a sealed
   ledger receipt and finished history record, but does not consolidate the
   entire growing array. A failed/partial phase keeps its stage/ledger and is
   resumed with the same RunId, never recreated with a different source.
6. After phase 1, while the base is quiescent, run
   `tools/benchmark_incremental_snapshot.py copy` into a private
   `snapshots/<RunId>/array` prefix. Its manifest pins source object inventory
   and verifies destination object names/sizes. Run the separate `benchmark`
   command on that copy; it checks the sealed phase point count before/after
   TileDB consolidation, records fragment counts/bytes and wall time, and
   leaves the base unmodified. Copy and benchmark are restartable.
7. After all phases, call `consolidate_macro_v4_collection` with the frozen
   partition's target bounds and ordered `(ledger_uri, RunId)` entries. It
   refuses missing/extra histories, unsealed phases, overlapping/out-of-grid
   rectangles, or uncovered pixels, checks each phase count before/after one
   whole-array consolidation, and returns a maintenance report. Only then
   copy the canonical array to one final private `db/becker-full/array`
   publication prefix. The resumable `benchmark_incremental_snapshot.py copy`
   command can perform the inventory-verified final copy using the last sealed
   phase's ledger; do not run its `benchmark` action on the publication copy.
   GDAL/VRT/extract checks must pass before granting collaborators access.

## Cost calibration and gates

The sealed 9B Batch reference processed **8.524575503B points**, published
294 leaf blocks after 38 memory splits, and completed whole-array
finalization in 582.5 s (453.0 s fragment consolidation). Its preliminary
RunId-tagged Cost Explorer total through 2026-09-27 was about **$9.57**:
roughly $8.69 EC2 compute, $0.61 EC2-Other/EBS and $0.27 tagged S3,
including the later preview activity. The 9B admission ledger's $195.85 was
**not a bill**. September Cost Explorer data remains estimated, and shared
bucket/untagged S3 is not allocated to that RunId.

At 20B, simple point scaling predicts about **690 published leaves** and
**89 split attempts**. With the measured instance rates and an intentionally
conservative dispatch envelope of $0.12 per stage, $0.06 per publish, and
$5 per finalizer, admission is approximately
`(690 + 89) × $0.12 + 690 × $0.06 + $5 = $140`.
Set the first 20B **per-run admission cap to $175**, about 25% above that
calculation. At the observed Spot rate (~$0.334 per m9g.4xlarge-hour), a
full 20-minute 4-vCPU stage reservation corresponds to about $0.028 of EC2;
the $0.12 charge leaves room for packing loss, retries, EBS and controls.
The reservation units are dispatch controls, not forecast AWS charges, and
do not bound untagged S3 or long-lived storage.

| Scope | Linear tagged-cost anchor | Planning allowance, not a quote | Admission envelope |
| --- | ---: | ---: | ---: |
| First 20B phase | ~$22.4 | $30–70 billed incl. shared overhead/retries | $140 modeled; $175 cap |
| Three 20B phases | ~$67.3 | $90–210 billed | ~$420 modeled |
| Remaining ~37.225B, preferably two ~18.6B phases | ~$41.8 | $55–130 billed | ~$261 modeled |
| All 97.225B plus one 20B snapshot and final copy/maintenance | ~$109 tagged extrapolation | **$160–350** project allowance, excluding ongoing storage | ~$681 modeled across five phases; $175 cap each |

The full EPT header advertises 97,225,507,932 points. Transforming its full
EPSG:3857 XY bounds to EPSG:5070 and rounding outward to the 20 m profile
grid yields half-open pixel bounds **(123899, 30265, 127876, 34188)**.
The calibrated first phase is **(125639, 31979, 126988, 33104)**. Seven
rectangles partition all 15,601,771 target cells without gap or overlap;
the first is estimated at 17.987B raw source points. Actual eligible/retained
points may differ after missing-node handling, clipping, water masking and
other filtering. The nominal phase upper estimates are planning diagnostics,
not guaranteed workload limits.

The 9B canonical array was 14.37 GB in 34,140 S3 objects. Linear scaling
suggests ~33.7 GB/~80,000 objects for a 20B canonical copy, and ~164 GB for
97B before any fragment-count changes. At an illustrative S3 Standard
$0.023/GB-month, one 20B copy costs about $0.78/month and one 97B copy about
$3.77/month; retained stages and duplicate final copies add to this. The
masked 2.65B pilot retained 5.78 GB of stage/control objects, so a simple
20B stage-prefix extrapolation is another ~44 GB before cleanup. Request
charges, data transfer, and consolidation I/O are separate. Snapshot COPY
requests alone are approximately $0.40 at $0.005/1,000 if the object-count
extrapolation holds. Confirm current us-west-2 S3 charges from Cost Explorer
after the first phase rather than treating these linear figures as a bill.

The $1,250 September budget currently leaves about $769.51 versus calculated
spend, but also includes unrelated account activity. Do not commit all five
phase caps as if they were dollars spent. Stop after the first 20B, reconcile
posted billing and snapshot/finalization timing, then update the full-project
range and admission coefficients before releasing the second phase. A run
that approaches $175 admission must pause dispatch and be reviewed, not
silently raised.

## Go/no-go before phase 1

- Density calibration and gapless partitioning are complete. Validate the
  seven-phase manifest with `--require-complete` and pin its SHA-256 in the
  phase request. Fill in RunIds/ledger URIs as phases seal.
- Run offline Batch/template tests and one small S3 append/copy/finalize
  integration on the exact container digest. Verify `extract`, GDAL/VRT,
  counts and water-mask behavior. The completed 2.65B masked comparison
  removed 1.807% of points and 9.915% of populated pixels while retaining
  exact land counts/means; it did not include the newer water-as-ground HAG
  change. Freeze and validate that HAG change before creating the 97B array.
- Choose a new empty canonical prefix in the permanent private bucket,
  reserve distinct RunIds, and use the persistent per-canonical S3 lease.
  No existing public preview array should become this base.
- Use a fresh per-run Batch stack with `CostCeilingUsd=175`, RunId tags,
  480 stage Spot vCPUs initially, 16 on-demand publisher vCPUs, and the
  `sivimetric@hobu.co` budget recipient as configured. Record object
  inventories and actual Cost Explorer usage types at each gate.

## Operator sequence after the gates pass

The first request must be made from a deployed, committed container image
whose digest is recorded alongside the plan. In `sm-distributed`, after its
new per-run Batch stack is ready with matching RunId and $175 ceiling:

When deploying the CloudFormation template with `--s3-bucket`, set
`--s3-prefix infrastructure/batch-v4/$PHASE1_RUN_ID`. Do not upload the
template under `builds/$PHASE1_RUN_ID`: request creation deliberately requires
that run's build prefix to be empty before it writes the immutable request.

```sh
python scripts/validate_becker_grid_plan.py \
  --plan config/becker-incremental-grid-plan.json --require-complete
python scripts/create_batch_v4_request.py \
  --run-id "$PHASE1_RUN_ID" --batch-stack "$PHASE1_STACK" \
  --output-bucket sm-becker-056176271256-c0f24425-9fcc-4464-a7fe-820ebeb7e8be \
  --canonical-prefix builds/collection-canonical/array \
  --phase-name phase-1 \
  --defer-array-consolidation --cost-ceiling-usd 175
```

`--phase-name` resolves the exact bounds from the complete calibrated plan
and records its file and stable partition hashes in the immutable request;
do not hand-copy the pixel indices. Later phases also require
`--append-to-existing-canonical`.

The request automatically uses the SHA-pinned private water mask. Run the
zero-work Batch canary and reviewed first-stage gate before releasing all
stages. After phase 1 is sealed and its Batch stack is stopped, use the
following separate snapshot steps; `SNAPSHOT_URI` is a new private prefix:

```sh
python /Users/hobu/dev/git/silvimetric-hobu/tools/benchmark_incremental_snapshot.py copy \
  --source-uri "$CANONICAL_URI" --snapshot-uri "$SNAPSHOT_URI" \
  --ledger-uri "$PHASE1_LEDGER_URI" --build-id "$PHASE1_RUN_ID"
python /Users/hobu/dev/git/silvimetric-hobu/tools/benchmark_incremental_snapshot.py benchmark \
  --snapshot-uri "$SNAPSHOT_URI" --fragment-size-mb 300
python scripts/release_batch_v4_collection_lease.py \
  --request-uri "$PHASE1_REQUEST_URI" \
  --snapshot-manifest-uri "${SNAPSHOT_URI}.snapshot.json"
```

Phase 2 uses another reviewed pixel rectangle/RunId/Batch stack and the same
canonical prefix, adding `--append-to-existing-canonical`; it omits a
phase-wide source crop but retains all macro collars. Repeat that process
until the plan has no unallocated rectangles. Then run
`tools/finalize_incremental_collection.py` with the completed JSON plan and
copy its verified canonical array to `db/becker-full/array` using the snapshot
tool's `copy` action and the last sealed phase's ledger. Do not publish the
`builds/` base or the snapshot benchmark prefix.

References: [9B result](BECKER-9B-BATCH-RUN-2026-09-26.md),
[AWS S3 pricing](https://aws.amazon.com/s3/pricing/),
[AWS Spot quota behavior](https://docs.aws.amazon.com/AWSEC2/latest/UserGuide/using-spot-limits.html).
