# Continuation context

## Repository and publishing

- Current working branch: `codex/gdal-metadata-uint8`.
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

## Validation used for this state

```shell
/Users/hobu/miniforge3/envs/silvimetric/bin/python -m pytest \
  tests/test_fusion_metric_equivalence.py tests/test_fusion_gridmetrics.py \
  tests/test_metrics.py tests/test_storage.py \
  tests/test_fusion.py::TestFusion::test_against_fusion -q
```

This ran successfully with 33 passing tests on 2026-08-20.
