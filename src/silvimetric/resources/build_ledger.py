"""Durable, append-only records for resumable canonical-array builds.

The ledger deliberately avoids a shared mutable document.  Every transition is
an immutable JSON object beneath the build's ledger URI, so a worker dying
between a TileDB write and an acknowledgement cannot corrupt the coordination
state.  A later driver derives the state of each deterministic block from its
records and either resumes publication or creates a fresh stage attempt.
"""

from __future__ import annotations

import json
import time
import uuid
from dataclasses import dataclass
from pathlib import PurePosixPath
from typing import Any

import tiledb


_STATE_ORDER = {
    'planned': 0,
    'computing': 1,
    # A failed stage has no durable output receipt.  It remains resumable, and
    # may be replaced by a spatial ``split`` receipt when resource pressure
    # proves that the original unit is too large.
    'failed': 2,
    'staged': 2,
    # Publishing is an observation, not a durable completion.  A restart must
    # resume from the preceding staged receipt unless a published receipt was
    # successfully persisted.
    'publishing': 1,
    # ``split`` is a terminal state for a parent work unit only.  Its child
    # units become the active leaves of the build plan.
    'split': 4,
    'published': 5,
    'validated': 6,
}


def _join_uri(base: str, *parts: str) -> str:
    """Join local or object-store URIs without losing an ``s3://`` scheme."""
    if '://' in base:
        scheme, remainder = base.split('://', 1)
        return f'{scheme}://{PurePosixPath(remainder, *parts)}'
    return str(PurePosixPath(base, *parts))


@dataclass(frozen=True)
class BuildRecord:
    """One immutable build-state transition."""

    block_id: str
    state: str
    attempt_id: str
    created_at_ns: int
    details: dict[str, Any]
    uri: str | None = None

    @classmethod
    def from_json(cls, value: dict[str, Any], uri: str) -> 'BuildRecord':
        return cls(
            block_id=value['block_id'],
            state=value['state'],
            attempt_id=value['attempt_id'],
            created_at_ns=int(value['created_at_ns']),
            details=value.get('details', {}),
            uri=uri,
        )

    def to_json(self) -> dict[str, Any]:
        return {
            'block_id': self.block_id,
            'state': self.state,
            'attempt_id': self.attempt_id,
            'created_at_ns': self.created_at_ns,
            'details': self.details,
        }


class BuildLedger:
    """Append-only durable build records backed by TileDB VFS.

    ``uri`` may be a shared POSIX directory for local testing or an S3 prefix
    for production.  VFS is given the caller's TileDB context configuration so
    the ledger follows the same profile/region/no-sign policy as its array.
    """

    def __init__(self, uri: str, ctx: tiledb.Ctx | None = None):
        self.uri = uri.rstrip('/')
        self._context_config = dict(ctx.config()) if ctx is not None else None

    def _vfs(self) -> tiledb.VFS:
        if self._context_config is None:
            return tiledb.VFS()
        return tiledb.VFS(ctx=tiledb.Ctx(tiledb.Config(self._context_config)))

    def block_uri(self, block_id: str) -> str:
        return _join_uri(self.uri, 'blocks', block_id)

    def publish_commit_uri(self, block_id: str) -> str:
        """Return the immutable, per-block canonical-commit index prefix.

        The main transition log remains the source of truth for build state.
        This small side index closes the short recovery window between a
        verified TileDB write and the subsequent ``published`` transition:
        a worker which dies in that interval can be recovered without reading
        the canonical range a second time.
        """
        return _join_uri(self.uri, 'publish-commits', block_id)

    def _record_uri(self, record: BuildRecord) -> str:
        # The attempt and monotonic-ish timestamp make every record immutable,
        # including repeated observations of a stable state after recovery.
        name = (
            f'{record.created_at_ns:020d}-{record.state}-'
            f'{record.attempt_id}.json'
        )
        return _join_uri(self.block_uri(record.block_id), name)

    def append(
        self,
        block_id: str,
        state: str,
        *,
        attempt_id: str | None = None,
        **details: Any,
    ) -> BuildRecord:
        """Persist an immutable state transition and return its receipt."""
        if state not in _STATE_ORDER and state not in {
            'build_started',
            # All macro stages have been durably published to the canonical
            # array.  A scheduler-only process may now perform expensive
            # array maintenance without retaining the compute fleet.
            # No memory-heavy stage task remains in this driver.  This is an
            # intentionally repeatable observation: an infrastructure
            # watcher may use a newly appended receipt to retire a dedicated
            # stage-worker fleet while bounded canonical publishers continue.
            'build_stage_tasks_complete',
            'build_published',
            'build_consolidating',
            'build_sealed',
            'build_partial',
        }:
            raise ValueError(f'Unknown build ledger state {state!r}')
        record = BuildRecord(
            block_id=block_id,
            state=state,
            attempt_id=attempt_id or uuid.uuid4().hex,
            created_at_ns=time.time_ns(),
            details=details,
        )
        uri = self._record_uri(record)
        vfs = self._vfs()
        vfs.create_dir(self.block_uri(block_id))
        with vfs.open(uri, 'wb') as stream:
            stream.write(json.dumps(record.to_json(), sort_keys=True).encode())
        return BuildRecord(**{**record.__dict__, 'uri': uri})

    def append_publish_commit(
        self, block_id: str, *, attempt_id: str | None = None, **details: Any
    ) -> str:
        """Persist a verified canonical-write commit index record.

        This is intentionally a separate immutable document rather than an
        array metadata update: array metadata would introduce a global write
        hot spot among spatially independent publishers.  The document is
        written only *after* range/signature verification, so it is safe to
        use during reconciliation when a process never reached ``append`` for
        the normal ``published`` ledger transition.
        """
        attempt = attempt_id or uuid.uuid4().hex
        created_at_ns = time.time_ns()
        document = {
            'block_id': block_id,
            'attempt_id': attempt,
            'created_at_ns': created_at_ns,
            'details': details,
        }
        name = f'{created_at_ns:020d}-commit-{attempt}.json'
        uri = _join_uri(self.publish_commit_uri(block_id), name)
        vfs = self._vfs()
        vfs.create_dir(self.publish_commit_uri(block_id))
        with vfs.open(uri, 'wb') as stream:
            stream.write(json.dumps(document, sort_keys=True).encode())
        return uri

    def publish_commit(self, block_id: str) -> dict[str, Any] | None:
        """Return the latest verified canonical-commit index record."""
        vfs = self._vfs()
        prefix = self.publish_commit_uri(block_id)
        if not vfs.is_dir(prefix):
            return None
        commits: list[dict[str, Any]] = []
        for uri in vfs.ls(prefix):
            if not uri.endswith('.json'):
                continue
            with vfs.open(uri, 'rb') as stream:
                value = json.loads(stream.read())
            value['uri'] = uri
            commits.append(value)
        if not commits:
            return None
        return max(commits, key=lambda value: int(value['created_at_ns']))

    def records(self, block_id: str) -> list[BuildRecord]:
        """Read all immutable records for one planned block."""
        vfs = self._vfs()
        block_uri = self.block_uri(block_id)
        if not vfs.is_dir(block_uri):
            return []
        records = []
        for uri in vfs.ls(block_uri):
            if not uri.endswith('.json'):
                continue
            with vfs.open(uri, 'rb') as stream:
                records.append(
                    BuildRecord.from_json(json.loads(stream.read()), uri)
                )
        return sorted(records, key=lambda record: record.created_at_ns)

    def state(self, block_id: str) -> BuildRecord | None:
        """Return the highest durable state, preferring the latest receipt."""
        records = self.records(block_id)
        if not records:
            return None
        return max(
            records,
            key=lambda record: (
                _STATE_ORDER.get(record.state, -1),
                record.created_at_ns,
            ),
        )

    def summary(self, block_ids: list[str]) -> dict[str, Any]:
        """Return an auditable view without scanning unknown ledger keys."""
        states: dict[str, int] = {}
        records: dict[str, BuildRecord | None] = {}
        for block_id in block_ids:
            record = self.state(block_id)
            records[block_id] = record
            state = record.state if record is not None else 'missing'
            states[state] = states.get(state, 0) + 1
        return {
            'ledger_uri': self.uri,
            'block_count': len(block_ids),
            'states': states,
            'records': records,
        }

    def assert_build_identity(
        self,
        *,
        build_id: str,
        canonical_uri: str,
        build_signature: str,
        build_inputs: dict[str, Any] | None = None,
    ) -> bool:
        """Bind a ledger prefix to one intended canonical-array build.

        A ledger URI is a build-scoped resource, not a reusable coordination
        directory. This check prevents an operator from accidentally resuming
        a partially completed build with a different source, date, spatial
        plan, or destination. It returns ``True`` when this is a resume and
        ``False`` when the caller must write the initial manifest.

        The driver remains the single coordinator for a ledger. Append-only
        VFS objects intentionally do not try to provide a distributed lease;
        two independent drivers must not use the same ledger concurrently.
        """
        original = self.build_manifest()
        if original is None:
            return False

        original = original.details
        expected = {
            'build_id': build_id,
            'canonical_uri': canonical_uri.rstrip('/'),
            'build_signature': build_signature,
        }
        actual = {
            key: str(original.get(key, '')).rstrip('/')
            if key == 'canonical_uri'
            else str(original.get(key, ''))
            for key in expected
        }
        if actual != expected:
            raise ValueError(
                'The requested build does not match the existing ledger '
                f'manifest at {self.uri!r}. Use a new ledger URI for a new '
                f'build; existing={actual!r}, requested={expected!r}; '
                f'existing_inputs={original.get("build_inputs")!r}, '
                f'requested_inputs={build_inputs!r}.'
            )
        return True

    def build_manifest(self) -> BuildRecord | None:
        """Return the first immutable ``build_started`` manifest, if any."""
        manifests = [
            record
            for record in self.records('__build__')
            if record.state == 'build_started'
        ]
        return manifests[0] if manifests else None
