"""Configurable FUSION GridMetrics elevation and intensity strata."""

from __future__ import annotations

import numpy as np

from ..attribute import Attributes as A
from ..metric import Metric
from .p_moments import m_kurtosis, m_mean, m_skewness, m_variance
from .stats import m_cv, m_max, m_median, m_min, m_mode, m_stddev


def _stratum_filter(elev_key: str, lower: float | None, upper: float | None):
    def select(data):
        selected = data
        if lower is not None:
            selected = selected[selected[elev_key] >= lower]
        if upper is not None:
            selected = selected[selected[elev_key] < upper]
        return selected

    return select


def _count(data, *args):
    return data.count()


def _proportion(data, total):
    if total == 0:
        return -9999.0
    return data.count() / total


def get_strata_metrics(
    elev_key: str,
    value_key: str,
    breaks: tuple[float, ...] | list[float],
    prefix: str,
):
    """Create FUSION's count, proportion, and statistics for each stratum."""
    breaks = tuple(float(value) for value in breaks)
    if not breaks or any(
        right <= left for left, right in zip(breaks, breaks[1:])
    ):
        raise ValueError('strata breaks must be a non-empty ascending sequence')

    bounds = [(None, breaks[0])]
    bounds.extend(zip(breaks[:-1], breaks[1:]))
    bounds.append((breaks[-1], None))
    metrics: dict[str, Metric] = {}
    methods = {
        'min': m_min,
        'max': m_max,
        'mean': m_mean,
        'mode': m_mode,
        'median': m_median,
        'stddev': m_stddev,
        'variance': m_variance,
        'skewness': m_skewness,
        'kurtosis': m_kurtosis,
    }
    for index, (lower, upper) in enumerate(bounds, start=1):
        name_prefix = f'{prefix}_{index}'
        select = _stratum_filter(elev_key, lower, upper)
        total = Metric(
            f'{name_prefix}_total', np.int32, _count, attributes=[A[value_key]]
        )
        count = Metric(
            f'{name_prefix}_count',
            np.int32,
            _count,
            filters=[select],
            attributes=[A[value_key]],
        )
        metrics[count.name] = count
        metrics[f'{name_prefix}_proportion'] = Metric(
            f'{name_prefix}_proportion',
            np.float32,
            _proportion,
            dependencies=[total],
            filters=[select],
            attributes=[A[value_key]],
        )
        local: dict[str, Metric] = {}
        for suffix, method in methods.items():
            local[suffix] = Metric(
                f'{name_prefix}_{suffix}',
                np.float32,
                method,
                filters=[select],
                attributes=[A[value_key]],
                # GridMetrics reports a stratum mean whenever it has a
                # point, but reserves all other descriptive values for
                # strata containing more than two points.
                minimum_points=None if suffix == 'mean' else 2,
            )
            metrics[local[suffix].name] = local[suffix]
        metrics[f'{name_prefix}_cv'] = Metric(
            f'{name_prefix}_cv',
            np.float32,
            m_cv,
            dependencies=[local['stddev'], local['mean']],
            filters=[select],
            attributes=[A[value_key]],
        )
    return metrics
