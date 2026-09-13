from types import SimpleNamespace

import pytest
import tiledb

from silvimetric import Storage


def test_tiledb_concurrency_override(monkeypatch):
    monkeypatch.setenv('SILVIMETRIC_TILEDB_CONCURRENCY', '4')

    context = Storage.get_tdb_context(None)

    assert context.config()['sm.compute_concurrency_level'] == '4'
    assert context.config()['sm.io_concurrency_level'] == '4'


def test_tiledb_s3_parallel_operations_can_be_bounded_per_storage(monkeypatch):
    monkeypatch.setenv('SILVIMETRIC_TILEDB_S3_MAX_PARALLEL_OPS', '12')
    storage = Storage.__new__(Storage)
    storage._context_overrides = {}
    assert (
        Storage.get_tdb_context(storage).config()['vfs.s3.max_parallel_ops']
        == '12'
    )

    storage.set_context_overrides(**{'vfs.s3.max_parallel_ops': 4})
    assert (
        Storage.get_tdb_context(storage).config()['vfs.s3.max_parallel_ops']
        == '4'
    )


def test_storage_serialization_excludes_open_reader():
    storage = Storage.__new__(Storage)
    reader = object()
    storage._reader = reader

    state = storage.__getstate__()

    assert storage._reader is reader
    assert state['_reader'] is None


@pytest.mark.parametrize('value', ['0', '-1', 'invalid'])
def test_tiledb_concurrency_override_rejects_invalid_values(monkeypatch, value):
    monkeypatch.setenv('SILVIMETRIC_TILEDB_CONCURRENCY', value)

    with pytest.raises(ValueError, match='must be a positive integer'):
        Storage.get_tdb_context(None)


def test_stage_consolidation_replans_after_each_merge(monkeypatch):
    """A later node from an old plan may overlap a newly merged fragment."""
    storage = Storage.__new__(Storage)
    storage.config = SimpleNamespace(tdb_dir='/tmp/stage.tdb')
    plans = iter(
        [
            [
                {'fragment_uris': ['/tmp/a', '/tmp/b']},
                {'fragment_uris': ['/tmp/c', '/tmp/d']},
            ],
            [{'fragment_uris': ['/tmp/ab', '/tmp/c']}],
            [],
        ]
    )
    submitted = []

    monkeypatch.setattr(
        storage, 'stage_consolidation_plan', lambda fragment_size_mb: next(plans)
    )
    monkeypatch.setattr(
        tiledb,
        'consolidate',
        lambda uri, fragment_uris: submitted.append((uri, fragment_uris)),
    )

    assert storage.consolidate_stage_plan(300) == 2
    assert submitted == [
        ('/tmp/stage.tdb', ['a', 'b']),
        ('/tmp/stage.tdb', ['ab', 'c']),
    ]


def test_maintenance_operations_preserve_s3_context(monkeypatch):
    """Vacuum/consolidation must retain the configured S3 region and profile."""
    storage = Storage.__new__(Storage)
    storage.config = SimpleNamespace(tdb_dir='s3://example/array')
    monkeypatch.setenv('SILVIMETRIC_TILEDB_S3_REGION', 'us-west-2')
    calls = []

    monkeypatch.setattr(
        tiledb,
        'vacuum',
        lambda uri, ctx, config: calls.append(('vacuum', uri, config)),
    )
    monkeypatch.setattr(
        tiledb,
        'consolidate',
        lambda uri, ctx, config: calls.append(('consolidate', uri, config)),
    )

    storage.vacuum('commits')
    storage.consolidate('array_meta')

    assert [(operation, uri) for operation, uri, _ in calls] == [
        ('vacuum', 's3://example/array'),
        ('consolidate', 's3://example/array'),
    ]
    assert all(config['vfs.s3.region'] == 'us-west-2' for _, _, config in calls)
    assert calls[0][2]['sm.vacuum.mode'] == 'commits'
    assert calls[1][2]['sm.consolidation.mode'] == 'array_meta'
