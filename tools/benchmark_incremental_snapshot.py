#!/usr/bin/env python3
"""Copy a sealed Becker phase to private S3, then benchmark consolidation.

Run ``copy`` and ``benchmark`` separately. Stop all canonical writers before
copying and do not start the next append until the copy manifest says
``copy_complete``. Both operations can be re-entered after interruption.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from concurrent.futures import ThreadPoolExecutor
from datetime import datetime, timezone
from urllib.parse import urlparse

import boto3
import silvimetric as sm
from botocore.exceptions import ClientError

from silvimetric.resources.build_ledger import BuildLedger


def split_uri(uri: str) -> tuple[str, str]:
    parsed = urlparse(uri)
    if parsed.scheme != 's3' or not parsed.netloc or not parsed.path.strip('/'):
        raise ValueError(f'Expected a nonempty s3://bucket/prefix URI: {uri}')
    return parsed.netloc, parsed.path.strip('/')


def inventory(s3, bucket: str, prefix: str) -> list[tuple[str, int, str]]:
    found = []
    for page in s3.get_paginator('list_objects_v2').paginate(
        Bucket=bucket, Prefix=prefix.rstrip('/') + '/'
    ):
        for item in page.get('Contents', []):
            found.append((
                item['Key'][len(prefix.rstrip('/') + '/'):],
                int(item['Size']), item['ETag'].strip('"'),
            ))
    return sorted(found)


def inventory_hash(items: list[tuple[str, int, str]]) -> str:
    return hashlib.sha256(
        json.dumps(items, separators=(',', ':')).encode()
    ).hexdigest()


def manifest_location(snapshot_uri: str) -> tuple[str, str]:
    bucket, prefix = split_uri(snapshot_uri)
    return bucket, prefix.rstrip('/') + '.snapshot.json'


def read_manifest(s3, snapshot_uri: str) -> dict | None:
    bucket, key = manifest_location(snapshot_uri)
    try:
        body = s3.get_object(Bucket=bucket, Key=key)['Body'].read()
    except ClientError as error:
        if error.response['Error']['Code'] in {'NoSuchKey', '404'}:
            return None
        raise
    return json.loads(body)


def write_manifest(s3, snapshot_uri: str, manifest: dict) -> None:
    bucket, key = manifest_location(snapshot_uri)
    s3.put_object(
        Bucket=bucket, Key=key,
        Body=(json.dumps(manifest, sort_keys=True, indent=2) + '\n').encode(),
        ContentType='application/json', ServerSideEncryption='AES256',
    )


def assert_private_bucket(s3, bucket: str) -> None:
    flags = s3.get_public_access_block(Bucket=bucket)[
        'PublicAccessBlockConfiguration'
    ]
    if not all(flags.get(key, False) for key in (
        'BlockPublicAcls', 'IgnorePublicAcls', 'BlockPublicPolicy',
        'RestrictPublicBuckets',
    )):
        raise ValueError('Snapshot bucket must block all public access')


def assert_quiescent_source(
    source_uri: str, ledger_uri: str, build_id: str,
) -> None:
    storage = sm.Storage.from_db(source_uri)
    ledger = BuildLedger(ledger_uri, sm.Storage.get_tdb_context(storage))
    manifest = ledger.build_manifest()
    if (
        manifest is None
        or manifest.details.get('build_id') != build_id
        or manifest.details.get('canonical_uri', '').rstrip('/')
        != source_uri.rstrip('/')
    ):
        raise ValueError('Source array and build ledger do not match')
    seal = ledger.state('__build__')
    if seal is None or seal.state != 'build_sealed':
        raise ValueError('Source phase must be sealed before copying')
    slot = int(manifest.details['time_slot'])
    if storage.config.next_time_slot != slot + 1:
        raise ValueError('A later phase has already reserved the canonical array')
    history = storage.get_shatter_meta(slot)
    if str(history.name) != build_id or not history.finished:
        raise ValueError('Canonical history does not show the sealed phase')


def copy_snapshot(
    s3, source_uri: str, snapshot_uri: str, ledger_uri: str,
    build_id: str, workers: int,
) -> dict:
    source_bucket, source_prefix = split_uri(source_uri)
    snapshot_bucket, snapshot_prefix = split_uri(snapshot_uri)
    if source_bucket == snapshot_bucket and (
        snapshot_prefix.startswith(source_prefix + '/')
        or source_prefix.startswith(snapshot_prefix + '/')
        or source_prefix == snapshot_prefix
    ):
        raise ValueError('Source and snapshot prefixes may not contain each other')
    assert_private_bucket(s3, snapshot_bucket)
    assert_quiescent_source(source_uri, ledger_uri, build_id)
    source = inventory(s3, source_bucket, source_prefix)
    if not source:
        raise ValueError('Source TileDB array prefix is empty')
    fingerprint = inventory_hash(source)
    manifest = read_manifest(s3, snapshot_uri)
    if manifest is None:
        if inventory(s3, snapshot_bucket, snapshot_prefix):
            raise ValueError('Snapshot prefix exists without a copy manifest')
        manifest = {
            'schema_version': 1, 'state': 'copying', 'build_id': build_id,
            'source_uri': source_uri, 'snapshot_uri': snapshot_uri,
            'ledger_uri': ledger_uri, 'source_inventory_sha256': fingerprint,
            'source_object_count': len(source),
            'source_bytes': sum(size for _, size, _ in source),
            'started_at': datetime.now(timezone.utc).isoformat(),
        }
        write_manifest(s3, snapshot_uri, manifest)
    elif (
        manifest['build_id'] != build_id
        or manifest['source_uri'] != source_uri
        or manifest['ledger_uri'] != ledger_uri
        or manifest['source_inventory_sha256'] != fingerprint
    ):
        raise ValueError('Source or build changed since snapshot copy began')
    if manifest['state'] in {'benchmark_started', 'benchmarked'}:
        raise ValueError('Snapshot is already in or past benchmark maintenance')

    present = {
        key: size for key, size, _ in inventory(
            s3, snapshot_bucket, snapshot_prefix
        )
    }

    def copy_one(item: tuple[str, int, str]) -> None:
        relative, size, etag = item
        if present.get(relative) == size:
            return
        if size > 5 * 1024**3:
            raise ValueError(f'TileDB object needs multipart copy: {relative}')
        key = source_prefix + '/' + relative
        s3.copy_object(
            Bucket=snapshot_bucket,
            Key=snapshot_prefix + '/' + relative,
            CopySource={'Bucket': source_bucket, 'Key': key},
            CopySourceIfMatch='"' + etag + '"',
            ServerSideEncryption='AES256',
        )

    with ThreadPoolExecutor(max_workers=workers) as pool:
        list(pool.map(copy_one, source))
    if inventory_hash(inventory(s3, source_bucket, source_prefix)) != fingerprint:
        raise RuntimeError('Canonical source changed during snapshot copy')
    target = inventory(s3, snapshot_bucket, snapshot_prefix)
    if [(key, size) for key, size, _ in target] != [
        (key, size) for key, size, _ in source
    ]:
        raise RuntimeError('Snapshot inventory does not match canonical source')
    manifest['state'] = 'copy_complete'
    manifest['copy_completed_at'] = datetime.now(timezone.utc).isoformat()
    manifest['snapshot_object_count'] = len(target)
    manifest['snapshot_bytes'] = sum(size for _, size, _ in target)
    write_manifest(s3, snapshot_uri, manifest)
    return manifest


def benchmark_snapshot(s3, snapshot_uri: str, fragment_size_mb: int) -> dict:
    manifest = read_manifest(s3, snapshot_uri)
    if manifest is None or manifest['state'] not in {
        'copy_complete', 'benchmark_started', 'benchmarked',
    }:
        raise ValueError('Snapshot copy must complete before benchmarking')
    if manifest['state'] == 'benchmarked':
        return manifest
    if manifest['state'] == 'copy_complete':
        manifest['state'] = 'benchmark_started'
        write_manifest(s3, snapshot_uri, manifest)
    result = sm.consolidate_macro_v4_snapshot(
        snapshot_uri, manifest['ledger_uri'], manifest['build_id'],
        fragment_size_mb=fragment_size_mb,
    )
    manifest['benchmark'] = result
    manifest['state'] = 'benchmarked'
    manifest['benchmark_completed_at'] = datetime.now(timezone.utc).isoformat()
    write_manifest(s3, snapshot_uri, manifest)
    return manifest


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='action', required=True)
    copy = sub.add_parser('copy')
    copy.add_argument('--source-uri', required=True)
    copy.add_argument('--snapshot-uri', required=True)
    copy.add_argument('--ledger-uri', required=True)
    copy.add_argument('--build-id', required=True)
    copy.add_argument('--workers', type=int, default=12)
    benchmark = sub.add_parser('benchmark')
    benchmark.add_argument('--snapshot-uri', required=True)
    benchmark.add_argument('--fragment-size-mb', type=int, default=300)
    args = parser.parse_args()
    if args.action == 'copy':
        if args.workers < 1 or args.workers > 32:
            parser.error('--workers must be between 1 and 32')
        result = copy_snapshot(
            boto3.client('s3'), args.source_uri, args.snapshot_uri,
            args.ledger_uri, args.build_id, args.workers,
        )
    else:
        result = benchmark_snapshot(
            boto3.client('s3'), args.snapshot_uri, args.fragment_size_mb,
        )
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
