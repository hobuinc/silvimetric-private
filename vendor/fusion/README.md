# Portable FUSION GridMetrics

This vendored source is a deliberately narrow, native build of the FUSION
`GridMetrics` command-line application. It does not build the FUSION GUI or
other FUSION tools. The original source is public-domain (`LICENSE`).

The upstream project targets Visual Studio/MFC. `portable/include/` supplies
only the small MFC/Win32 compatibility surface reached by GridMetrics, and
the source edits retain LAS's specified fixed-width binary fields. The target
is intended for macOS and Linux; no Windows build is provided here.

Build out of tree:

```shell
cmake -S vendor/fusion -B build/fusion-gridmetrics -DCMAKE_BUILD_TYPE=Release
cmake --build build/fusion-gridmetrics --parallel
ctest --test-dir build/fusion-gridmetrics --output-on-failure
```

The executable is `build/fusion-gridmetrics/GridMetrics`. It accepts the
native FUSION command line; on POSIX, run it from the input/output directory
and pass relative data paths because FUSION treats an argument beginning with
`/` as an option.

The Silvimetric comparison harness can generate a fresh GridMetrics baseline:

```shell
export FUSION_GRIDMETRICS="$PWD/build/fusion-gridmetrics/GridMetrics"
python -m pytest tests/test_fusion.py -q
```

The harness expands the COPC fixture to uncompressed LAS with PDAL before
running GridMetrics. It records its exact command and input checksum in a
temporary `fusion-gridmetrics-manifest.json`. A comparison failure is useful
diagnostic output: it identifies any remaining metric-semantic discrepancy
between the current Silvimetric implementation and the newly generated
FUSION result.
