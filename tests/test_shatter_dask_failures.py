from types import SimpleNamespace

import pytest

from silvimetric.commands import shatter as shatter_command


class _FailingClient:
    def __init__(self, future):
        self.future = future

    def submit(self, *args, **kwargs):
        return self.future


class _Storage:
    def consolidate(self, **kwargs):
        pytest.fail('must not consolidate a partial distributed shatter result')


def test_run_raises_when_a_distributed_task_fails(monkeypatch):
    """A Dask task error must not be converted into an incomplete success."""
    failure = ValueError('simulated PDAL failure')
    future = SimpleNamespace(status='error', key='shatter-leaf-1')
    monkeypatch.setattr(
        shatter_command, 'get_client', lambda: _FailingClient(future)
    )
    monkeypatch.setattr(
        shatter_command,
        'as_completed',
        lambda futures, **kwargs: [(future, failure)],
    )

    config = SimpleNamespace(point_count=0, date=None)
    with pytest.raises(
        RuntimeError,
        match=(
            r'(?s)1 Dask shatter task\(s\) failed.*'
            r'shatter-leaf-1.*simulated PDAL failure'
        ),
    ) as error:
        shatter_command.run(iter([object()]), config, _Storage())

    assert isinstance(error.value.__cause__, ValueError)


def test_run_reports_a_non_exception_dask_failure_payload(monkeypatch):
    future = SimpleNamespace(status='error', key='shatter-leaf-1')
    payload = ('deserialize', 'tiledb context creation failed')
    monkeypatch.setattr(
        shatter_command, 'get_client', lambda: _FailingClient(future)
    )
    monkeypatch.setattr(
        shatter_command,
        'as_completed',
        lambda futures, **kwargs: [(future, payload)],
    )

    config = SimpleNamespace(point_count=0, date=None)
    with pytest.raises(
        RuntimeError, match='tiledb context creation failed'
    ) as error:
        shatter_command.run(iter([object()]), config, _Storage())

    assert error.value.__cause__ is None
