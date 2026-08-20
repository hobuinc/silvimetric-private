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
  The optional remote S3 test runs only when the private repository supplies
  `SM_AWS_ACCESS_KEY_ID` and `SM_AWS_SECRET_ACCESS_KEY` secrets.
- A separate Ubuntu/macOS CMake job builds and CTests vendored GridMetrics.
  Keep this workflow private-only; do not duplicate it into a public repo.

## FUSION metric alignment

- `get_grid_metrics()` now exposes FUSION-style controls: no minimum-height
  default, strict `/minpts` behavior, return 1–9 and other counts, density
  fractions, optional KDE/fuel outputs, RGB/NIR intensity selection, and
  configurable elevation/intensity strata.
- Each GridMetrics metric carries FUSION provenance/definition metadata via
  `fusion_metadata.py`. The main mapping and audit are in
  `FUSION-SM-ATTRIBUTE-BREAKDOWN.md` and `FUSION-SM-GAP-ANALYSIS.md`.
- The ordinary checked-in FUSION baseline is deterministic and passing. A
  fresh native FUSION run is opt-in through `FUSION_GRIDMETRICS`; differences
  it reports are diagnostic until source selection and `/minht` + `/minpts`
  eligibility semantics have a fully recorded pixel-equality baseline.

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
