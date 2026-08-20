# FUSION GridMetrics reference pathway

`../NoCAL_PlumasNF_B2_2018_TestingData_FUSIONNormalized.copc.laz` is the
canonical source fixture. It is tracked by Git LFS with SHA-256
`6912665fcd0d807c22f74a2682f1e8e9a3a232a853ee6dbc13584bf5af0b2a36`.

The checked-in rasters keep the normal test suite self-contained. For a fresh
source baseline, build the vendored native `GridMetrics` target for macOS or
Linux, configure its path, and run the FUSION test:

```shell
cmake -S vendor/fusion -B build/fusion-gridmetrics -DCMAKE_BUILD_TYPE=Release
cmake --build build/fusion-gridmetrics --parallel
export FUSION_GRIDMETRICS="$PWD/build/fusion-gridmetrics/GridMetrics"
python -m pytest tests/test_fusion.py -q
```

The test expands the COPC to an ordinary LAS file with PDAL because FUSION's
legacy LAS reader is not a COPC reader. It then runs the following substantive
GridMetrics configuration:

- `/noground /nointdtm` — the fixture is already ground normalized;
- `/minht:2 /minpts:3` and height break `2`;
- a 30 m grid spanning `635535,4402335,635865,4402815`. GridMetrics treats
  the maximum `/gridxy` value as an additional cell origin, so the invocation
  supplies `635835,4402785` to produce this outside-edge extent;
- `/ascii` plus the raster products used by the Silvimetric comparison.

The temporary `fusion-gridmetrics-manifest.json` records the exact executable
command, input SHA-256, stdout, and stderr for an externally generated run.
Any comparison failures from this opt-in regenerated baseline are intentional
signals of a remaining FUSION/Silvimetric metric discrepancy; the checked-in
baseline keeps the ordinary test suite deterministic while those gaps are
closed.
