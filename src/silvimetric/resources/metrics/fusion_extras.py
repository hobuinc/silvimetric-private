"""Optional FUSION GridMetrics analyses not included in the base family."""

from __future__ import annotations

import math

import numpy as np

from ..attribute import Attributes as A
from ..metric import Metric


def _fusion_kde(values, window: float, multiplier: float):
    """Translate FUSION's 512-step GaussianKDE calculation."""
    values = np.asarray(values, dtype=float)
    point_count = len(values)
    if point_count < 10:
        return (0, 0.0, 0.0, 0.0)

    data_min = values.min()
    data_max = values.max()
    data_range = data_max - data_min
    if data_range < window or data_range < 3.0:
        return (0, 0.0, 0.0, 0.0)

    stddev = np.std(values, ddof=1)
    iq = np.percentile(values, 75) - np.percentile(values, 25)
    bandwidth = 0.9 * (min(stddev, iq) / 1.34) * point_count**-0.2
    bandwidth *= multiplier
    if bandwidth <= 0 or not np.isfinite(bandwidth):
        return (0, 0.0, 0.0, 0.0)

    steps = 512
    lower = data_min - bandwidth * 3.0
    upper = data_max + bandwidth * 3.0
    x = np.linspace(lower, upper, steps)
    y = np.exp(-((x[:, None] - values[None, :]) ** 2) / (2 * bandwidth**2)).sum(
        axis=1
    ) / (point_count * math.sqrt(2 * math.pi * bandwidth**2))

    smooth_x = x.copy()
    smooth_y = y.copy()
    use_smoothed_curve = window != 0.0
    if use_smoothed_curve:
        half_window = int(window / (x[1] - x[0]))
        if half_window % 2 == 0:
            half_window += 1

        # This is a direct translation of GridMetrics::GaussianKDE.  Its
        # edge windows deliberately grow/shrink rather than use a uniform
        # clipped window.
        for index in range(half_window):
            end_half_window = index
            smooth_y[index] = y[
                max(0, index - end_half_window) : index + end_half_window + 1
            ].mean()

        cell_count = 0
        average = 0.0
        for index in range(half_window, steps - half_window):
            if index == half_window:
                values_in_window = y[
                    index - half_window : index + half_window + 1
                ]
                average = values_in_window.sum()
                cell_count = len(values_in_window)
            else:
                average -= y[index - half_window - 1]
                average += y[index + half_window]
            smooth_y[index] = average / cell_count

        for index in range(
            steps - 1, max(half_window, steps - half_window) - 1, -1
        ):
            end_half_window = steps - 1 - index
            smooth_y[index] = y[index - end_half_window : min(
                steps, index + end_half_window + 1
            )].mean()

    curve = smooth_y if use_smoothed_curve else y
    signs = np.zeros(steps, dtype=np.int8)
    signs[0] = -1
    signs[-1] = 1
    first_minimum = False
    for index in range(steps):
        if data_min <= smooth_x[index] <= data_max:
            if not first_minimum:
                signs[index] = -1
                first_minimum = True
            elif curve[index] > curve[index - 1]:
                signs[index] = 1
            elif curve[index] < curve[index - 1]:
                signs[index] = -1
            else:
                signs[index] = 0

    extrema = []
    for index in range(1, steps):
        if signs[index] != signs[index - 1]:
            extrema.append((x[index - 1], signs[index - 1]))
        if len(extrema) >= 63:
            return (0, 0.0, 0.0, 0.0)
    modes = [
        value
        for value, kind in extrema
        if kind == 1 and data_min <= value <= data_max
    ]
    if not modes:
        return (0, 0.0, 0.0, 0.0)
    return (len(modes), min(modes), max(modes), max(modes) - min(modes))


def get_kde_metrics(elev_key: str, window: float, multiplier: float):
    """Return FUSION-compatible optional KDE summary metrics."""
    if window < 0 or multiplier <= 0:
        raise ValueError(
            'kde_window must be non-negative and kde_multiplier positive'
        )

    def kde(data, *args):
        return _fusion_kde(data, window, multiplier)

    base = Metric('fusion_kde_base', object, kde, attributes=[A[elev_key]])
    return {
        'kde_mode_count': Metric(
            'kde_mode_count',
            np.int32,
            lambda data, values: values[0],
            [base],
            attributes=[A[elev_key]],
        ),
        'kde_min_mode': Metric(
            'kde_min_mode',
            np.float32,
            lambda data, values: values[1],
            [base],
            attributes=[A[elev_key]],
        ),
        'kde_max_mode': Metric(
            'kde_max_mode',
            np.float32,
            lambda data, values: values[2],
            [base],
            attributes=[A[elev_key]],
        ),
        'kde_mode_range': Metric(
            'kde_mode_range',
            np.float32,
            lambda data, values: values[3],
            [base],
            attributes=[A[elev_key]],
        ),
    }


def get_fuel_metrics(metrics: dict[str, Metric], elev_key: str):
    """Return FUSION's optional canopy fuel-model metric outputs."""

    dependencies = {
        'weight': [
            metrics['p25'],
            metrics['p90'],
            metrics['1st_density_above_htbreak'],
        ],
        'bulk': [
            metrics['cv'],
            metrics['p10'],
            metrics['p25'],
            metrics['p90'],
            metrics['1st_density_above_htbreak'],
        ],
        'base': [
            metrics['cv'],
            metrics['p10'],
            metrics['p50'],
            metrics['p75'],
            metrics['1st_density_above_htbreak'],
        ],
        'height': [
            metrics['max'],
            metrics['p25'],
            metrics['p50'],
            metrics['p75'],
            metrics['1st_density_above_htbreak'],
        ],
    }

    def weight(data, p25, p90, first_density):
        if first_density <= 0:
            return -9999.0
        return (22.7 + 2.9 * p25 - 1.7 * p90 + 106.6 * first_density) ** 2

    def bulk(data, cv, p10, p25, p90, first_density):
        if first_density <= 0:
            return -9999.0
        return (
            math.exp(
                -4.3
                + 3.2 * cv
                + 0.02 * p10
                + 0.13 * p25
                - 0.12 * p90
                + 2.4 * first_density
            )
            * 1.037
        )

    def base(data, cv, p10, p50, p75, first_density):
        if first_density <= 0:
            return -9999.0
        return (
            3.2
            + 19.3 * cv
            + 0.7 * p10
            + 2.0 * p50
            - 1.8 * p75
            - 8.8 * first_density
        )

    def height(data, maximum, p25, p50, p75, first_density):
        if first_density <= 0:
            return -9999.0
        return (
            2.8
            + 0.25 * maximum
            + 0.25 * p25
            - p50
            + 1.5 * p75
            + 3.5 * first_density
        )

    methods = {
        'canopy_fuel_weight': (weight, dependencies['weight']),
        'canopy_bulk_density': (bulk, dependencies['bulk']),
        'canopy_base_height': (base, dependencies['base']),
        'canopy_height': (height, dependencies['height']),
    }
    return {
        name: Metric(name, np.float32, method, deps, attributes=[A[elev_key]])
        for name, (method, deps) in methods.items()
    }
