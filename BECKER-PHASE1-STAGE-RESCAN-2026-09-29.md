# Becker Phase 1 pre-stage density rescan — 2026-09-29

## Outcome

RunId `aac5de66-6a5a-4128-9140-b4d56b0a2a72` now has **893 unreleased,
recoverable leaves**. Its six previously published leaves and canonical array
were not changed. No Batch stage or publisher was released during this rescan;
the final EC2 check found no pending, running, or stopping instance tagged with
this RunId.

The frozen input is `MN_BeckerCo_1_2021` in `us-west-2`, with the pinned water
mask SHA-256 `0f527d33c3f61fbb6703f1e6ddd8750dc46abd4f964241d9e5f2d4752daa78e5`.
The [scanner](analysis/becker_phase1_stage_rescan.py) reads the durable planned
receipts, verifies that mask, skips fully water-covered macro cores, and
executes bounded `readers.ept` queries at 10 m hierarchy resolution. Each
query includes the macro's 20 m processing collar, reprojects to EPSG:5070,
and crops to that collared target window. This is a **coarse LOD risk proxy**,
not a full-resolution source count or a promised stage memory bound. The
[checkpoint](analysis/results/becker-phase1-stage-rescan-2026-09-29.json)
contains 1,385 unique window counts and no unresolved errors; its SHA-256 is
`14c1b0234950d3da862c6b94522999fc67c653666b2513198ee95a8d4177842e`.

## Calibration and plan changes

The completed dense probe supplied the only directly measured stage outcomes
at this resolution. Values below are collared 10 m EPT counts versus retained
full-resolution points in the canonical array:

| Processing macro | 10 m count | Retained points | Stage outcome |
| --- | ---: | ---: | --- |
| West A, 32×64 cells | 163,639 | 28,120,422 | Published |
| West B, 32×64 cells | 132,438 | 25,115,329 | Published |
| East, 64×64 cells | 163,420 | 35,448,106 | Published |
| Original west, 64×64 cells | 287,221 | 53,235,751 across its eventual children | OOM at 20 GiB |

The published figures include the water-mask and science-pipeline effects;
the hierarchy query is raw and low resolution. The observed safe/failed
separation motivates a conservative 170,000-count guardrail, but the small
calibration set cannot guarantee that every smaller count will fit 20 GiB.

| Pass | Active macro assessment | Durable pre-splits | Pending leaves afterward |
| --- | --- | ---: | ---: |
| Initial scan | 412 macros; 337 non-water macros over 160,000 | 233 large unreleased blocks (including four low-risk blocks selected by area) | 485 |
| Child scan | 494 macros; 312 macros over 160,000 | Exactly those 312 reviewed blocks | 797 |
| Grandchild scan | 806 macros; 126 macros over 160,000 | 95 blocks over 170,000 or containing a high-density direct sample and over 150,000 | 892 |
| Hotspot check | One 32×32-cell leaf still held a 1,046,420-point/100 m sample and counted 162,913 at 10 m | That one leaf, producing children of 81,844 and 90,763 | **893** |

The final plan has **902 active macros**, of which 10 are fully water-covered
and 892 have bounded hierarchy measurements. For the non-water macros, the
median count is 114,506, the 95th percentile is 157,764, and the maximum is
169,862. None exceeds the 170,000 guardrail or the special high-density-sample
rule. The control ledger contains 893 `PENDING_STAGE`, 643 `SPLIT` (including
the two prior OOM splits), and six `PUBLISHED` records. No
`PRESPLITTING` record remains. The six published leaves retain 105,726,144
points; this is still a partial build, not the approximately 18B-point phase.

Two scan passes encountered a native PDAL hierarchy-read failure that killed
their process pool; the checkpoint preserved successful queries. Both missing
tails completed at lower concurrency. A separate transient EPT read error was
retried successfully. No failed query was treated as zero or safe.

## Admission, limits, and next gate

The frozen request reserves 12 cents for a stage, 6 cents for a publisher,
and $5 for finalization. The build ledger currently records **$1.56 already
reserved**, not billed. A single stage and publisher for each of the 893
pending leaves would reserve another $160.74; including finalization projects
**$167.30**, leaving **$7.70** under the $175 per-run admission ceiling. Retries,
additional splits, and shared or untagged S3/network charges are not included
in that projection. Admission reservations are not AWS Cost Explorer charges
and the $175 budget alert is not an absolute billing cutoff.

Do not release all 893 leaves solely on this scan. First stage and publish a
small reviewed set near the 170,000 guardrail, recording peak stage RSS,
`get_data`/PDAL and metric times, retained points, and canonical count/NoData
validation. If it confirms a comfortable memory margin, recheck the live
reservation and posted cost before bulk release. If it OOMs, split the
affected density class or revisit stage memory/instance packing before
spending the remaining admission headroom.
