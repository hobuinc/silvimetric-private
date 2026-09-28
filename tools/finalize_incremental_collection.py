#!/usr/bin/env python3
"""Seal a complete incremental TileDB collection from its phase manifest.

The plan must contain exact USGS Albers pixel bounds, no unallocated
rectangles, and one sealed RunId/ledger URI per phase. This program performs
the count/coverage gate before TileDB consolidation and writes the resulting
report only after post-consolidation verification succeeds.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from datetime import datetime, timezone
from pathlib import Path

import silvimetric as sm

from silvimetric.resources.usgs_albers import (
    USGS_ALBERS_TOP_LEFT_X, USGS_ALBERS_TOP_LEFT_Y,
)


def target_bounds(pixels: list[int], resolution: int) -> sm.Bounds:
    if len(pixels) != 4 or any(not isinstance(value, int) for value in pixels):
        raise ValueError('Plan needs four integer half-open pixel indices')
    x1, y1, x2, y2 = pixels
    if x1 < 0 or y1 < 0 or x1 >= x2 or y1 >= y2:
        raise ValueError('Invalid target pixel rectangle')
    return sm.Bounds(
        USGS_ALBERS_TOP_LEFT_X + x1 * resolution,
        USGS_ALBERS_TOP_LEFT_Y - y2 * resolution,
        USGS_ALBERS_TOP_LEFT_X + x2 * resolution,
        USGS_ALBERS_TOP_LEFT_Y - y1 * resolution,
    )


def finalize(plan_path: Path, canonical_uri: str, report_path: Path) -> dict:
    raw = plan_path.read_bytes()
    plan = json.loads(raw)
    if plan.get('resource_id') != 'MN_BeckerCo_1_2021':
        raise ValueError('This runbook only seals MN_BeckerCo_1_2021')
    if plan.get('unallocated_rectangles'):
        raise ValueError('The collection plan still has unallocated rectangles')
    if plan.get('resolution_m') != 20:
        raise ValueError('Expected 20 m USGS Albers output')
    phases = plan.get('phases', [])
    if not phases or any(
        'run_id' not in phase or 'ledger_uri' not in phase
        for phase in phases
    ):
        raise ValueError('Every phase needs its sealed RunId and ledger URI')
    report = sm.consolidate_macro_v4_collection(
        canonical_uri,
        [(phase['ledger_uri'], phase['run_id']) for phase in phases],
        target_bounds(plan['full_pixel_bounds'], plan['resolution_m']),
        fragment_size_mb=300,
    )
    report.update({
        'resource_id': plan['resource_id'],
        'source_advertised_points': plan['source_advertised_points'],
        'plan_sha256': hashlib.sha256(raw).hexdigest(),
        'sealed_at': datetime.now(timezone.utc).isoformat(),
    })
    if report_path.exists():
        raise FileExistsError(f'Refusing to overwrite report {report_path}')
    report_path.write_text(json.dumps(report, indent=2, sort_keys=True) + '\n')
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--plan', type=Path, required=True)
    parser.add_argument('--canonical-uri', required=True)
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(
        finalize(args.plan, args.canonical_uri, args.report),
        indent=2, sort_keys=True,
    ))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
