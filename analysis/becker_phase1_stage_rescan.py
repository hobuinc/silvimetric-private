"""Bounded, resumable EPT hierarchy scan of unreleased Becker processing macros.

This is read-only against AWS. It uses a 10 m EPT LOD query over each macro's
20 m processing collar, not a full-resolution point read or Batch job.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import time
from collections import Counter, defaultdict
from concurrent.futures import ProcessPoolExecutor, as_completed
from pathlib import Path
from urllib.parse import urlparse

import boto3
from boto3.dynamodb.conditions import Key
from osgeo import gdal


ORIGIN_X = -2493045
ORIGIN_Y = 3310005
CELL_M = 20
REFERENCES = {
    # Three completed, single-macro recovery leaves and the OOM parent.
    "safe-west-a": ((126592, 32256, 126624, 32320), 28_120_422, "published"),
    "safe-west-b": ((126624, 32256, 126656, 32320), 25_115_329, "published"),
    "safe-east": ((126656, 32256, 126720, 32320), 35_448_106, "published"),
    "oom-west": ((126592, 32256, 126656, 32320), 53_235_751, "oom"),
}


def s3_parts(uri: str) -> tuple[str, str]:
    parsed = urlparse(uri)
    if parsed.scheme != "s3" or not parsed.netloc:
        raise ValueError(f"Expected S3 URI: {uri!r}")
    return parsed.netloc, parsed.path.lstrip("/")


def pixel_rect(bounds: list[float]) -> tuple[int, int, int, int]:
    x0, y0, x1, y1 = bounds
    cells = (
        (x0 - ORIGIN_X) / CELL_M,
        (ORIGIN_Y - y1) / CELL_M,
        (x1 - ORIGIN_X) / CELL_M,
        (ORIGIN_Y - y0) / CELL_M,
    )
    rect = tuple(round(value) for value in cells)
    if any(abs(a - b) > 1e-6 for a, b in zip(rect, cells)):
        raise ValueError(f"Macro is not on the pinned pixel grid: {bounds}")
    if rect[0] >= rect[2] or rect[1] >= rect[3]:
        raise ValueError(f"Invalid macro rectangle: {rect}")
    return rect


def rect_key(rect: tuple[int, int, int, int]) -> str:
    return ",".join(map(str, rect))


def load_plan(request: dict) -> tuple[list[dict], list[dict]]:
    table = boto3.resource("dynamodb", region_name="us-west-2").Table(
        request["control_table"]
    )
    response = table.query(KeyConditionExpression=Key("BuildId").eq(request["build_id"]))
    items = response["Items"]
    while "LastEvaluatedKey" in response:
        response = table.query(
            KeyConditionExpression=Key("BuildId").eq(request["build_id"]),
            ExclusiveStartKey=response["LastEvaluatedKey"],
        )
        items.extend(response["Items"])
    pending = [
        item for item in items
        if item.get("RecordType") == "BLOCK"
        and item.get("State") == "PENDING_STAGE"
        and "StageReleasedAt" not in item
    ]
    pending_by_id = {item["BlockId"]: item for item in pending}

    bucket, prefix = s3_parts(request["ledger_uri"])
    s3 = boto3.client("s3", region_name="us-west-2")
    planned_keys: dict[str, str] = {}
    for page in s3.get_paginator("list_objects_v2").paginate(
        Bucket=bucket, Prefix=prefix.rstrip("/") + "/blocks/"
    ):
        for obj in page.get("Contents", []):
            key = obj["Key"]
            if "-planned-" not in key or not key.endswith(".json"):
                continue
            block_id = key.split("/blocks/", 1)[1].split("/", 1)[0]
            if block_id in pending_by_id:
                if block_id in planned_keys:
                    raise RuntimeError(f"Duplicate planned receipt for {block_id}")
                planned_keys[block_id] = key
    if set(planned_keys) != set(pending_by_id):
        missing = sorted(set(pending_by_id) - set(planned_keys))
        raise RuntimeError(f"Missing planned receipts: {missing[:8]}")

    macros: dict[str, dict] = {}
    for block_id, key in sorted(planned_keys.items()):
        receipt = json.loads(s3.get_object(Bucket=bucket, Key=key)["Body"].read())
        details = receipt["details"]
        for bounds in details["macro_bounds"]:
            rect = pixel_rect(bounds)
            entry = macros.setdefault(
                rect_key(rect), {"pixel_bounds": rect, "block_ids": []}
            )
            entry["block_ids"].append(block_id)
    return pending, list(macros.values())


def target_bounds(rect: tuple[int, int, int, int]) -> tuple[float, float, float, float]:
    x0, y0, x1, y1 = rect
    return (
        ORIGIN_X + x0 * CELL_M,
        ORIGIN_Y - y1 * CELL_M,
        ORIGIN_X + x1 * CELL_M,
        ORIGIN_Y - y0 * CELL_M,
    )


def pdal_bounds(bounds: tuple[float, float, float, float]) -> str:
    return f"([{bounds[0]},{bounds[2]}],[{bounds[1]},{bounds[3]}])"


def count_macro(task: tuple[str, str, tuple[int, int, int, int], float, int]) -> tuple[str, int, float]:
    import pdal
    from pyproj import Transformer

    key, ept, rect, lod_m, collar_cells = task
    x0, y0, x1, y1 = rect
    collared = (x0 - collar_cells, y0 - collar_cells,
                x1 + collar_cells, y1 + collar_cells)
    bounds = target_bounds(collared)
    source_bounds = Transformer.from_crs(
        5070, 3857, always_xy=True
    ).transform_bounds(*bounds, densify_pts=21)
    reader = {
        "type": "readers.ept", "filename": ept,
        "bounds": pdal_bounds(source_bounds), "resolution": lod_m,
        "requests": 2, "ignore_unreadable": True,
    }
    pipeline = pdal.Pipeline(json.dumps({"pipeline": [
        reader,
        {"type": "filters.reprojection", "out_srs": "EPSG:5070"},
        {"type": "filters.crop", "bounds": pdal_bounds(bounds)},
    ]}))
    started = time.monotonic()
    result = int(pipeline.execute())
    return key, result, time.monotonic() - started


def save_checkpoint(path: Path, report: dict) -> None:
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    os.replace(temporary, path)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--request-uri", required=True)
    parser.add_argument("--mask", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--workers", type=int, default=4)
    parser.add_argument("--max-queries", type=int, default=0)
    parser.add_argument("--lod-m", type=float, default=10.0)
    parser.add_argument("--collar-cells", type=int, default=1)
    args = parser.parse_args()
    if args.workers < 1 or args.lod_m <= 0 or args.collar_cells < 0:
        raise ValueError("Invalid worker, LOD, or collar setting")

    request_bucket, request_key = s3_parts(args.request_uri)
    request = json.loads(boto3.client("s3", region_name="us-west-2").get_object(
        Bucket=request_bucket, Key=request_key
    )["Body"].read())
    if request["build_id"] != request["run_id"] or not request["spatial_profile"]["usgs_albers"]:
        raise ValueError("This scanner requires one pinned USGS Albers build")
    mask_hash = hashlib.sha256(args.mask.read_bytes()).hexdigest()
    if mask_hash != request["water_mask"]["sha256"]:
        raise ValueError("Local water mask does not match the frozen request")
    pending, macros = load_plan(request)

    gdal.UseExceptions()
    mask = gdal.Open(str(args.mask))
    gt = mask.GetGeoTransform()
    mask_x = round((gt[0] - ORIGIN_X) / CELL_M)
    mask_y = round((ORIGIN_Y - gt[3]) / CELL_M)
    import numpy as np
    for macro in macros:
        x0, y0, x1, y1 = macro["pixel_bounds"]
        values = mask.ReadAsArray(x0 - mask_x, y0 - mask_y, x1 - x0, y1 - y0)
        if values is None or values.shape != (y1 - y0, x1 - x0):
            raise ValueError(f"Water mask does not cover {macro['pixel_bounds']}")
        if np.any((values != 0) & (values != 1)):
            raise ValueError("Invalid water-mask values in a planned macro")
        macro["water_fraction"] = round(float(np.mean(values == 1)), 6)

    signature = {
        "request_uri": args.request_uri,
        "build_id": request["build_id"],
        "source_ept": request["source_ept"],
        "mask_sha256": mask_hash,
        "lod_m": args.lod_m,
        "collar_cells": args.collar_cells,
        "pending_block_ids_sha256": hashlib.sha256(
            "\n".join(sorted(x["BlockId"] for x in pending)).encode()
        ).hexdigest(),
    }
    if args.output.exists():
        report = json.loads(args.output.read_text())
        stable_keys = set(signature) - {"pending_block_ids_sha256"}
        if any(report["signature"][key] != signature[key] for key in stable_keys):
            raise ValueError("Checkpoint source or scan settings differ")
        # A durable pre-split changes only the active leaf IDs/descriptors.
        # Keep hierarchy counts for unchanged macro rectangles across passes.
        if report["signature"] != signature:
            report["signature"] = signature
            report["pending_blocks"] = len(pending)
            report["macros"] = macros
            save_checkpoint(args.output, report)
    else:
        report = {"signature": signature, "pending_blocks": len(pending),
                  "macros": macros, "reference_macros": REFERENCES,
                  "counts": {}, "errors": {}}
        save_checkpoint(args.output, report)

    tasks = []
    for macro in macros:
        if macro["water_fraction"] == 1.0:
            continue  # get_data() does not read EPT for a fully-water core.
        key = rect_key(tuple(macro["pixel_bounds"]))
        if key not in report["counts"]:
            tasks.append((key, request["source_ept"],
                          tuple(macro["pixel_bounds"]), args.lod_m,
                          args.collar_cells))
    for reference in REFERENCES.values():
        rect = reference[0]
        key = rect_key(rect)
        if key not in report["counts"]:
            tasks.append((key, request["source_ept"], rect, args.lod_m,
                          args.collar_cells))
    tasks = list(dict((task[0], task) for task in tasks).values())
    if args.max_queries:
        tasks = tasks[:args.max_queries]
    print(json.dumps({"pending_blocks": len(pending), "pending_macros": len(macros),
                      "queued_queries": len(tasks), "already_counted": len(report["counts"]),
                      "fully_water_macros": sum(m["water_fraction"] == 1.0 for m in macros)}),
          flush=True)
    with ProcessPoolExecutor(max_workers=args.workers) as executor:
        futures = {executor.submit(count_macro, task): task for task in tasks}
        for done, future in enumerate(as_completed(futures), 1):
            task = futures[future]
            try:
                key, count, seconds = future.result()
                report["counts"][key] = {"coarse_points": count,
                                         "seconds": round(seconds, 3)}
                report["errors"].pop(key, None)
            except Exception as error:
                report["errors"][task[0]] = repr(error)
            save_checkpoint(args.output, report)
            if done % 10 == 0 or done == len(tasks):
                print(json.dumps({"completed_this_pass": done,
                                  "queued_this_pass": len(tasks),
                                  "total_counted": len(report["counts"]),
                                  "errors": len(report["errors"])}), flush=True)
    print(json.dumps({"report": str(args.output),
                      "counted": len(report["counts"]),
                      "errors": report["errors"]}), flush=True)


if __name__ == "__main__":
    main()
