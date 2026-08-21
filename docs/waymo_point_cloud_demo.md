# Waymo point-cloud object detection demo

This demo stays on `codex/jetson-centerpoint-v1`; it does not require merging `main`.

## Input contract

The validated Waymo CenterPoint input is little-endian float32:

```text
[x, y, z, tanh(intensity), elongation]
```

Coordinates are `+X forward`, `+Y left`, and `+Z up`. The GUI can open model-ready Nx5
`points.bin`, individual raw Waymo Nx6 `*_returnN.bin`, and ASCII/binary PCD. PCD intensity is
assumed to be preprocessed for the model; missing elongation becomes zero. Compressed PCD is not
supported.

## Convert the derived Waymo ZIP

The converter combines all five LiDARs and both returns by default:

```powershell
C:\Users\user\anaconda3\python.exe .\tools\export_waymo_frame.py `
  ".\data\segment-15832924468527961_1564_160_1584_160_with_camera_labels.zip" `
  ".\data\waymo_demo\frame_000\points.bin" `
  --frame frame_000 `
  --summary-json ".\data\waymo_demo\frame_000\export_summary.json"
```

The ZIP and generated demo files are intentionally ignored by Git.

## GUI demonstration

1. Launch `build\package\FMCW_LiDAR_CenterPoint\FMCW_LiDAR.exe`.
2. Apply a setup using CUDA cuFFT.
3. Open **Live View > 3D Point Cloud**.
4. Select **Open Cloud...** and choose `data\waymo_demo\frame_000\points.bin`.
5. Select the exported weights root and turn **Objects ON**.

For the current segment's frame 000, the verified smoke run loaded 185,289 points and returned
one Vehicle box. A single run took 32.3 ms; this is a functional result, not a p50/p95 benchmark.
