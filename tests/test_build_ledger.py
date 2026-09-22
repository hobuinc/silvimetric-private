import pytest

from silvimetric.resources.build_ledger import BuildLedger


def test_immutable_records_keep_a_staged_block_resumable(tmp_path):
    ledger = BuildLedger((tmp_path / 'ledger').as_posix())
    ledger.append('block-a', 'planned')
    ledger.append('block-a', 'computing')
    ledger.append('block-a', 'staged', stage_uri='file:///stage-a')
    ledger.append('block-a', 'publishing', stage_uri='file:///stage-a')

    # An interrupted publish is not a durable success. Recovery starts from
    # the preserved staged receipt rather than trusting the transient state.
    state = ledger.state('block-a')
    assert state is not None
    assert state.state == 'staged'
    assert state.details['stage_uri'] == 'file:///stage-a'
    assert len(ledger.records('block-a')) == 4


def test_build_identity_rejects_a_foreign_resume(tmp_path):
    ledger = BuildLedger((tmp_path / 'ledger').as_posix())
    assert not ledger.assert_build_identity(
        build_id='run-a',
        canonical_uri='file:///canonical',
        build_signature='signature-a',
    )
    ledger.append(
        '__build__',
        'build_started',
        build_id='run-a',
        canonical_uri='file:///canonical',
        build_signature='signature-a',
    )
    assert ledger.assert_build_identity(
        build_id='run-a',
        canonical_uri='file:///canonical/',
        build_signature='signature-a',
    )
    with pytest.raises(ValueError, match='does not match'):
        ledger.assert_build_identity(
            build_id='run-b',
            canonical_uri='file:///canonical',
            build_signature='signature-b',
        )


def test_publish_commit_index_recovers_verified_write_window(tmp_path):
    """The index survives a crash after verification but before receipt."""
    ledger = BuildLedger((tmp_path / 'ledger').as_posix())
    commit_uri = ledger.append_publish_commit(
        'block-a',
        stage_uri='file:///stage-a',
        schema_sha256='schema',
        signature={'point_count': 10, 'cell_count': 2},
    )

    commit = ledger.publish_commit('block-a')
    assert commit is not None
    # TileDB VFS normalizes a local path to ``file://`` when listing it.
    assert commit['uri'].removeprefix('file://') == commit_uri
    assert commit['details'] == {
        'stage_uri': 'file:///stage-a',
        'schema_sha256': 'schema',
        'signature': {'point_count': 10, 'cell_count': 2},
    }
