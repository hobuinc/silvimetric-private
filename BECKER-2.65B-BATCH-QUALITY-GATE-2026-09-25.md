# Becker 2.65B Batch quality gate and billing checkpoint

Build `c1b65682-0081-4e14-bbc9-f19274502e64` is sealed. The read-only
validator in `tools/validate_batch_v4.py` independently checks its immutable
stage receipts, the canonical TileDB array, GDAL TileDB reads, Silvimetric
`extract`, and a raw-point scientific recomputation. It does not change S3 or
TileDB state. Its offline tests are in `tests/test_validate_batch_v4.py`.

## Results

| Check | Result |
| --- | ---: |
| Private CI for the pushed baseline, Linux/macOS × Python 3.12/3.14, GridMetrics, S3 test | All 7 jobs passed ([run 36083563174](https://github.com/hobuinc/silvimetric-private/actions/runs/36083563174)); the new validator has not yet run in CI |
| Local validator + shatter tests | 64 passed, 2 skipped |
| Published immutable stages | 61; each count and populated-cell total matches its receipt |
| Stage-owned cells with multiple owners | 0 |
| Full bounded stage/canonical count mismatch | 0 cells |
| Stage, canonical, and seal point total | 2,647,868,723 |
| Populated canonical cells | 214,282 |
| Dense, cross-stage-boundary, and occupied-edge windows | 5 selected metrics match immutable stages in each |
| GDAL TileDB direct reads | `count`, `m_Z_mean`, and `m_Z_l4` match in all three windows; pixel-is-area Albers transform verified |
| Silvimetric `extract` | `m_Z_mean` and `m_Z_l4` match in dense and boundary windows; pixel-is-area GeoTIFF georeferencing verified |

The densest sampled cell is at global `(X=125984, Y=32426)` and contains
124,805 points. Recomputing its mean and L-moments from the stored raw `Z`
values agrees with the `float32` metric values. Its stored `m_Z_mean` is
`8.5195503235`, compared with `8.5195499459` recomputed. Stored `m_Z_l4` is
`-0.5593578219`; the recomputed value is `-0.5593578403`. The three 8 × 8
windows were selected from live counts and stage ownership, not from manually
chosen locations. The boundary window straddles populated cells owned by
different stages. The occupied edge window has 58 populated cells; the other
two have 64 each. The full JSON diagnostic is currently at
`/private/tmp/becker-c1b65682-quality.json` on the local host.

To reproduce, set the `silvimetric-smoke` AWS profile and `us-west-2`, then
run the following command from this repository. The GDAL interpreter is
separate because the `silvimetric` and `sm-distributed` environments do not
contain the `libgdal-tiledb` plugin.

```shell
AWS_PROFILE=silvimetric-smoke AWS_REGION=us-west-2 AWS_DEFAULT_REGION=us-west-2 \
SILVIMETRIC_TILEDB_S3_MAX_PARALLEL_OPS=4 SILVIMETRIC_TILEDB_CONCURRENCY=4 \
/Users/hobu/miniforge3/envs/sm-distributed/bin/python tools/validate_batch_v4.py \
  --canonical-uri s3://sm-becker-056176271256-c0f24425-9fcc-4464-a7fe-820ebeb7e8be/db/batch-v4-albers-yx-row-c1b65682 \
  --ledger-uri s3://sm-becker-056176271256-c0f24425-9fcc-4464-a7fe-820ebeb7e8be/builds/c1b65682-0081-4e14-bbc9-f19274502e64/ledger \
  --build-id c1b65682-0081-4e14-bbc9-f19274502e64 \
  --gdal-python /Users/hobu/miniforge3/envs/gdal-tdb/bin/python \
  --gdal-driver-path /Users/hobu/miniforge3/envs/gdal-tdb/lib/gdalplugins \
  --output /private/tmp/becker-c1b65682-quality.json
```

## Billing checkpoint

At the 2026-09-25 UTC checkpoint, Cost Explorer returned **$0 estimated**
for this run's `RunId` on September 24 and 25. This is an unposted charge,
not a free run. The build admission ledger reserved **$42.10 of its $50 cap**;
that reservation is neither an AWS charge nor an estimate of the final bill.
Even the prior Becker RunId currently shows only about $0.50 of tagged
September 24 cost, so these recent daily numbers are not complete enough to
calibrate the next build.

S3 object inventory, which measures *stored bytes rather than billed usage*,
is attributable by this run's unique prefixes:

| Prefix | Objects | Bytes | GiB |
| --- | ---: | ---: | ---: |
| Canonical array `db/batch-v4-albers-yx-row-c1b65682/` | 7,836 | 4,994,295,021 | 4.651 |
| Build control and immutable stages `builds/c1b65682-.../` | 41,211 | 5,953,535,745 | 5.545 |
| Combined | 49,047 | 10,947,830,766 | 10.196 |

The permanent Becker bucket has `Project`, `Dataset`, and `BuildId` tags but
no per-run `RunId` tag; it is shared across builds. It has no S3 server-access
logging or per-prefix request-metrics configuration, and this account lists
no CloudTrail trail. Consequently Cost Explorer cannot retrospectively assign
this run's S3 GET/PUT and network charges exactly from `RunId`. Tagged EC2
compute and other tag-bearing services should be reconciled when posted;
S3 storage can be allocated by prefix bytes and residence time, while request
and transfer charges require new telemetry or an explicitly estimated share.
Do not label an account-level S3 total as this run's measured cost.

The next cost checkpoint should query `RunId` by service and usage type,
separating EC2 compute, EBS/EC2-Other, S3, and network, then compare those
posted lines with the admission reservation and the per-prefix S3 inventory.
Until those lines post, no billed 9B-point projection is justified.
