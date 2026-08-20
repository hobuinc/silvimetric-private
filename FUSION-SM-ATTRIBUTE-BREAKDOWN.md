# FUSION and Silvimetric Attribute Breakdown

## Scope and naming model

This review compares Silvimetric's `get_grid_metrics()` implementation with
the supplied FUSION `GridMetrics` and `CloudMetrics` source. FUSION emits
separate `Elev ...` and `Int ...` CSV columns. Silvimetric identifies the
source dimension in the stored TileDB attribute name:

| Silvimetric stored attribute | FUSION source/output family |
|---|---|
| `m_Z_<metric>` | `Elev <metric>` |
| `m_Intensity_<metric>` | `Int <metric>` |

The mapping below assumes the same selected points, height normalization,
height break, and minimum-height rules. "Exact" means the metric formula is
equivalent under those assumptions.

## Core metric mapping

| Silvimetric metric | FUSION output name | Mathematical correspondence |
|---|---|---|
| `min`, `max` | `Elev minimum` / `Elev maximum`; `Int minimum` / `Int maximum` | Exact |
| `mean` | `Elev mean`; `Int mean` | Exact arithmetic mean, including an all-zero non-empty cell |
| `variance` | `Elev variance`; `Int variance` | Exact sample variance: denominator `n - 1` |
| `median` | `Elev P50`; `Int P50` | Exact; FUSION does not separately emit median |
| `p01` through `p99` | `Elev P01` through `Elev P99`; `Int P01` through `Int P99` | Exact linear interpolation at `(n - 1) * p` |
| `iq` | `Elev IQ`; `Int IQ` | Exact: `P75 - P25` |
| `90m10` | `P90 - P10` | Exact |
| `95m05` | intended `P95 - P05` | Silvimetric is correct; the reviewed FUSION GridMetrics raster code appears to assign `P90 - P10` to its `p95m05` grid |
| `aad` | `Elev AAD`; `Int AAD` | Exact mean absolute deviation from the mean |
| `mad_median` | `Elev MAD median` | Exact median absolute deviation from `P50` |
| `mad_mode` | `Elev MAD mode` | Same formula, but inherits the mode difference described below |
| `l1`, `l2`, `l3`, `l4` | `Elev L1` through `L4`; `Int L1` through `L4` | Exact sample L-moment construction |
| `lcv`, `lskewness`, `lkurtosis` | `L CV`, `L skewness`, `L kurtosis` | Exact ratios: `L2/L1`, `L3/L2`, `L4/L2` |
| `canopy_relief_ratio` | `Canopy relief ratio` | Exact formula; both default to `0` for a constant cell (`nodata` is an explicit Silvimetric option) |
| `sqmean` | `Elev quadratic mean` | Exact RMS |
| `cumean` | `Elev cubic mean` | Exact for normalized non-negative heights; differs with negative values because FUSION applies `abs()` after summing cubes |
| `profile_area` | `Profile area` | Equivalent for non-negative heights; FUSION clamps every height to zero before percentile interpolation |
| `r1_count` through `r9_count`, `rother_count` | `Return 1 count` through `Return 9 count`, `other return count` | Exact, subject to the same `/minht` selection |
| `all_count` | `Total all returns` | Exact |
| `first_count` | `Total first returns` | Exact |
| `all_count_above_minht` | `Total return count above <minht>` | Exact |
| `1st_count_above_htbreak` | `First returns above <heightbreak>` | Exact |
| `all_count_above_htbreak` | `All returns above <heightbreak>` | Exact |
| `1st_cover_above_htbreak` | `Percentage first returns above <heightbreak>` | Exact |
| `all_cover_above_htbreak` | `Percentage all returns above <heightbreak>` | Exact |
| `all_1st_cover_above_htbreak` | `(All returns above break) / (Total first returns) * 100` | Exact |

## Legacy FUSION/LDV vegetation names

The supplied FUSION source uses descriptive strata headers rather than the
literal strings below. These names are the legacy LDV names Silvimetric is
intended to present.

| FUSION/LDV name | Silvimetric metric | Definition |
|---|---|---|
| `Cover3mAll` | `all_cover_above_htbreak`, `ht_break=3` | All returns above 3 m / all returns * 100 |
| `Cover3m1st` | `1st_cover_above_htbreak`, `ht_break=3` | First returns above 3 m / all first returns * 100 |
| `ARbyFR` | `all_1st_cover_above_htbreak`, `ht_break=3` | All returns above 3 m / all first returns * 100 |
| `Strata-1` | `Strata-1` | Height `< 1.37 m` |
| `Strata-2` | `Strata-2` | `1.37 <= height < 5 m` |
| `Strata-3` | `Strata-3` | `5 <= height < 10 m` |
| `Strata-4` | `Strata-4` | `10 <= height < 15 m` |
| `Strata-5` | `Strata-5` | `15 <= height < 20 m` |
| `Strata-6` | `Strata-6` | Height `>= 20 m` |

This six-stratum definition is equivalent to FUSION with
`/strata:1.37,5,10,15,20`. It is not FUSION's general default strata
configuration. Silvimetric also supports arbitrary FUSION-style elevation and
intensity strata through `strata_breaks` and `intensity_strata_breaks`, with
count, proportion, and descriptive-statistic outputs.

## Historical initial gaps — resolved

The six implementation gaps identified in the initial review are now fixed:

1. Product-moment standard deviation, CV, skewness, and kurtosis use sample
   standard deviation (`n - 1`).
2. Mode follows FUSION's 64 bins at increments of `range / 63`.
3. `all_count_above_mode` depends on mode.
4. Mean/mode cover metrics retain FUSION's unfiltered all-return denominator.
5. An all-zero non-empty cell has mean zero.
6. `canopy_relief_ratio`, `cumean`, and `profile_area` use FUSION's documented
   degenerate and negative-height behavior.

See `FUSION-SM-GAP-ANALYSIS.md` for the current per-metric audit, empirical
comparison, and remaining functionality gaps.

## Recommended naming strategy

Do not rename existing TileDB attributes in place. Preserve existing names
such as `m_Z_min` and `m_Intensity_min`, and add canonical FUSION aliases and
descriptions to metric metadata. That maintains database compatibility while
allowing extract, GDAL metadata, and documentation to expose the FUSION-facing
names such as `Elev minimum`, `Int minimum`, `Cover3mAll`, and `Strata-1`.

Only mark an alias as FUSION-compatible after the numerical gaps above have
been addressed and regression-tested against known FUSION output.

## Reviewed implementation sources

- `src/silvimetric/resources/metrics/grid_metrics.py`
- `src/silvimetric/resources/metrics/stats.py`
- `src/silvimetric/resources/metrics/p_moments.py`
- `src/silvimetric/resources/metrics/percentiles.py`
- `src/silvimetric/resources/metrics/covers.py`
- `src/silvimetric/resources/metrics/fusion_ldv.py`
- `src/silvimetric/resources/metrics/l_moments.py`
- `src/silvimetric/resources/metrics/aad.py`
- `fusion/fusion_src/code/github/FUSION_Metrics/GridMetrics/GridMetrics.cpp`
- `fusion/fusion_src/code/github/FUSION_Metrics/CloudMetrics/Cloudmetrics.cpp`
