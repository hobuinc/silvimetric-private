"""Offline checks for the read-only production quality gate."""

import json

import numpy as np
import pytest

from tools.validate_batch_v4 import (
    half_open_indices,
    load_receipts,
    s3_parts,
    select_windows,
    window_around,
)


def test_grid_bounds_are_half_open_and_require_alignment():
    root = (-2493045, -100, 100, 3310005)
    assert half_open_indices((20015, 2661345, 20215, 2661545), root, 20) == (
        125653, 32423, 125663, 32433
    )
    with pytest.raises(ValueError, match='not aligned'):
        half_open_indices((20016, 2661345, 20215, 2661545), root, 20)


def test_sample_windows_find_a_real_owner_boundary():
    count = np.ones((12, 12), dtype=np.uint64)
    count[5, 6] = 100_000
    owner = np.ones((12, 12), dtype=np.uint16)
    owner[:, 6:] = 2
    windows, dense = select_windows(count, owner, (100, 200, 112, 212))
    assert dense == (106, 205)
    assert set(windows) == {'dense', 'boundary', 'edge'}
    bx1, by1, bx2, by2 = windows['boundary']
    assert bx1 <= 105 < bx2 and bx1 <= 106 < bx2
    assert 100 <= bx1 < bx2 <= 112
    assert 200 <= by1 < by2 <= 212
    assert window_around(2, 3, (0, 0, 4, 4)) == (0, 0, 4, 4)


def test_s3_receipts_enforce_identity_and_seal():
    build_id = 'test-build'
    canonical = 's3://bucket/db/array'
    ledger = 's3://bucket/builds/test-build/ledger'
    prefix = 'builds/test-build/ledger/blocks/'
    documents = {
        prefix + 'block/000-published-a.json': {
            'block_id': 'block', 'state': 'published', 'created_at_ns': 1,
            'details': {'point_count': 5, 'stage_uri': 's3://bucket/stage', 'bounds': [0, 0, 1, 1]},
        },
        prefix + '__build__/000-build_started-a.json': {
            'block_id': '__build__', 'state': 'build_started', 'created_at_ns': 1,
            'details': {'build_id': build_id, 'canonical_uri': canonical},
        },
        prefix + '__build__/001-build_sealed-a.json': {
            'block_id': '__build__', 'state': 'build_sealed', 'created_at_ns': 2,
            'details': {'build_id': build_id, 'published_block_count': 1, 'point_count': 5},
        },
    }

    class FakeS3:
        def get_paginator(self, name):
            assert name == 'list_objects_v2'
            return self

        def paginate(self, **_kwargs):
            return [{'Contents': [{'Key': key} for key in documents]}]

        def get_object(self, *, Bucket, Key):
            assert Bucket == 'bucket'

            class Body:
                def read(self):
                    return json.dumps(documents[Key]).encode()

            return {'Body': Body()}

    assert s3_parts(ledger) == ('bucket', 'builds/test-build/ledger')
    published, seal = load_receipts(FakeS3(), ledger, build_id, canonical)
    assert len(published) == 1 and seal['point_count'] == 5
    with pytest.raises(ValueError, match='identity'):
        load_receipts(FakeS3(), ledger, 'wrong-build', canonical)
    documents[prefix + '__build__/001-build_sealed-a.json']['details']['point_count'] = 6
    with pytest.raises(ValueError, match='point count'):
        load_receipts(FakeS3(), ledger, build_id, canonical)
