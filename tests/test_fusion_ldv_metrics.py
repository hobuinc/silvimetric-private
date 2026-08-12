import numpy as np
import pandas as pd

import silvimetric as sm
from silvimetric.resources.metrics.fusion_ldv import get_fusion_ldv_metrics
from silvimetric.resources.taskgraph import Graph


def test_fusion_ldv_cover_and_strata_metrics():
    """Match FUSION's strata-boundary semantics."""
    data = pd.DataFrame(
        {
            'xi': [0] * 10,
            'yi': [0] * 10,
            # Each strata boundary starts its succeeding stratum, except 20 m
            # which belongs to Strata-6.
            'Z': [0.0, 1.0, 1.37, 3.0, 4.0, 5.0, 10.0, 15.0, 20.0, 21.0],
            'ReturnNumber': [1, 1, 2, 1, 2, 1, 2, 1, 2, 2],
        }
    )

    metrics = get_fusion_ldv_metrics('Z')
    result = Graph(list(metrics.values())).run(data)

    expected_strata = (20.0, 30.0, 10.0, 10.0, 10.0, 20.0)
    actual_strata = tuple(
        result.at[(0, 0), f'm_Z_Strata-{index}']
        for index in range(1, 7)
    )
    assert np.allclose(actual_strata, expected_strata)
    assert np.isclose(sum(actual_strata), 100.0)


def test_fusion_ldv_metrics_are_in_grid_metrics_and_described():
    metrics = get_fusion_ldv_metrics('HeightAboveGround')

    assert set(metrics) == {f'Strata-{index}' for index in range(1, 7)}
    assert 'inclusive lower bound' in metrics['Strata-3'].description


def test_grid_metrics_preserve_cover_aliases_and_unfiltered_strata():
    """Existing cover metrics are FUSION's 3 m outputs at ht_break=3."""
    data = pd.DataFrame(
        {
            'xi': [0] * 10,
            'yi': [0] * 10,
            'Z': [0.0, 1.0, 1.37, 3.0, 4.0, 5.0, 10.0, 15.0, 20.0, 21.0],
            'ReturnNumber': [1, 1, 2, 1, 2, 1, 2, 1, 2, 2],
            'Intensity': [1] * 10,
        }
    )
    metrics = get_fusion_ldv_metrics('Z')
    grid_metrics = sm.grid_metrics.get_grid_metrics('Z', min_ht=2, ht_break=3)
    selected = [
        grid_metrics[name]
        for name in (
            'all_cover',
            '1st_cover',
            'all_1st_cover',
        )
    ] + list(metrics.values())
    result = Graph(selected).run(data)

    assert np.isclose(
        result.at[(0, 0), 'm_ReturnNumber_all_cover_above_htbreak'], 60.0
    )
    assert np.isclose(
        result.at[(0, 0), 'm_ReturnNumber_1st_cover_above_htbreak'], 40.0
    )
    assert np.isclose(
        result.at[
            (0, 0), 'm_ReturnNumber_all_1st_cover_above_htbreak'
        ],
        120.0,
    )
    assert np.isclose(result.at[(0, 0), 'm_Z_Strata-1'], 20.0)
