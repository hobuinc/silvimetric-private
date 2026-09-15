# Continuation context

## Repository and publishing

- Current working branch: `codex/macro-v4-single-array`.
- `origin` is the private repository, `git@github.com:hobuinc/silvimetric-private.git`.
  Do not add or push to a public Silvimetric remote unless the user explicitly
  directs it.
- Keep related changes in small commits; this branch contains both the TileDB
  GDAL-compatibility work and the FUSION metric-alignment work.

## Continuous integration

- `.github/workflows/main.yml` is the private CI workflow. It runs on every
  push and pull request, with a manual-dispatch option.
- The full pytest suite runs on Ubuntu and macOS with Python 3.12 and 3.14.
  The ordinary matrix intentionally does not inject AWS credentials and logs
  that its optional remote S3 test is skipped. A separate single
  `s3-integration` Ubuntu/Python 3.14 job injects
  `SILVIMETRIC_ACCESS_KEY_ID` and `SILVIMETRIC_SECRET_ACCESS_KEY` and runs
  only the remote shatter test. The `silvimetric` CI test bucket is in
  `us-east-1`, so that job overrides the TileDB/AWS region locally; the rest
  of the project uses `us-west-2`. It uses the stable
  `SILVIMETRIC_TEST_S3_BUCKET` base bucket and a SHA/run-specific prefix plus
  a UUID per test database, so cleanup cannot touch another run's data.
- The Ubuntu/macOS CMake matrix builds and CTests vendored GridMetrics first.
  It publishes one executable artifact per OS. All pytest and S3 jobs depend
  on this build; pytest downloads the matching executable, regenerates the
  FUSION ASCII verification rasters, compares Silvimetric output against
  them, and uploads the generated rasters plus manifest for seven days.
  Keep this workflow private-only; do not duplicate it into a public repo.

## FUSION metric alignment

- `get_grid_metrics()` now exposes FUSION-style controls: no minimum-height
  default, strict `/minpts` behavior, return 1–9 and other counts, density
  fractions, optional KDE/fuel outputs, RGB/NIR intensity selection, and
  configurable elevation/intensity strata.
- Each GridMetrics metric carries FUSION provenance/definition metadata via
  `fusion_metadata.py`. The main mapping and audit are in
  `FUSION-SM-ATTRIBUTE-BREAKDOWN.md` and `FUSION-SM-GAP-ANALYSIS.md`.
- CI supplies `FUSION_GRIDMETRICS` from the matching CMake artifact and
  enables a fresh source baseline on every pytest job. `test_fusion.py` logs
  every compared metric's shapes, valid/differing-cell counts, and difference
  statistics. The FUSION harness uses `/gridxy` cell centers and `/buffer:15`
  so that its source point population matches Silvimetric's 30 m edge-based
  cells. It asserts pixel-exact population/count products and tolerance-based
  equality for floating summaries. FUSION's ASCII cover rasters are converted
  from fractions to the percentage convention used by its CSV/LDV products;
  its known `95m05` raster-writer P90-P10 defect is explicitly skipped.

## Native GridMetrics build

- The vendored FUSION source is in `vendor/fusion`. It deliberately builds
  only the command-line GridMetrics application on macOS/Linux; no Windows
  target is maintained here.
- Build and smoke-test it out of tree:

  ```shell
  cmake -S vendor/fusion -B build/fusion-gridmetrics -DCMAKE_BUILD_TYPE=Release
  cmake --build build/fusion-gridmetrics --parallel
  ctest --test-dir build/fusion-gridmetrics --output-on-failure
  ```

- The portability layer lives under `vendor/fusion/portable/include`. The LAS
  reader uses explicit 32-bit on-disk types because Windows `long` and POSIX
  `long` have different widths.
- The FUSION harness converts the LFS COPC fixture to ordinary LAS and records
  the exact execution command in `fusion-gridmetrics-manifest.json`.

## TileDB/GDAL compatibility

- Dense TileDB domains are physically padded to complete tile blocks so GDAL's
  TileDB raster driver can read edge blocks. Logical Silvimetric bounds and
  GDAL metadata remain the authoritative outside extent.
- `StorageConfig` automatically retains point attributes required by metric
  dependencies (including direct Attribute dependencies and RGB/NIR inputs).

## Canonical USGS Albers profile

- `initialize --usgs_albers` establishes a full-CONUS, pixel-is-area grid in
  `EPSG:5070+5703`.  Its fixed upper-left *outer pixel edge* is
  `(-2493045, 3310005)` and every profile database retains the same root.
- `shatter --usgs_albers` is required for profile databases and rejected for
  ordinary databases.  Its optional bounds are target EPSG:5070 bounds; the
  reader query is transformed to the source CRS, then PDAL reprojects and
  crops before Silvimetric calculates global pixel indices.
- Never enumerate profile root tiles.  Use
  `Extents.get_root_aligned_leaf_children()` so scheduling remains bounded to
  the input footprint while preserving physical TileDB tile boundaries.
- `tests/test_usgs_albers.py` covers the profile contract, reprojection,
  TileDB/GDAL pixel-is-area metadata, history serialization, CLI initialization,
  and equal cross-database target-grid selections.

## Macro-v4 canonical-array proof

- `macro-v4-single-array` is an intentionally separate proof path. It writes
  non-overlapping macro cores directly into one pre-created dense TileDB array;
  workers create internal fragments, not a Group of public spatial shards.
- It currently accepts only an empty destination array and schedules Dask
  tasks with `retries=0`: recovery needs an idempotent staging/publish ledger
  before a production run may retry an uncertain commit.
- Its canonical-array consolidation lets TileDB select valid fragment groups
  and sets `sm.consolidation.max_fragment_size`; do not reuse macro-v3's
  explicit local-stage consolidation plan after concurrent writes, because
  parallel commit order can make its selected fragment list invalid.
- Regression coverage in `tests/test_shatter.py` verifies macro-v2-equivalent
  extraction and two concurrent Dask writers against one array. The next
  milestone is a bounded S3 calibration using adaptive work units.
- `macro-v4-staged-publish` now records an adaptive ledger plan tree. A
  `MemoryError`, nanny-memory indication, or final Dask `KilledWorker` records
  a failed parent and replaces it with deterministic grid-aligned children.
  Generic task failures remain partial/resumable rather than being silently
  split. Configure `build_max_split_depth` (default 8) and
  `build_min_cells_per_side` (default one processing-tile side). The
  `MN_BeckerCo_1_2021` 1.28 km historical over-memory macro shape is covered
  by a planner regression test; an injected Dask memory failure verifies
  split/publish/extract equivalence, and a separate arbitrary stage failure
  verifies a clean later resume.
- The bounded S3 proof completed on 2026-09-12 at
  `s3://sm-smoke-056176271256-3b780f15-996a-4e00-a7f1-a7fadcb7bf17/macro-v4-proof/c88ca1fc-bea2-447b-a68f-cbaa4a0898c2/metrics.tdb`:
  two local Dask workers wrote 108,900 points in 121 populated cells; TileDB
  reports one active consolidated fragment after vacuum. The smoke bucket
  expires its contents after seven days.
- `macro-v4-staged-publish` is the production-oriented successor to that
  direct proof. It writes each deterministic macro block to an immutable
  local/S3 stage, records append-only `planned`/`staged`/`published` receipts,
  and limits concurrent writers to the canonical array. Re-run with the same
  `ShatterConfig.name`, canonical URI, and ledger URI: a build signature
  rejects foreign inputs, completed receipts are reused, a missing publish
  receipt is reconciled from the canonical count footprint, and a stable time
  slot is restored. Its final TileDB consolidation/vacuum is deliberately
  re-enterable. Do not run two drivers against one ledger concurrently.
- Use `build_publish_concurrency=4`,
  `build_publish_vfs_parallel_ops=4`, and
  `build_stage_vfs_parallel_ops=4` as the conservative first S3 fleet
  settings. The prior Becker failure was many workers each using TileDB's
  default parallel VFS operations against one fragment prefix. The EC2 runner
  creates durable S3 stages and ledger beneath `builds/<RunId>/`.
- `Storage.vacuum()` and the generic `Storage.consolidate()` must retain the
  `Storage.get_tdb_context()` settings. A fresh TileDB context loses the
  S3-region/profile configuration and leaves superseded S3 objects behind.

## Validation used for this state

```shell
/Users/hobu/miniforge3/envs/silvimetric/bin/python -m pytest \
  tests/test_fusion_metric_equivalence.py tests/test_fusion_gridmetrics.py \
  tests/test_metrics.py tests/test_storage.py \
  tests/test_fusion.py::TestFusion::test_against_fusion -q
```

This ran successfully with 33 passing tests on 2026-08-20.
