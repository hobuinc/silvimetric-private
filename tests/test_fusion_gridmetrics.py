from pathlib import Path

from fusion_gridmetrics import (
    FUSION_RASTER_PRODUCTS,
    generated_metric_map,
    gridmetrics_command,
)


def test_gridmetrics_command_uses_normalized_lfs_source_configuration():
    command = gridmetrics_command(
        ['GridMetrics.exe'],
        Path('/tmp/fusion-source.las'),
        Path('/tmp/fusion-output'),
    )

    assert command[0] == 'GridMetrics.exe'
    assert '/noground' in command
    assert '/nointdtm' in command
    assert '/ascii' in command
    assert '/buffer:15' in command
    assert '/minht:2' in command
    assert '/minpts:3' in command
    assert '/gridxy:635550,4402350,635850,4402800' in command
    assert f'/raster:{FUSION_RASTER_PRODUCTS}' in command
    assert command[-4:] == [
        '2',
        '30',
        'fusion',
        'fusion-source.las',
    ]


def test_generated_fusion_outputs_map_back_to_silvimetric_rasters():
    metric_map = generated_metric_map(
        Path('/tmp/fusion-output'), Path('/tmp/silvimetric-output')
    )

    assert (
        metric_map[
            Path(
                '/tmp/fusion-output/'
                'fusion_all_returns_all_metrics_elevation_mean.asc'
            )
        ]
        == Path('/tmp/silvimetric-output/m_Z_mean.tif')
    )
    assert (
        metric_map[
            Path(
                '/tmp/fusion-output/'
                'fusion_all_returns_all_metrics_intensity_p95.asc'
            )
        ]
        == Path('/tmp/silvimetric-output/m_Intensity_p95.tif')
    )
    assert (
        metric_map[
            Path(
                '/tmp/fusion-output/'
                'fusion_all_returns_all_metrics_elevation_return_9_count.asc'
            )
        ]
        == Path('/tmp/silvimetric-output/m_ReturnNumber_r9_count.tif')
    )
