# Becker Phase 1 launch gate — 2026-09-29

## Gate result

**Passed, with bulk dispatch held.** RunId
`aac5de66-6a5a-4128-9140-b4d56b0a2a72` has a successful zero-work ARM64
canary, a complete durable plan, and one staged-and-published work unit. The
other 255 work units remain `PENDING_STAGE`; both Batch queues are empty and
both compute environments have desired capacity zero. No EC2 instance was
pending, running, or stopping at the final check. Do not mistake this gate for
completion of the estimated 18-billion-point phase.

| Item | Pinned value |
| --- | --- |
| Stack | `silvimetric-batch-v4-aac5de66` in `us-west-2` |
| Request | `s3://sm-becker-056176271256-c0f24425-9fcc-4464-a7fe-820ebeb7e8be/builds/aac5de66-6a5a-4128-9140-b4d56b0a2a72/batch-v4-request.json` |
| Canonical array | `s3://sm-becker-056176271256-c0f24425-9fcc-4464-a7fe-820ebeb7e8be/builds/collection-canonical/array` |
| USGS Albers Phase 1 pixels | `[125639,31979,126988,33104)` |
| Calibrated source points | 17,987,450,723 estimate; 21,743,277,082 nominal 95% upper |
| Image | `056176271256.dkr.ecr.us-west-2.amazonaws.com/silvimetric-batch-v4-canary-942b4a4f@sha256:9145efa0300fd49790a0fa30bd891754fb846eedc6b0fa44d2b4e6bf601f055b` |
| Science recipe | PDAL pipeline SHA-256 `8738c30a1a044f769724418dffd4334349ff93d123c0eb46c33802923c23f5d9`; water mask SHA-256 `0f527d33c3f61fbb6703f1e6ddd8750dc46abd4f964241d9e5f2d4752daa78e5` |
| Guardrails | $175 per-run admission ceiling and tagged AWS Budget alerts; 480 Spot stage / 16 On-Demand publisher vCPU maxima; deferred array consolidation |

The canary Batch job `04f3002a-a98f-4661-bdce-140ca000568a` passed with
Python 3.14.7, PDAL-Python 3.5.5, ARM64, the compound USGS Albers PROJ
reprojection, the private water-mask hash, and GDAL TileDB pixel-is-area
georeferencing. The plan Batch job
`c5705888-9cf0-4253-8695-9dc2c971ea63` succeeded. It sampled 474 candidate
windows, made 479 coarse hierarchy queries and 478 native sample queries,
and created 256 work units from 418 processing macros. Its total time was
710.4 seconds, of which 603.4 seconds was planner sampling. Its durable
`plan.json` records `queued.stage = 1` and `queued.publish = 0`.

The one Spot stage, Batch job `8800ff7f-98be-4170-9701-b81824298b93`,
finished in 177.7 seconds. Its receipt for block
`19735-2647925-20875-2648245` reports 9,189,439 retained points in 723
populated cells; internal work took 166.7 seconds, including 121.3 seconds
of EPT read, 37.5 seconds of metric calculation, and 5.3 seconds of stage
write. Its staged logical TileDB data size was 111,006,390 bytes. Publisher
job `e10aacb8-0ebb-4f45-8ca2-99c334e6c577` succeeded in 16.3 seconds;
its receipt reports 13.2 seconds of publish work and about 806 MB peak RSS.

An independent GDAL read of the canonical S3 TileDB array over the block's
57×16 pixel window returned exactly **9,189,439** count points and **723**
occupied cells. The `m_Z_mean` raster had all 723 occupied cells readable
and NoData in all 189 zero-count cells. Its geotransform was
`(-2493045,20,0,3310005,0,-20)` and `AREA_OR_POINT=Area`.

## Cost and next release

The $175 is an admission limit and tagged budget threshold, **not a posted
bill or an absolute AWS account spending limit**. As of the preflight, the
September account budget showed $490.845 calculated spend against $1,250.
Wait for tagged compute, EBS, S3, and network usage before claiming actual
per-point cost. The first block is small and partly water-masked, so its
throughput should not be extrapolated alone to all 256 blocks.

The first deployment accidentally uploaded the 53,716-byte CloudFormation
template beneath `builds/<RunId>/`, which tripped the intentional empty-build
preflight. Its exact object was copied to
`infrastructure/batch-v4/<RunId>/` and removed from the build prefix before
creating the request. Future stack deployments must use an infrastructure
S3 prefix outside `builds/<RunId>/`.

The next deliberate release should sample at least one dense inland and one
water-heavy block, compare their stage/publish timing and counts with the
ledger, and review admission coefficients before dispatching the remaining
255 units. The current run and canonical array are resumable; no new RunId or
array should be created for that continuation.
