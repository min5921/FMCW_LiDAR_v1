#!/usr/bin/env python3
"""Convert an exported Waymo segment ZIP to the FMCWPCD1 replay format."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import re
import struct
import sys
import time
import zipfile

try:
    import numpy as np
except ImportError as exc:  # pragma: no cover - exercised by the command-line environment
    raise SystemExit("NumPy is required: install it with 'python -m pip install numpy'") from exc


FILE_MAGIC = b"FMCWPCD1"
FORMAT_VERSION = 1
FRAME_MAGIC = 0x31444350
FRAME_HEADER = struct.Struct("<IQQQIII")
FILE_HEADER = struct.Struct("<8sI")
POINT_DTYPE = np.dtype(
    [
        ("x", "<f4"),
        ("y", "<f4"),
        ("z", "<f4"),
        ("intensity", "<f4"),
        ("velocity", "<f4"),
        ("valid", "u1"),
    ],
    align=False,
)
WAYMO_COLUMNS = ["x", "y", "z", "intensity", "elongation", "nlz_flag"]
WAYMO_POINT_BYTES = 6 * 4
LIDAR_ORDER = {name: index for index, name in enumerate(
    ("TOP", "FRONT", "SIDE_LEFT", "SIDE_RIGHT", "REAR")
)}
LIDAR_PATH = re.compile(
    r"^frame_(?P<frame>\d+)/lidar/(?P<lidar>[A-Z_]+)_return(?P<return>[12])\.bin$"
)


def _validate_schema(archive: zipfile.ZipFile) -> dict:
    try:
        schema = json.loads(archive.read("schema.json"))
    except KeyError as exc:
        raise ValueError("Waymo archive does not contain schema.json") from exc
    except json.JSONDecodeError as exc:
        raise ValueError(f"Waymo schema.json is invalid: {exc}") from exc

    lidar = schema.get("lidar_bin", {})
    if schema.get("byte_order") != "little-endian":
        raise ValueError("Only little-endian Waymo exports are supported")
    if lidar.get("dtype") != "float32" or lidar.get("columns") != WAYMO_COLUMNS:
        raise ValueError(
            "Unsupported lidar BIN schema; expected float32 " + ",".join(WAYMO_COLUMNS)
        )
    coordinates = schema.get("coordinates", "")
    normalized_coordinates = coordinates.lower().replace(" ", "")
    if not all(axis in normalized_coordinates for axis in ("xforward", "yleft", "zup")):
        raise ValueError(
            "Waymo points must already use the vehicle frame: x forward, y left, z up"
        )
    return schema


def _inventory(archive: zipfile.ZipFile) -> dict[int, list[tuple[str, int, zipfile.ZipInfo]]]:
    frames: dict[int, list[tuple[str, int, zipfile.ZipInfo]]] = {}
    for info in archive.infolist():
        match = LIDAR_PATH.match(info.filename)
        if match is None:
            continue
        if info.file_size % WAYMO_POINT_BYTES != 0:
            raise ValueError(
                f"{info.filename} has {info.file_size} bytes; expected a multiple of "
                f"{WAYMO_POINT_BYTES}"
            )
        frame_index = int(match.group("frame"))
        frames.setdefault(frame_index, []).append(
            (match.group("lidar"), int(match.group("return")), info)
        )

    if not frames:
        raise ValueError("No frame_###/lidar/*_return[12].bin files were found")
    for entries in frames.values():
        entries.sort(key=lambda entry: (LIDAR_ORDER.get(entry[0], 100), entry[0], entry[1]))
    return frames


def _frame_points(
    archive: zipfile.ZipFile,
    entries: list[tuple[str, int, zipfile.ZipInfo]],
) -> tuple[np.ndarray, dict[str, int], int]:
    counts = {f"{lidar}_return{return_index}": info.file_size // WAYMO_POINT_BYTES
              for lidar, return_index, info in entries}
    point_count = sum(counts.values())
    if point_count > 0xFFFFFFFF:
        raise ValueError(f"Frame contains too many points for FMCWPCD1: {point_count}")

    merged = np.empty(point_count, dtype=POINT_DTYPE)
    merged["velocity"].fill(np.nan)
    offset = 0
    nlz_points = 0
    for lidar, return_index, info in entries:
        raw = archive.read(info)
        values = np.frombuffer(raw, dtype="<f4")
        if values.size % len(WAYMO_COLUMNS) != 0:
            raise ValueError(f"{info.filename} contains a partial point record")
        values = values.reshape((-1, len(WAYMO_COLUMNS)))
        end = offset + values.shape[0]
        target = merged[offset:end]
        target["x"] = values[:, 0]
        target["y"] = values[:, 1]
        target["z"] = values[:, 2]
        target["intensity"] = values[:, 3]
        target["valid"] = np.isfinite(values[:, :3]).all(axis=1).astype(np.uint8)
        nlz_points += int(np.count_nonzero(values[:, 5] > 0.0))
        offset = end

    if offset != point_count or POINT_DTYPE.itemsize != 21:
        raise RuntimeError("Internal merged point layout mismatch")
    return merged, counts, nlz_points


def verify_output(path: Path, expected_counts: list[int]) -> None:
    size = path.stat().st_size
    with path.open("rb") as stream:
        magic, version = FILE_HEADER.unpack(stream.read(FILE_HEADER.size))
        if magic != FILE_MAGIC or version != FORMAT_VERSION:
            raise ValueError("Converted file header verification failed")

        for expected_index, expected_count in enumerate(expected_counts):
            header = stream.read(FRAME_HEADER.size)
            if len(header) != FRAME_HEADER.size:
                raise ValueError(f"Converted file ended before frame {expected_index}")
            (frame_magic, last_frame_id, scan_frame_index, revision,
             width, height, point_count) = FRAME_HEADER.unpack(header)
            if frame_magic != FRAME_MAGIC:
                raise ValueError(f"Frame {expected_index} has an invalid marker")
            if last_frame_id != expected_index or scan_frame_index != expected_index:
                raise ValueError(f"Frame {expected_index} index metadata is inconsistent")
            if revision != 0 or width != point_count or height != 1:
                raise ValueError(f"Frame {expected_index} layout metadata is inconsistent")
            if point_count != expected_count:
                raise ValueError(
                    f"Frame {expected_index} expected {expected_count} points, got {point_count}"
                )
            payload_bytes = point_count * POINT_DTYPE.itemsize
            next_position = stream.tell() + payload_bytes
            if next_position > size:
                raise ValueError(f"Frame {expected_index} point payload is truncated")
            stream.seek(payload_bytes, os.SEEK_CUR)

        if stream.tell() != size:
            raise ValueError("Converted file has trailing or unindexed bytes")


def convert_waymo_archive(input_path: Path, output_path: Path, quiet: bool = False) -> dict:
    input_path = input_path.resolve()
    output_path = output_path.resolve()
    output_path.parent.mkdir(parents=True, exist_ok=True)
    partial_path = output_path.with_name(output_path.name + ".partial")
    manifest_path = output_path.with_suffix(".json")
    started = time.monotonic()

    if partial_path.exists():
        partial_path.unlink()

    frame_reports = []
    expected_counts = []
    try:
        with zipfile.ZipFile(input_path, "r") as archive:
            schema = _validate_schema(archive)
            frames = _inventory(archive)
            frame_indices = sorted(frames)
            if not quiet:
                print(
                    f"Converting {len(frame_indices)} Waymo frames from {input_path.name}",
                    flush=True,
                )

            with partial_path.open("wb") as output:
                output.write(FILE_HEADER.pack(FILE_MAGIC, FORMAT_VERSION))
                for output_index, source_index in enumerate(frame_indices):
                    points, counts, nlz_points = _frame_points(archive, frames[source_index])
                    point_count = int(points.size)
                    output.write(
                        FRAME_HEADER.pack(
                            FRAME_MAGIC,
                            output_index,
                            output_index,
                            0,
                            point_count,
                            1,
                            point_count,
                        )
                    )
                    output.write(points.tobytes(order="C"))
                    expected_counts.append(point_count)
                    frame_reports.append(
                        {
                            "output_frame": output_index,
                            "source_frame": source_index,
                            "points": point_count,
                            "valid_points": int(np.count_nonzero(points["valid"])),
                            "nlz_points_included": nlz_points,
                            "sources": counts,
                        }
                    )
                    if not quiet and (output_index == 0 or (output_index + 1) % 10 == 0
                                      or output_index + 1 == len(frame_indices)):
                        elapsed = max(time.monotonic() - started, 1.0e-9)
                        print(
                            f"[{output_index + 1:3d}/{len(frame_indices)}] "
                            f"{point_count:,} points | {elapsed:.1f} s",
                            flush=True,
                        )
                output.flush()
                os.fsync(output.fileno())

        verify_output(partial_path, expected_counts)
        os.replace(partial_path, output_path)
    except Exception:
        if partial_path.exists():
            partial_path.unlink()
        raise

    elapsed_seconds = time.monotonic() - started
    sensor_names = sorted(
        {name.rsplit("_return", 1)[0] for report in frame_reports for name in report["sources"]},
        key=lambda name: (LIDAR_ORDER.get(name, 100), name),
    )
    manifest = {
        "format": "FMCWPCD1",
        "format_version": FORMAT_VERSION,
        "source_archive": str(input_path),
        "output_file": str(output_path),
        "coordinate_frame": "Waymo vehicle frame: X forward, Y left, Z up, meters",
        "lidars_merged": sensor_names,
        "returns_merged": [1, 2],
        "intensity": "Waymo intensity",
        "velocity": "NaN (not present in the Waymo export)",
        "elongation": "not stored by FMCWPCD1",
        "nlz_policy": "included; count recorded per frame",
        "frame_count": len(frame_reports),
        "total_points": sum(expected_counts),
        "output_bytes": output_path.stat().st_size,
        "elapsed_seconds": elapsed_seconds,
        "input_schema": schema,
        "frames": frame_reports,
    }
    manifest_path.write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    if not quiet:
        mib = manifest["output_bytes"] / (1024.0 * 1024.0)
        print(
            f"Complete: {len(frame_reports)} frames, {manifest['total_points']:,} points, "
            f"{mib:.1f} MiB -> {output_path}",
            flush=True,
        )
        print(f"Manifest: {manifest_path}", flush=True)
    return manifest


def _default_output(input_path: Path) -> Path:
    name = input_path.name
    if name.lower().endswith(".zip"):
        name = name[:-4]
    return input_path.with_name(name + ".merged.pointcloud.bin")


def main() -> int:
    parser = argparse.ArgumentParser(
        description=(
            "Merge all Waymo LiDARs and both returns in every frame into one "
            "FMCWPCD1 replay file."
        )
    )
    parser.add_argument("input", type=Path, help="Waymo exported segment ZIP")
    parser.add_argument("output", type=Path, nargs="?", help="Output .pointcloud.bin")
    parser.add_argument("--quiet", action="store_true", help="Suppress progress output")
    args = parser.parse_args()

    if not args.input.is_file():
        parser.error(f"input archive does not exist: {args.input}")
    output = args.output or _default_output(args.input)
    try:
        convert_waymo_archive(args.input, output, args.quiet)
    except (OSError, ValueError, zipfile.BadZipFile) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
