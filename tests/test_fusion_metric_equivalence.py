import numpy as np
import pandas as pd

from silvimetric.resources.metric import Metric
from silvimetric.resources.config import StorageConfig
from silvimetric.resources.extents import Bounds
from silvimetric.resources.metrics.grid_metrics import get_grid_metrics
from silvimetric.resources.metrics.p_moments import (
    m_kurtosis,
    m_mean,
    m_skewness,
)
from silvimetric.resources.metrics.percentiles import m_profile_area
from silvimetric.resources.metrics.stats import m_cumean, m_mode, m_stddev
from silvimetric.resources.taskgraph import Graph


def test_product_moments_use_fusion_sample_standard_deviation():
    values = pd.Series([0.0, 1.0, 2.0, 5.0])
    mean = values.mean()
    stddev = np.std(values, ddof=1)

    assert np.isclose(m_stddev(values), stddev)
    assert np.isclose(
        m_skewness(values), ((values - mean) ** 3).sum() / (3 * stddev**3)
    )
    assert np.isclose(
        m_kurtosis(values), ((values - mean) ** 4).sum() / (3 * stddev**4)
    )
    assert m_mean(pd.Series([0.0, 0.0])) == 0.0


def test_mode_cubic_mean_and_profile_area_match_fusion_algorithms():
    # The two final values share FUSION's 63rd scaled bin. NumPy's histogram
    # would return the lower bin edge (0.984375), while FUSION returns 1.0.
    assert m_mode(pd.Series([0.0, 0.8, 0.81, 1.0, 1.0, 1.0])) == 1.0

    cubic_values = pd.Series([-2.0, 1.0])
    assert np.isclose(m_cumean(cubic_values), np.cbrt(3.5))

    profile_values = pd.Series([-3.0, 1.0, 2.0, 3.0])
    p = np.percentile(np.maximum(profile_values.to_numpy(), 0.0), range(100))
    expected_area = (p[0] / p[99] + (2.0 * p[1:99] / p[99]).sum() + 1.0) * 0.5
    assert np.isclose(m_profile_area(profile_values), expected_area)


def test_cover_above_mean_and_mode_use_all_returns_as_denominator():
    data = pd.DataFrame(
        {
            'xi': [0] * 5,
            'yi': [0] * 5,
            # The selected minimum-height population is [2, 5, 6].  The
            # all-return denominator for both cover metrics remains 5.
            'Z': [0.0, 1.0, 2.0, 5.0, 6.0],
            'Intensity': [1.0] * 5,
            'ReturnNumber': [1, 1, 2, 1, 2],
        }
    )
    metrics = get_grid_metrics('Z', min_ht=1, ht_break=3, min_points=2)
    selected = [
        metrics[name]
        for name in (
            'mode',
            'all_count_above_mean',
            'all_count_above_mode',
            'all_cover_above_mean',
            'all_cover_above_mode',
        )
    ]
    result = Graph(selected).run(data)

    assert result.at[(0, 0), 'm_Z_all_count_above_mean'] == 2
    assert result.at[(0, 0), 'm_Z_all_count_above_mode'] == 2
    assert result.at[(0, 0), 'm_Z_all_cover_above_mean'] == 40.0
    assert result.at[(0, 0), 'm_Z_all_cover_above_mode'] == 40.0


def test_canopy_relief_ratio_defaults_to_zero_and_can_use_nodata():
    data = pd.DataFrame({'xi': [0] * 4, 'yi': [0] * 4, 'Z': [7.0] * 4})

    default_metric = get_grid_metrics()['crr']
    nodata_metric = get_grid_metrics(canopy_relief_ratio_constant='nodata')[
        'crr'
    ]

    assert (
        Graph(default_metric).run(data).at[(0, 0), 'm_Z_canopy_relief_ratio']
        == 0.0
    )
    assert (
        Graph(nodata_metric).run(data).at[(0, 0), 'm_Z_canopy_relief_ratio']
        == -9999.0
    )


def test_grid_metric_metadata_is_documented_and_serialized():
    metric = get_grid_metrics(
        'HeightAboveGround',
        min_ht=2.5,
        ht_break=3.5,
        canopy_relief_ratio_constant='nodata',
    )['crr']
    fusion = metric.metadata['fusion']

    assert fusion['source'] == 'FUSION GridMetrics'
    assert 'Canopy relief ratio' in fusion['definition']
    assert fusion['parameters']['elevation_dimension'] == 'HeightAboveGround'
    assert fusion['parameters']['constant_cell_value'] == -9999.0
    assert fusion['output_names'] == ['m_HeightAboveGround_canopy_relief_ratio']

    restored = Metric.from_dict(metric.to_json())
    assert restored.metadata == metric.metadata


def test_gridmetrics_controls_and_extensions_match_fusion_semantics():
    data = pd.DataFrame(
        {
            'xi': [0] * 10,
            'yi': [0] * 10,
            'Z': [-1.0, 0.0, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0],
            'Intensity': list(range(10, 20)),
            'Red': list(range(100, 110)),
            'ReturnNumber': [1, 2, 8, 9, 0, 10, 1, 2, 1, 2],
        }
    )

    # FUSION defaults to no minht; its descriptive values require CellCount
    # strictly greater than /minpts (the default /minpts is 3).
    defaults = get_grid_metrics()
    assert Graph(defaults['mean']).run(data).iloc[0, 0] == 3.5
    assert Graph(get_grid_metrics(min_ht=2)['mean']).run(data).iloc[0, 0] == 5.5
    assert Graph(defaults['mean']).run(data.iloc[:3]).iloc[0, 0] == -9999.0
    assert Graph(defaults['all_count']).run(data.iloc[:3]).iloc[0, 0] == 3

    counts = Graph(
        [defaults[name] for name in ('r8_count', 'r9_count', 'rother_count')]
    ).run(data)
    assert counts.at[(0, 0), 'm_ReturnNumber_r8_count'] == 1
    assert counts.at[(0, 0), 'm_ReturnNumber_r9_count'] == 1
    assert counts.at[(0, 0), 'm_ReturnNumber_rother_count'] == 2

    densities = Graph(
        [
            defaults['1st_density_above_htbreak'],
            defaults['all_density_above_htbreak'],
            defaults['all_1st_density_above_htbreak'],
        ]
    ).run(data)
    # Above the default 3 m break: first returns 6 and 8, and all values 4-8.
    assert densities.at[(0, 0), 'm_Z_1st_density_above_htbreak'] == 2 / 3
    assert densities.at[(0, 0), 'm_Z_all_density_above_htbreak'] == 5 / 10
    assert densities.at[(0, 0), 'm_Z_all_1st_density_above_htbreak'] == 5 / 3

    rgb = get_grid_metrics(intensity_key='Red')['mean']
    assert Graph(rgb).run(data).at[(0, 0), 'm_Red_mean'] == 104.5

    strata = get_grid_metrics(strata_breaks=[1.5, 5.5])
    result = Graph(
        [
            strata['strata_1_count'],
            strata['strata_2_count'],
            strata['strata_3_count'],
            strata['strata_1_proportion'],
            strata['strata_1_mean'],
            strata['strata_1_min'],
        ]
    ).run(data)
    assert result.at[(0, 0), 'm_Z_strata_1_count'] == 3
    assert result.at[(0, 0), 'm_Z_strata_2_count'] == 4
    assert result.at[(0, 0), 'm_Z_strata_3_count'] == 3
    assert result.at[(0, 0), 'm_Z_strata_1_proportion'] == 0.3
    assert result.at[(0, 0), 'm_Z_strata_1_mean'] == 0.0
    assert result.at[(0, 0), 'm_Z_strata_1_min'] == -1.0

    intensity_strata = get_grid_metrics(
        intensity_key='Red', intensity_strata_breaks=[1.5]
    )
    result = Graph(
        [
            intensity_strata['intstrata_1_count'],
            intensity_strata['intstrata_1_mean'],
            intensity_strata['intstrata_2_mean'],
        ]
    ).run(data)
    assert result.at[(0, 0), 'm_Red_intstrata_1_count'] == 3
    assert result.at[(0, 0), 'm_Red_intstrata_1_mean'] == 101.0
    assert result.at[(0, 0), 'm_Red_intstrata_2_mean'] == 106.0

    optional = get_grid_metrics(kde_window=2.5, fuel_models=True)
    result = Graph(
        [
            optional['kde_mode_count'],
            optional['kde_min_mode'],
            optional['canopy_fuel_weight'],
            optional['canopy_bulk_density'],
        ]
    ).run(data)
    assert result.at[(0, 0), 'm_Z_kde_mode_count'] >= 0
    assert np.isfinite(result.at[(0, 0), 'm_Z_kde_min_mode'])
    assert result.at[(0, 0), 'm_Z_canopy_fuel_weight'] > 0
    assert result.at[(0, 0), 'm_Z_canopy_bulk_density'] > 0


def test_storage_config_retains_rgb_dimension_selected_by_gridmetrics():
    metrics = list(get_grid_metrics(intensity_key='Red').values())
    config = StorageConfig(
        tdb_dir='/tmp/fusion-red-test',
        root=Bounds(minx=0, miny=0, maxx=30, maxy=30),
        crs='EPSG:26910',
        metrics=metrics,
    )
    assert 'Red' in {attribute.name for attribute in config.attrs}
