"""FUSION/LDV-compatible vegetation-strata metrics.

These metrics operate on ground-normalized heights (``Z`` after Silvimetric's
legacy HAG-to-Z ferry, or ``HeightAboveGround`` directly). Their names preserve
the requested FUSION/LDV output spelling.
"""

import numpy as np

from ..attribute import Attributes as A
from ..metric import Metric
STRATA_BREAKS_M = (1.37, 5.0, 10.0, 15.0, 20.0)


def _in_stratum(lower: float | None, upper: float | None):
    """Build a FUSION /strata-compatible percentage function.

    FUSION uses an inclusive lower bound and exclusive upper bound. The first
    stratum has no lower bound and the final stratum has no upper bound.
    """

    def metric(data) -> float:
        in_range = True
        if lower is not None:
            in_range = data >= lower
        if upper is not None:
            in_range = in_range & (data < upper)
        if data.count() == 0:
            return float('nan')
        return in_range.sum() / data.count() * 100.0

    return metric


def get_fusion_ldv_metrics(elev_key: str = 'Z') -> dict[str, Metric]:
    """Return FUSION/LDV output metrics not already in GridMetrics.

    Silvimetric already supplies FUSION's 3 m cover values through
    ``all_cover_above_htbreak`` (Cover3mAll),
    ``1st_cover_above_htbreak`` (Cover3m1st), and
    ``all_1st_cover_above_htbreak`` (ARbyFR). The additions here are the six
    vegetation strata. ``elev_key`` must identify a ground-normalized height
    dimension.
    """
    if elev_key not in {'Z', 'HeightAboveGround'}:
        raise ValueError('elev_key must be Z or HeightAboveGround')

    metrics: dict[str, Metric] = {}

    stratum_bounds = (
        (None, STRATA_BREAKS_M[0]),
        (STRATA_BREAKS_M[0], STRATA_BREAKS_M[1]),
        (STRATA_BREAKS_M[1], STRATA_BREAKS_M[2]),
        (STRATA_BREAKS_M[2], STRATA_BREAKS_M[3]),
        (STRATA_BREAKS_M[3], STRATA_BREAKS_M[4]),
        (STRATA_BREAKS_M[4], None),
    )
    stratum_descriptions = (
        (
            'percentage of all returns below 1.37 m (FUSION does not impose '
            'a lower bound on its first stratum)'
        ),
        'percentage of all returns from 1.37 m inclusive to 5 m exclusive',
        'percentage of all returns from 5 m inclusive to 10 m exclusive',
        'percentage of all returns from 10 m inclusive to 15 m exclusive',
        'percentage of all returns from 15 m inclusive to 20 m exclusive',
        'percentage of all returns at or above 20 m',
    )
    for index, (bounds, description) in enumerate(
        zip(stratum_bounds, stratum_descriptions), start=1
    ):
        metrics[f'Strata-{index}'] = Metric(
            f'Strata-{index}',
            np.float32,
            _in_stratum(*bounds),
            attributes=[A[elev_key]],
            description=(
                f'FUSION/LDV strata metric: {description}. FUSION strata '
                'use an inclusive lower bound and exclusive upper bound.'
            ),
        )
    return metrics


fusion_ldv_metrics = get_fusion_ldv_metrics()
