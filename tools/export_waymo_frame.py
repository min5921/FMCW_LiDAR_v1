#!/usr/bin/env python3
"""Convert one derived Waymo ZIP frame to CenterPoint Nx5 points.bin."""

from __future__ import annotations

import argparse
import json
import zipfile
from pathlib import Path

import numpy as np


LIDARS = ("TOP", "FRONT", "SIDE_LEFT", "SIDE_RIGHT", "REAR")
RETURNS = ("return1", "return2")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("archive", type=Path)
    parser.add_argument("output_bin", type=Path)
    parser.add_argument("--frame", default="frame_000")
    parser.add_argument("--lidars", nargs="+", choices=LIDARS, default=list(LIDARS))
    parser.add_argument("--returns", nargs="+", choices=RETURNS, default=list(RETURNS))
    parser.add_argument("--drop-nlz", action="store_true")
    parser.add_argument("--intensity-transform", choices=("tanh", "none"), default="tanh")
    parser.add_argument("--summary-json", type=Path)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    arrays: list[np.ndarray] = []
    sources: list[dict[str, object]] = []
    expected_columns = ["x", "y", "z", "intensity", "elongation", "nlz_flag"]

    with zipfile.ZipFile(args.archive) as archive:
        schema = json.loads(archive.read("schema.json").decode("utf-8"))
        if schema.get("lidar_bin", {}).get("columns") != expected_columns:
            raise ValueError("unexpected Waymo derived archive lidar schema")
        for lidar in args.lidars:
            for lidar_return in args.returns:
                entry = f"{args.frame}/lidar/{lidar}_{lidar_return}.bin"
                values = np.frombuffer(archive.read(entry), dtype="<f4")
                if values.size % 6:
                    raise ValueError(f"{entry} is not Nx6 float32")
                points6 = values.reshape(-1, 6)
                before = int(points6.shape[0])
                if args.drop_nlz:
                    points6 = points6[points6[:, 5] < 0.0]
                arrays.append(points6[:, :5].astype("<f4", copy=True))
                sources.append({
                    "entry": entry,
                    "points_before_filter": before,
                    "points_after_filter": int(points6.shape[0]),
                })

    points = np.concatenate(arrays, axis=0)
    if args.intensity_transform == "tanh":
        points[:, 3] = np.tanh(points[:, 3])
    args.output_bin.parent.mkdir(parents=True, exist_ok=True)
    points.astype("<f4", copy=False).tofile(args.output_bin)

    summary = {
        "archive": str(args.archive),
        "frame": args.frame,
        "output_bin": str(args.output_bin),
        "feature_columns": ["x", "y", "z", "intensity", "elongation"],
        "intensity_transform": args.intensity_transform,
        "drop_nlz": args.drop_nlz,
        "lidars": args.lidars,
        "returns": args.returns,
        "num_points": int(points.shape[0]),
        "sources": sources,
        "min": points.min(axis=0).tolist(),
        "max": points.max(axis=0).tolist(),
    }
    if args.summary_json:
        args.summary_json.parent.mkdir(parents=True, exist_ok=True)
        args.summary_json.write_text(json.dumps(summary, indent=2), encoding="utf-8")
    print(json.dumps(summary, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
