# FUSION and Silvimetric metric gap analysis (revalidated)

This review compares the current Silvimetric `get_grid_metrics()` code with
the supplied FUSION `GridMetrics.cpp`, including a direct run over
`NoCAL_PlumasNF_B2_2018_TestingData_FUSIONNormalized.copc.laz`.

## Validation basis

Silvimetric processes the supplied 4,420,201-point normalized COPC with the
same forced 30 m origin and extent as the checked-in FUSION rasters. The test
now asserts exact geotransform equality and exact equality for a stable set of
reference count pixels. The legacy FUSION rasters contain 4,419,981 counted
points while the COPC contains 4,420,201. The original FUSION command-line
selection was not recorded, so this 220-point difference prevents a
defensible whole-raster equality assertion. The test also retains bounded
whole-raster comparisons for the overlapping cells.

The prior six implementation gaps are closed:

1. Standard deviation, CV, skewness, and kurtosis use FUSION's sample SD.
2. Mode uses FUSION's 64 scaled bins and lowest-bin tie behavior.
3. Mode-based counts depend on mode, not mean.
4. Mean/mode cover metrics retain all-return denominators.
5. An all-zero non-empty cell has mean zero.
6. CRR, cubic mean, and profile area use FUSION's documented edge behavior.

## Status vocabulary

- **Match** — implementation and selection rule match FUSION when both are
  given the same points and configuration.
- **Corrected divergence** — Silvimetric deliberately follows FUSION's help
  definition rather than a demonstrable defect in the shipped FUSION source.
- **Extension** — available in Silvimetric but not a FUSION GridMetrics output.
- **Configuration gap** — formula matches, but a FUSION control is not exposed
  by `get_grid_metrics()`.

All numeric **Match** entries retain the global caveats below for no-data and
minimum-cell handling.

## Per-Silvimetric-metric table

| Silvimetric metric | FUSION equivalent | Status | Remaining discrepancy or note |
|---|---|---|---|
| `p01` | Elev/Int P01 | Corrected divergence | Elevation matches. FUSION's intensity path samples the first `CellCount` values after sorting all returns, rather than the filtered height-selected population. Silvimetric computes the documented filtered percentile. |
| `p05` | Elev/Int P05 | Corrected divergence | Same elevation match and intensity-source defect as `p01`. |
| `p10` | Elev/Int P10 | Corrected divergence | Same elevation match and intensity-source defect as `p01`. |
| `p20` | Elev/Int P20 | Corrected divergence | Same elevation match and intensity-source defect as `p01`. |
| `p25` | Elev/Int P25 | Corrected divergence | Same elevation match and intensity-source defect as `p01`. |
| `p30` | Elev/Int P30 | Corrected divergence | Same elevation match and intensity-source defect as `p01`. |
| `p40` | Elev/Int P40 | Corrected divergence | Same elevation match and intensity-source defect as `p01`. |
| `p50` | Elev/Int P50 | Corrected divergence | Same elevation match and intensity-source defect as `p01`. |
| `p60` | Elev/Int P60 | Corrected divergence | Same elevation match and intensity-source defect as `p01`. |
| `p70` | Elev/Int P70 | Corrected divergence | Same elevation match and intensity-source defect as `p01`. |
| `p75` | Elev/Int P75 | Corrected divergence | Same elevation match and intensity-source defect as `p01`. |
| `p80` | Elev/Int P80 | Corrected divergence | Same elevation match and intensity-source defect as `p01`. |
| `p90` | Elev/Int P90 | Corrected divergence | Same elevation match and intensity-source defect as `p01`. |
| `p95` | Elev/Int P95 | Corrected divergence | Same elevation match and intensity-source defect as `p01`. |
| `p99` | Elev/Int P99 | Corrected divergence | Same elevation match and intensity-source defect as `p01`. |
| `90m10` | P90 minus P10 | Corrected divergence | Exact for elevation. Intensity result follows Silvimetric's correctly filtered percentile values, not FUSION's faulty intensity percentile output. |
| `95m05` | P95 minus P05 | Corrected divergence | Silvimetric implements the documented definition. FUSION's raster writer assigns P90 minus P10 to its `95m05` raster. |
| `profile_area` | Elev profile area | Match | Uses all returns, clamps negative heights before linear percentile interpolation, and normalizes at P99. |
| `iq` | Elev/Int IQ | Corrected divergence | Exact for elevation. FUSION's intensity IQ is derived from its faulty intensity percentile path. |
| `l1` | Elev/Int L1 | Match | Sample L-moment formula and height selection match. |
| `l2` | Elev/Int L2 | Match | Sample L-moment formula and height selection match. |
| `l3` | Elev/Int L3 | Match | Sample L-moment formula and height selection match. |
| `l4` | Elev/Int L4 | Match | Sample L-moment formula and height selection match. |
| `lcv` | Elev/Int L-CV | Match | L2 / L1. |
| `lskewness` | Elev/Int L-skewness | Match | L3 / L2. |
| `lkurtosis` | Elev/Int L-kurtosis | Match | L4 / L2. |
| `mode` | Elev/Int mode | Match | Exact 64-bin scale, representative value, and lowest-bin tie selection. |
| `median` | Elev/Int P50 | Corrected divergence | Exact for elevation. For intensity it is the correct filtered P50, whereas FUSION's emitted intensity P50 follows its faulty percentile path. FUSION has no separately named median output. |
| `min` | Elev/Int minimum | Match | Direct selected-population minimum. |
| `max` | Elev/Int maximum | Match | Direct selected-population maximum. |
| `stddev` | Elev/Int standard deviation | Match | Sample SD, denominator `n - 1`. |
| `cv` | Elev/Int CV | Match | Sample SD divided by mean. |
| `canopy_relief_ratio` | Elev canopy relief ratio | Match | Default constant-cell result is FUSION's `0`. The optional `nodata` behavior is an explicit Silvimetric extension. |
| `sqmean` | Elev quadratic mean | Match | `sqrt(sum(x^2) / n)`. |
| `cumean` | Elev cubic mean | Match | `cuberoot(abs(sum(x^3)) / n)`. |
| `mean` | Elev/Int mean | Match | Arithmetic mean; all-zero non-empty cells correctly yield zero. |
| `variance` | Elev/Int variance | Match | Sample variance, denominator `n - 1`. |
| `skewness` | Elev/Int skewness | Match | FUSION product-moment form using sample SD. |
| `kurtosis` | Elev/Int kurtosis | Match | FUSION product-moment form using sample SD; not excess kurtosis. |
| `aad` | Elev/Int AAD | Match | Mean absolute deviation from the arithmetic mean. |
| `mad_median` | Elev MAD median | Match | Median absolute deviation from P50. |
| `mad_mode` | Elev MAD mode | Match | Median absolute deviation from the FUSION 64-bin mode. |
| `mad_mean` | — | Extension | Median absolute deviation from the mean; FUSION GridMetrics does not emit this metric. |
| `all_count` | `totalall` | Match | Total return count, including returns below `min_ht`. |
| `all_count_above_minht` | `count` | Match | All returns strictly above `min_ht`. |
| `all_count_above_htbreak` | `allcount` | Match | All returns strictly above the height break. |
| `all_count_above_mean` | `allcountmean` | Match | All returns above the mean of the `min_ht`-selected population. |
| `all_count_above_mode` | `allcountmode` | Match | All returns above the mode of the `min_ht`-selected population. |
| `1st_count` | `totalfirst` | Match | Total first-return count. |
| `1st_count_above_htbreak` | first returns above height break | Match | Strictly above the configured height break. |
| `1st_count_above_mean` | `fcountmean` | Match | First returns above the selected-population mean. |
| `1st_count_above_mode` | `fcountmode` | Match | First returns above the selected-population mode. |
| `r1_count` | `r1count` | Match | Return number 1 above `min_ht`. |
| `r2_count` | `r2count` | Match | Return number 2 above `min_ht`. |
| `r3_count` | `r3count` | Match | Return number 3 above `min_ht`. |
| `r4_count` | `r4count` | Match | Return number 4 above `min_ht`. |
| `r5_count` | `r5count` | Match | Return number 5 above `min_ht`. |
| `r6_count` | `r6count` | Match | Return number 6 above `min_ht`. |
| `r7_count` | `r7count` | Match | Return number 7 above `min_ht`. |
| `r8_count` | `r8count` | Match | Return number 8 above `min_ht`. |
| `r9_count` | `r9count` | Match | Return number 9 above `min_ht`. |
| `rother_count` | `rothercount` | Match | Return numbers outside 1 through 9 above `min_ht`. |
| `1st_density_above_htbreak` | `densityabove / densitytotal` | Match | First returns above height break divided by all first returns. |
| `all_density_above_htbreak` | `allcover / 100` | Match | All returns above height break divided by all returns. |
| `all_1st_density_above_htbreak` | `afcover / 100` | Match | All returns above height break divided by all first returns. |
| `kde_mode_count`, `kde_min_mode`, `kde_max_mode`, `kde_mode_range` | `/kde` outputs | Match | Optional 512-step Gaussian KDE, including FUSION's smoothing and peak selection rules. |
| `canopy_fuel_weight`, `canopy_bulk_density`, `canopy_base_height`, `canopy_height` | `/fuel` outputs | Match | Optional FUSION canopy fuel equations using P10/P25/P50/P75/P90, CV, maximum, and first-return density. |
| `strata_*`, `intstrata_*` | `/strata`, `/intstrata` | Match | Configurable lower-inclusive/upper-exclusive height strata, including count, proportion, and FUSION thresholded descriptive statistics. |
| `all_cover_above_htbreak` | `allcover` / Cover3mAll | Match | All returns above break divided by all returns, expressed as percent. |
| `1st_cover_above_htbreak` | `cover` / Cover3m1st | Match | First returns above break divided by all first returns, expressed as percent. |
| `all_1st_cover_above_htbreak` | `afcover` / ARbyFR | Match | All returns above break divided by all first returns, expressed as percent. |
| `all_cover_above_mean` | `allabovemean` | Match | All returns above mean divided by all returns, expressed as percent. |
| `all_cover_above_mode` | `allabovemode` | Match | All returns above mode divided by all returns, expressed as percent. |
| `1st_cover_above_mean` | `abovemean` | Match | First returns above mean divided by all first returns, expressed as percent. |
| `1st_cover_above_mode` | `abovemode` | Match | First returns above mode divided by all first returns, expressed as percent. |
| `all_1st_cover_above_mean` | `afabovemean` | Match | All returns above mean divided by all first returns, expressed as percent. |
| `all_1st_cover_above_mode` | `afabovemode` | Match | All returns above mode divided by all first returns, expressed as percent. |
| `Strata-1` | FUSION/LDV stratum 1 | Match | `< 1.37 m`; matches `/strata:1.37,5,10,15,20`. |
| `Strata-2` | FUSION/LDV stratum 2 | Match | `1.37 <= h < 5 m`. |
| `Strata-3` | FUSION/LDV stratum 3 | Match | `5 <= h < 10 m`. |
| `Strata-4` | FUSION/LDV stratum 4 | Match | `10 <= h < 15 m`. |
| `Strata-5` | FUSION/LDV stratum 5 | Match | `15 <= h < 20 m`. |
| `Strata-6` | FUSION/LDV stratum 6 | Match | `>= 20 m`. FUSION's default strata break list is different. |

## FUSION metrics or controls not represented by a Silvimetric metric

| FUSION capability | Silvimetric status | Consequence |
|---|---|---|
| `r8count`, `r9count`, `rothercount` | Implemented | Silvimetric now exposes `r8_count`, `r9_count`, and `rother_count`. |
| Density fractions | Implemented | The first/all/all-over-first height-break, mean, and mode density fractions complement percentage cover metrics. `densitycell` remains a raster-density unit, not a fraction. |
| KDE mode count, minimum mode, maximum mode, mode range | Implemented | Enable with `kde_window` and optionally tune `kde_multiplier`. |
| Fuel-model outputs | Implemented | Enable with `fuel_models=True`. |
| `first` (apply first-return filter to every metric) | Configuration gap | Silvimetric has first-return metrics but no global GridMetrics-equivalent selection mode. |
| `minpts` (default 3) | Implemented | `min_points=3` is the default and applies FUSION's strict `CellCount > minpts` criterion to descriptive metrics. |
| `minht` / `htmin` default | Implemented | `min_ht=-99999.0` is now the FUSION-compatible default. |
| Arbitrary `/strata` and `/intstrata` statistics | Implemented | Pass `strata_breaks` or `intensity_strata_breaks`. |
| RGB/NIR intensity source | Implemented | Pass `intensity_key='Red'`, `'Green'`, `'Blue'`, or `'Infrared'`. |

## Recommended next implementation work

1. Make floating metric failures/insufficient observations write the configured
   no-data value (`-9999`) rather than a floating NaN where FUSION would emit
   no-data.
2. Produce a new FUSION reference raster set from the supplied COPC with a
   recorded command line and the same source-side filters as Silvimetric. That
   will allow whole-raster pixel equality rather than the current stable-pixel
   baseline plus bounded comparison.

## Sources reviewed

- `fusion/fusion_src/code/github/FUSION_Metrics/GridMetrics/GridMetrics.cpp`
- `src/silvimetric/resources/metrics/*.py`
- Direct local COPC-to-TileDB `shatter` and `extract` comparison run
