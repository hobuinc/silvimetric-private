from types import SimpleNamespace

import pytest
import tiledb

from silvimetric import Storage


def test_tiledb_concurrency_override(monkeypatch):
    monkeypatch.setenv('SILVIMETRIC_TILEDB_CONCURRENCY', '4')

    context = Storage.get_tdb_context(None)

    assert context.config()['sm.compute_concurrency_level'] == '4'
    assert context.config()['sm.io_concurrency_level'] == '4'


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
