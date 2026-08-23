#!/usr/bin/env python3

from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
import zipfile

import numpy as np


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "convert_waymo_zip", ROOT / "tools" / "convert_waymo_zip.py"
)
CONVERTER = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(CONVERTER)


class WaymoConverterTest(unittest.TestCase):
    def test_merges_lidars_returns_and_frames(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            archive_path = root / "waymo.zip"
            output_path = root / "merged.pointcloud.bin"
            schema = {
                "format_version": 1,
                "byte_order": "little-endian",
                "lidar_bin": {
                    "dtype": "float32",
                    "columns": CONVERTER.WAYMO_COLUMNS,
                },
                "coordinates": "Waymo vehicle frame; x forward, y left, z up",
            }

            def points(values: list[list[float]]) -> bytes:
                return np.asarray(values, dtype="<f4").tobytes()

            with zipfile.ZipFile(archive_path, "w", zipfile.ZIP_DEFLATED) as archive:
                archive.writestr("schema.json", json.dumps(schema))
                archive.writestr(
                    "frame_000/lidar/TOP_return1.bin",
                    points([[1, 2, 3, 4, 5, -1], [6, 7, 8, 9, 10, 1]]),
                )
                archive.writestr(
                    "frame_000/lidar/FRONT_return2.bin",
                    points([[11, 12, 13, 14, 15, -1]]),
                )
                archive.writestr(
                    "frame_001/lidar/TOP_return1.bin",
                    points([[21, 22, 23, 24, 25, -1]]),
                )

            manifest = CONVERTER.convert_waymo_archive(
                archive_path, output_path, quiet=True
            )
            self.assertEqual(manifest["frame_count"], 2)
            self.assertEqual(manifest["total_points"], 4)
            self.assertEqual(manifest["frames"][0]["nlz_points_included"], 1)
            self.assertEqual(manifest["lidars_merged"], ["TOP", "FRONT"])

            with output_path.open("rb") as stream:
                self.assertEqual(
                    CONVERTER.FILE_HEADER.unpack(stream.read(CONVERTER.FILE_HEADER.size)),
                    (CONVERTER.FILE_MAGIC, CONVERTER.FORMAT_VERSION),
                )
                first_header = CONVERTER.FRAME_HEADER.unpack(
                    stream.read(CONVERTER.FRAME_HEADER.size)
                )
                self.assertEqual(first_header[-3:], (3, 1, 3))
                first_points = np.frombuffer(
                    stream.read(3 * CONVERTER.POINT_DTYPE.itemsize),
                    dtype=CONVERTER.POINT_DTYPE,
                )
                self.assertEqual(first_points[0]["x"], 1.0)
                self.assertEqual(first_points[2]["x"], 11.0)
                self.assertAlmostEqual(
                    float(first_points[0]["intensity"]), float(np.tanh(4.0)), places=6
                )
                self.assertEqual(first_points[0]["elongation"], 5.0)
                self.assertEqual(first_points[1]["valid"], 1)

                second_header = CONVERTER.FRAME_HEADER.unpack(
                    stream.read(CONVERTER.FRAME_HEADER.size)
                )
                self.assertEqual(second_header[-3:], (1, 1, 1))


if __name__ == "__main__":
    unittest.main()
