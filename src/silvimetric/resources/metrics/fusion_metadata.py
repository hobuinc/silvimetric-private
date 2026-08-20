"""FUSION GridMetrics provenance attached to Silvimetric metric objects.

The definitions in this module are derived from FUSION's GridMetrics source
and command help.  They deliberately travel with a :class:`Metric` so a
processing service can present the same explanation that a user would find in
FUSION, without maintaining a second metric catalogue.
"""

from __future__ import annotations

from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from ..metric import Metric


_COMMON = {
    'source': 'FUSION GridMetrics',
    'documentation': (
        'FUSION GridMetrics command help and GridMetrics.cpp metric '
        'implementation.'
    ),
    'support': 'FUSION / USDA Forest Service metric definitions',
}

_DEFINITIONS = {
    'min': 'Minimum selected value in the grid cell.',
    'max': 'Maximum selected value in the grid cell.',
    'mean': 'Arithmetic mean of selected values in the grid cell.',
    'median': '50th percentile of selected values in the grid cell.',
    'mode': (
        "Mode from FUSION's 64 scaled bins. Values are scaled from the "
        'cell minimum to maximum into indexes 0 through 63; the lowest '
        'most-frequent bin is reported at its lower scaled position.'
    ),
    'stddev': 'Sample standard deviation: sqrt(sum((x - mean)^2) / (n - 1)).',
    'variance': 'Sample variance: sum((x - mean)^2) / (n - 1).',
    'cv': (
        'Coefficient of variation: sample standard deviation divided by mean.'
    ),
    'skewness': (
        'FUSION product-moment skewness: sum((x - mean)^3) / '
        '((n - 1) * sample_standard_deviation^3).'
    ),
    'kurtosis': (
        'FUSION product-moment kurtosis: sum((x - mean)^4) / '
        '((n - 1) * sample_standard_deviation^4).'
    ),
    'sqmean': 'Quadratic mean: sqrt(sum(x^2) / n).',
    'cumean': 'Cubic mean: cube_root(abs(sum(x^3)) / n).',
    'canopy_relief_ratio': (
        'Canopy relief ratio: (mean - minimum) / (maximum - minimum). '
        'FUSION writes 0 for a constant-valued cell.'
    ),
    'aad': 'Average absolute deviation from the arithmetic mean.',
    'mad_median': 'Median absolute deviation from the median.',
    'mad_mean': 'Median absolute deviation from the arithmetic mean.',
    'mad_mode': 'Median absolute deviation from the FUSION 64-bin mode.',
    'iq': 'Interquartile distance: P75 minus P25.',
    '90m10': 'Percentile distance: P90 minus P10.',
    '95m05': 'Percentile distance: P95 minus P05.',
    'profile_area': (
        'Profile area under the P00 through P99 height-percentile curve, '
        'normalized by P99 and integrated with the composite trapezoid rule. '
        'Negative heights are clamped to zero before interpolation.'
    ),
    'l1': 'First L-moment.',
    'l2': 'Second L-moment.',
    'l3': 'Third L-moment.',
    'l4': 'Fourth L-moment.',
    'lcv': 'L-coefficient of variation: L2 / L1.',
    'lskewness': 'L-skewness: L3 / L2.',
    'lkurtosis': 'L-kurtosis: L4 / L2.',
    'all_count': 'Count of all returns in the cell.',
    'all_count_above_minht': (
        'Count of all returns above the configured minimum height.'
    ),
    'all_count_above_htbreak': (
        'Count of all returns above the configured height break.'
    ),
    'all_count_above_mean': (
        'Count of all returns above the mean calculated from the selected '
        'minimum-height population.'
    ),
    'all_count_above_mode': (
        'Count of all returns above the mode calculated from the selected '
        'minimum-height population.'
    ),
    '1st_count': 'Count of first returns in the cell.',
    '1st_count_above_htbreak': (
        'Count of first returns above the configured height break.'
    ),
    '1st_count_above_mean': (
        'Count of first returns above the selected-population mean.'
    ),
    '1st_count_above_mode': (
        'Count of first returns above the selected-population mode.'
    ),
    'rother_count': (
        'Count of return numbers outside the FUSION return-number bins 1 '
        'through 9, above the configured minimum height.'
    ),
    '1st_density_above_htbreak': (
        'First returns above the configured height break divided by all '
        'first returns.'
    ),
    'all_density_above_htbreak': (
        'All returns above the configured height break divided by all returns.'
    ),
    'all_1st_density_above_htbreak': (
        'All returns above the configured height break divided by all first '
        'returns.'
    ),
    '1st_density_above_mean': (
        'First returns above the selected-population mean divided by all '
        'first returns.'
    ),
    '1st_density_above_mode': (
        'First returns above the selected-population mode divided by all '
        'first returns.'
    ),
    'all_density_above_mean': (
        'All returns above the selected-population mean divided by all returns.'
    ),
    'all_density_above_mode': (
        'All returns above the selected-population mode divided by all returns.'
    ),
    'all_1st_density_above_mean': (
        'All returns above the selected-population mean divided by all first '
        'returns.'
    ),
    'all_1st_density_above_mode': (
        'All returns above the selected-population mode divided by all first '
        'returns.'
    ),
    'kde_mode_count': 'Number of KDE peaks found by FUSION GaussianKDE.',
    'kde_min_mode': 'Minimum height of a KDE peak found by GaussianKDE.',
    'kde_max_mode': 'Maximum height of a KDE peak found by GaussianKDE.',
    'kde_mode_range': 'Maximum KDE peak height minus minimum KDE peak height.',
    'canopy_fuel_weight': 'FUSION canopy fuel weight regression estimate.',
    'canopy_bulk_density': (
        'FUSION canopy bulk-density regression estimate, including the '
        'log-transformation correction.'
    ),
    'canopy_base_height': 'FUSION canopy base-height regression estimate.',
    'canopy_height': 'FUSION canopy-height regression estimate.',
    'all_cover_above_htbreak': (
        'Cover3mAll: percent of all returns above the configured height break.'
    ),
    '1st_cover_above_htbreak': (
        'Cover3m1st: percent of first returns above the configured height '
        'break.'
    ),
    'all_1st_cover_above_htbreak': (
        'ARbyFR: all returns above the configured height break divided by '
        'all first returns, expressed as a percentage.'
    ),
    'all_cover_above_mean': (
        'Percent of all returns above the selected-population mean.'
    ),
    'all_cover_above_mode': (
        'Percent of all returns above the selected-population mode.'
    ),
    '1st_cover_above_mean': (
        'Percent of first returns above the selected-population mean.'
    ),
    '1st_cover_above_mode': (
        'Percent of first returns above the selected-population mode.'
    ),
    'all_1st_cover_above_mean': (
        'All returns above mean divided by all first returns, as a percentage.'
    ),
    'all_1st_cover_above_mode': (
        'All returns above mode divided by all first returns, as a percentage.'
    ),
}


def _definition(name: str) -> str:
    if name.startswith('p') and len(name) == 3 and name[1:].isdigit():
        return f'{int(name[1:])}th percentile of selected values in the cell.'
    if name.startswith('r') and name.endswith('_count') and name[1].isdigit():
        return (
            f'Count of return number {name[1]} above the configured '
            'minimum height.'
        )
    if name.startswith('strata_'):
        return (
            'FUSION /strata configurable height-stratum metric. Strata are '
            'lower-inclusive and upper-exclusive, except for the unbounded '
            'first and last strata.'
        )
    if name.startswith('intstrata_'):
        return (
            'FUSION /intstrata configurable height-stratum metric calculated '
            'from the selected intensity, RGB, or NIR dimension.'
        )
    if name.startswith('Strata-'):
        return (
            'Percentage of all returns within this FUSION/LDV vegetation '
            'stratum.'
        )
    return _DEFINITIONS.get(name, f'FUSION-compatible definition for {name}.')


def _output_names(metric: 'Metric') -> list[str]:
    return [
        metric.entry_name(attribute.name) for attribute in metric.attributes
    ]


def apply_fusion_metadata(
    metrics: dict[str, 'Metric'],
    *,
    elev_key: str,
    min_ht: float,
    ht_break: float,
    canopy_relief_ratio_constant: str,
    min_points: int | None,
    intensity_key: str,
) -> None:
    """Attach complete FUSION-oriented documentation to grid metrics."""
    for metric in metrics.values():
        definition = _definition(metric.name)
        parameters: dict[str, object] = {
            'elevation_dimension': elev_key,
            'minimum_height': min_ht,
            'height_break': ht_break,
            'minimum_points': min_points,
            'intensity_dimension': intensity_key,
        }
        if metric.name == 'canopy_relief_ratio':
            parameters['constant_cell_value'] = (
                0.0 if canopy_relief_ratio_constant == 'zero' else -9999.0
            )

        metric.metadata = {
            'fusion': {
                **_COMMON,
                'definition': definition,
                'output_names': _output_names(metric),
                'parameters': parameters,
            }
        }
        if metric.description is None:
            metric.description = definition
