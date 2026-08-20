import copy
import numpy as np

from ..attribute import Attributes as A
from ..metric import Metric
from .percentiles import percentiles
from .l_moments import l_moments
from .stats import statistics
from .p_moments import product_moments
from .fusion_metadata import apply_fusion_metadata
from .aad import aad


# Make special crr. It relies too heavily on other metrics
# that will have filters applied to them.
def _grid_crr(data, *args):
    mean = data.mean()
    data_min = data.min()
    data_max = data.max()

    den = data_max - data_min
    if den == 0:
        return 0.0

    return (mean - data_min) / den


def _make_grid_crr_metric(constant: str) -> Metric:
    if constant not in {'zero', 'nodata'}:
        raise ValueError(
            "canopy_relief_ratio_constant must be either 'zero' or 'nodata'"
        )

    def grid_crr(data, *args):
        value = _grid_crr(data, *args)
        if constant == 'nodata' and data.min() == data.max():
            return -9999.0
        return value

    return Metric('canopy_relief_ratio', np.float32, grid_crr)


def f_z_gt_val(data, elev_key, val):
    return data[data[elev_key] > val]


def make_elev_filter(val, elev_key):
    return lambda data, elev_key=elev_key, val=val: f_z_gt_val(
        data, elev_key, val
    )


def _get_grid_metrics(
    elev_key='Z',
    intensity_key='Intensity',
    canopy_relief_ratio_constant='zero',
    kde_window=None,
    kde_multiplier=1.0,
    fuel_models=False,
    strata_breaks=None,
    intensity_strata_breaks=None,
):
    """
    Return FUSION GridMetrics Metrics dependent on an Elevation key.
    GridMetrics operate upon Intensity and an Elevation attribute.
    For PDAL in SilviMetric, elevation will be Z or HeightAboveGround.
    https://pdal.io/en/latest/dimensions.html#dimensions
    """
    from .covers import get_cover_metrics
    from .counts import get_count_metrics
    from .densities import get_density_metrics
    from .fusion_extras import get_fuel_metrics, get_kde_metrics
    from .fusion_ldv import get_fusion_ldv_metrics
    from .strata import get_strata_metrics

    # prevent this muckery from being infectious
    covers = copy.deepcopy(get_cover_metrics(elev_key))
    counts = copy.deepcopy(get_count_metrics(elev_key))
    densities = copy.deepcopy(get_density_metrics(elev_key))
    pcts = copy.deepcopy(percentiles)
    lmom = copy.deepcopy(l_moments)
    pmom = copy.deepcopy(product_moments)
    stats = copy.deepcopy(statistics)
    aad_copy = copy.deepcopy(aad)
    fusion_ldv = copy.deepcopy(get_fusion_ldv_metrics(elev_key))

    if elev_key not in {'Z', 'HeightAboveGround'}:
        raise ValueError('elev_key must be Z or HeightAboveGround')
    if intensity_key not in {'Intensity', 'Red', 'Green', 'Blue', 'Infrared'}:
        raise ValueError(
            'intensity_key must be Intensity, Red, Green, Blue, or Infrared'
        )
    for m in (pcts | lmom | pmom).values():
        m.attributes = [A[elev_key], A[intensity_key]]
        for d in m.dependencies:
            d.attributes = [A[elev_key], A[intensity_key]]

    # give profile_area separate pct_base so we can apply separate filters
    pcts['profile_area'].attributes = [A[elev_key]]
    pcts['iq'].attributes = [A[elev_key], A[intensity_key]]

    stats['cumean'].attributes = [A[elev_key]]
    stats['sqmean'].attributes = [A[elev_key]]

    stats['min'].attributes = [A[elev_key], A[intensity_key]]
    stats['max'].attributes = [A[elev_key], A[intensity_key]]
    stats['mode'].attributes = [A[elev_key], A[intensity_key]]
    stats['median'].attributes = [A[elev_key], A[intensity_key]]
    stats['stddev'].attributes = [A[elev_key], A[intensity_key]]
    stats['cv'].attributes = [A[elev_key], A[intensity_key]]
    stats['crr'] = _make_grid_crr_metric(canopy_relief_ratio_constant)
    stats['crr'].attributes = [A[elev_key]]

    aad_copy['aad'].attributes = [A[elev_key], A[intensity_key]]
    aad_copy['mad_median'].attributes = [A[elev_key]]
    aad_copy['mad_mean'].attributes = [A[elev_key]]
    aad_copy['mad_mode'].attributes = [A[elev_key]]

    grid_metrics: dict[str, Metric] = dict(
        pcts
        | lmom
        | stats
        | pmom
        | aad_copy
        | counts
        | covers
        | densities
        | fusion_ldv
    )
    if strata_breaks is not None:
        grid_metrics.update(
            get_strata_metrics(elev_key, elev_key, strata_breaks, 'strata')
        )
    if intensity_strata_breaks is not None:
        grid_metrics.update(
            get_strata_metrics(
                elev_key,
                intensity_key,
                intensity_strata_breaks,
                'intstrata',
            )
        )
    if kde_window is not None:
        grid_metrics.update(
            get_kde_metrics(elev_key, kde_window, kde_multiplier)
        )
    if fuel_models:
        grid_metrics.update(get_fuel_metrics(grid_metrics, elev_key))
    return grid_metrics


def get_grid_metrics(
    elev_key='Z',
    min_ht=-99999.0,
    ht_break=3,
    canopy_relief_ratio_constant='zero',
    min_points=3,
    intensity_key='Intensity',
    kde_window=None,
    kde_multiplier=1.0,
    fuel_models=False,
    strata_breaks=None,
    intensity_strata_breaks=None,
):
    """Get FUSION GridMetrics with filters and documentation applied.

    Parameters
    ----------
    canopy_relief_ratio_constant : {'zero', 'nodata'}, default 'zero'
        Value reported by ``canopy_relief_ratio`` in constant-valued cells.
        ``'zero'`` matches FUSION GridMetrics; ``'nodata'`` writes -9999.
    min_points : int, default 3
        FUSION's ``/minpts`` setting. Descriptive metrics require strictly
        more than this many height-selected points.
    intensity_key : {'Intensity', 'Red', 'Green', 'Blue', 'Infrared'}
        Point dimension used for FUSION's optional intensity/RGB/NIR metric
        family. The default is the LAS Intensity dimension.
    kde_window, kde_multiplier : float, optional
        Enable FUSION's `/kde:window,multiplier` outputs when ``kde_window``
        is supplied.
    fuel_models : bool, default False
        Include FUSION's optional canopy fuel-model output metrics.
    strata_breaks, intensity_strata_breaks : sequence of float, optional
        Enable FUSION `/strata` and `/intstrata` statistics respectively.
    """
    # cover metrics use the ht_break, all others use min_ht
    if min_points is not None and min_points < 0:
        raise ValueError('min_points must be non-negative or None')
    grid_metrics = _get_grid_metrics(
        elev_key,
        intensity_key,
        canopy_relief_ratio_constant,
        kde_window,
        kde_multiplier,
        fuel_models,
        strata_breaks,
        intensity_strata_breaks,
    )
    no_dep_filter_list = [
        'all_cover_above_htbreak',
        'all_cover_above_mean',
        'all_cover_above_mode',
        '1st_density_above_htbreak',
        'all_density_above_htbreak',
        'all_1st_density_above_htbreak',
        '1st_density_above_mean',
        '1st_density_above_mode',
        'all_density_above_mean',
        'all_density_above_mode',
        'all_1st_density_above_mean',
        'all_1st_density_above_mode',
        'canopy_fuel_weight',
        'canopy_bulk_density',
        'canopy_base_height',
        'canopy_height',
        '1st_cover_above_htbreak',
        'all_1st_cover_above_htbreak',
        'all_1st_density_above_htbreak',
        'all_1st_cover_above_mean',
        'all_1st_cover_above_mode',
        'profile_area',
    ]
    ht_break_list = [
        'all_cover_above_htbreak',
        '1st_cover_above_htbreak',
        '1st_density_above_htbreak',
        'all_density_above_htbreak',
        '1st_count_above_htbreak',
        'all_count_above_htbreak',
    ]
    no_filter_list = [
        '1st_count_above_mean',
        '1st_count_above_mode',
        '1st_cover_above_mean',
        '1st_cover_above_mode',
        'all_1st_cover_above_htbreak',
        'all_1st_cover_above_mean',
        'all_1st_cover_above_mode',
        'all_cover_above_mean',
        'all_cover_above_mode',
        '1st_density_above_mean',
        '1st_density_above_mode',
        # The all-over-first density is evaluated on all first returns.  Its
        # nested numerator, rather than the metric input, gets ht_break.
        'all_1st_density_above_htbreak',
        'all_density_above_mean',
        'all_density_above_mode',
        'all_1st_density_above_mean',
        'all_1st_density_above_mode',
        'all_count_above_mean',
        'all_count_above_mode',
        'all_count',
        '1st_count',
        # FUSION vegetation strata partition all returns, including those
        # below GridMetrics' configurable min_ht threshold.
        'Strata-1',
        'Strata-2',
        'Strata-3',
        'Strata-4',
        'Strata-5',
        'Strata-6',
        'profile_area',
    ]
    min_height = min_ht
    height_break = ht_break
    min_ht = make_elev_filter(min_height, elev_key)
    ht_break = make_elev_filter(height_break, elev_key)
    for gm in grid_metrics.values():
        is_configurable_stratum = gm.name.startswith(('strata_', 'intstrata_'))
        if gm.name in no_filter_list or is_configurable_stratum:
            if gm.name in {
                'all_1st_cover_above_htbreak',
                'all_1st_density_above_htbreak',
            }:
                # This metric is FUSION's ARbyFR: its denominator is all
                # first returns and must remain unfiltered, but the nested
                # numerator is all returns above ht_break.
                for dependency in gm.dependencies:
                    if dependency.name == 'all_count_above_htbreak':
                        dependency.add_filter(ht_break)
                    if dependency.name == 'all_1st_density_hbreak_numerator':
                        dependency.add_filter(ht_break)
            continue

        filter_fn = ht_break if gm.name in ht_break_list else min_ht

        if gm.name not in no_dep_filter_list:
            for d in gm.dependencies:
                if filter_fn not in d.filters:
                    d.filters.append(filter_fn)

        if filter_fn not in gm.filters:
            gm.add_filter(filter_fn)

    unguarded = {
        'all_count',
        'all_count_above_minht',
        'all_count_above_htbreak',
        '1st_count',
        '1st_count_above_htbreak',
        *{f'r{return_number}_count' for return_number in range(1, 10)},
        'rother_count',
        'all_cover_above_htbreak',
        '1st_cover_above_htbreak',
        'all_1st_cover_above_htbreak',
        'all_density_above_htbreak',
        '1st_density_above_htbreak',
        'all_1st_density_above_htbreak',
        # Profile area has its own FUSION condition (at least one point and a
        # positive P99), independent of /minpts.
        'profile_area',
        *{f'Strata-{index}' for index in range(1, 7)},
    }
    for metric in grid_metrics.values():
        if metric.name not in unguarded and not metric.name.startswith(
            ('strata_', 'intstrata_')
        ):
            metric.minimum_points = min_points

    apply_fusion_metadata(
        grid_metrics,
        elev_key=elev_key,
        min_ht=min_height,
        ht_break=height_break,
        canopy_relief_ratio_constant=canopy_relief_ratio_constant,
        min_points=min_points,
        intensity_key=intensity_key,
    )
    return grid_metrics
