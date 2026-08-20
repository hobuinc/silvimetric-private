"""FUSION GridMetrics density products expressed as fractions, not percent."""

import numpy as np

from ..attribute import Attributes as A
from ..metric import Metric
from .counts import count_fn, first_returns_filter, get_count_metrics


def density(data, *args):
    numerator = args[0]
    denominator = data.count()
    if denominator == 0:
        return np.nan
    return numerator / denominator


def filtered_density(data, *args):
    denominator = args[0]
    if denominator == 0:
        return np.nan
    return data.count() / denominator


def get_density_metrics(elev_key='Z'):
    """Return FUSION's first, all, and all-over-first density products."""
    counts = get_count_metrics(elev_key)
    all_first_hbreak_numerator = Metric(
        'all_1st_density_hbreak_numerator',
        np.int32,
        count_fn,
        attributes=[A['ReturnNumber']],
    )
    counts[all_first_hbreak_numerator.name] = all_first_hbreak_numerator

    def metric(name, dependency, attributes, filters=None, method=density):
        return Metric(
            name,
            np.float32,
            method,
            dependencies=[counts[dependency]],
            attributes=attributes,
            filters=filters,
        )

    return {
        '1st_density_above_htbreak': metric(
            '1st_density_above_htbreak',
            '1st_count_above_htbreak',
            [A[elev_key]],
            [first_returns_filter],
            filtered_density,
        ),
        'all_density_above_htbreak': metric(
            'all_density_above_htbreak',
            'all_count_above_htbreak',
            [A[elev_key]],
            method=filtered_density,
        ),
        'all_1st_density_above_htbreak': metric(
            'all_1st_density_above_htbreak',
            'all_1st_density_hbreak_numerator',
            [A[elev_key]],
            [first_returns_filter],
        ),
        '1st_density_above_mean': metric(
            '1st_density_above_mean',
            '1st_count_above_mean',
            [A[elev_key]],
            [first_returns_filter],
        ),
        '1st_density_above_mode': metric(
            '1st_density_above_mode',
            '1st_count_above_mode',
            [A[elev_key]],
            [first_returns_filter],
        ),
        'all_density_above_mean': metric(
            'all_density_above_mean',
            'all_count_above_mean',
            [A[elev_key]],
        ),
        'all_density_above_mode': metric(
            'all_density_above_mode',
            'all_count_above_mode',
            [A[elev_key]],
        ),
        'all_1st_density_above_mean': metric(
            'all_1st_density_above_mean',
            'all_count_above_mean',
            [A[elev_key]],
            [first_returns_filter],
        ),
        'all_1st_density_above_mode': metric(
            'all_1st_density_above_mode',
            'all_count_above_mode',
            [A[elev_key]],
            [first_returns_filter],
        ),
    }
