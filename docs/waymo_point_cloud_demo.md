# Waymo point-cloud object detection demo

The demo is integrated with the main point-cloud replay path. It supports both a single
model-ready frame and a converted multi-frame Waymo segment.

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

For multi-frame playback, convert the whole archive to WaymoPCD2. This preserves the validated
five-feature model contract in every replay frame:

```powershell
C:\Users\user\anaconda3\python.exe .\tools\convert_waymo_zip.py `
  ".\data\samples\segment-15832924468527961_1564_160_1584_160_with_camera_labels.zip"
```

## GUI demonstration

1. Launch `build\package\FMCW_LiDAR_CenterPoint\FMCW_LiDAR.exe`.
2. Apply a setup using CUDA cuFFT.
3. Open **Live View > 3D Point Cloud**.
4. Select **Open Cloud...** and choose `data\waymo_demo\frame_000\points.bin`.
5. Select the exported weights root and turn **Objects ON**.

For sequence playback, use the replay-file Open button and choose the generated
`*.merged.pointcloud.bin`, then use Play/Pause, Step, Stop, Loop, and FPS controls. Each displayed
frame is also submitted to the latest-frame CenterPoint queue; stale detections are never drawn
over a newer or looped replay frame.

For the current segment's frame 000, the verified smoke run loaded 185,289 points and returned
one Vehicle box. A single run took 32.3 ms; this is a functional result, not a p50/p95 benchmark.
