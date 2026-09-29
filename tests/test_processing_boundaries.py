"""Demonstrate why PDAL terrain metrics need a frozen processing grid."""

import json
from pathlib import Path

import pandas as pd
import pdal


def test_smrf_hag_neighbourhood_changes_when_a_work_unit_is_bisected():
    """The same points get different HAG values after a window split.

    The 20-unit reader collar is held fixed. Only the processing-core
    boundary changes. This regression guards against treating a Batch
    block-plan mismatch as a TileDB or execution-engine pixel bug.
    """
    source = Path(__file__).parent / 'data/autzen-small.copc.laz'

    def heights(minx: int, maxx: int) -> pd.DataFrame:
        stages = [
            {
                'type': 'readers.copc',
                'filename': str(source),
                'bounds': f'([{minx - 20},{maxx + 20}],[849980,851020])',
            },
            {'type': 'filters.ferry', 'dimensions': 'Z=>OriginalZ'},
            {
                'type': 'filters.sort',
                'dimensions': 'X,Y,Z,ReturnNumber,NumberOfReturns,Intensity',
            },
            {'type': 'filters.smrf'},
            {'type': 'filters.hag_nn'},
            {
                'type': 'filters.crop',
                'bounds': f'([{minx},{maxx}],[850000,851000])',
            },
        ]
        pipeline = pdal.Pipeline(json.dumps({'pipeline': stages}))
        pipeline.execute()
        points = pipeline.arrays[0]
        return pd.DataFrame(
            {name: points[name] for name in (
                'X', 'Y', 'OriginalZ', 'HeightAboveGround'
            )}
        )

    wide = heights(636500, 637500)
    half = heights(636500, 637000)
    wide = wide.loc[wide.X < 637000]
    half = half.loc[half.X < 637000]
    same_points = wide.merge(
        half,
        on=['X', 'Y', 'OriginalZ'],
        suffixes=('_wide', '_half'),
        validate='one_to_one',
    )
    assert len(same_points) == len(wide) == len(half)

    changed = (
        same_points.HeightAboveGround_wide
        - same_points.HeightAboveGround_half
    ).abs() > 1e-6
    assert changed.any()
    # The effects are not confined to the immediate collar. A 20-unit
    # margin does not make this terrain pipeline partition-invariant.
    assert (changed & (same_points.X < 636900)).any()
