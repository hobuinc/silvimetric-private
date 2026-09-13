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
