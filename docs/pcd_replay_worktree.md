# PCD Replay Worktree

## Scope

- Branch: `codex/pcd-replay-only`.
- Separate checkout: `FMCW_LiDAR_PCDReplay`, beside the original project.
- Baseline: `f5e63ef` plus the current R1-R10 review fixes, captured in `803da89`.
- Original main files and other worktrees remain unchanged. MCU IDE-local state
  was not copied into the new branch.
- Existing acquisition, processing, storage, scanner, EDFA and UDP functions remain.
- The application opens Live View / 3D Point Cloud without starting acquisition.

## Included

- ASCII/binary PCD with XYZ, XYZI, or XYZIV fields.
- FMCW `.pointcloud.bin` and converted Waymo multi-frame `.pointcloud.bin` replay.
- XYZ, XYZI and CSV/text input supported by the existing reader.
- Open, play/pause, step, rewind, loop and playback FPS controls.
- Original point coordinates, existing color modes, camera interaction and GPU rendering.
- A single PCD file is a single frame; multi-frame playback uses `.pointcloud.bin`.
- Existing display-only temporal fusion/interpolation controls remain unchanged.
  No new geometric registration or scan conversion algorithm is introduced here.

## Excluded

- Weights selection, model environment-variable loading, Objects toggle.
- CenterPoint inference service and detection-snapshot UI dispatch.
- Detection box rendering and stale-box holding.
- Separate CenterPoint/Waymo Nx5/Nx6 import shortcut. Use the already converted
  `.pointcloud.bin` data or standard PCD instead.
- `src/detection` remains historical source, but none of it is compiled into this
  worktree's application. Its model tests are not part of this build.
- `FMCW_WITH_CENTERPOINT=ON` is rejected explicitly. Continue model work in the
  separate CenterPoint checkout; integration requires a later explicit decision.

## Replay Data Flow

`MainWindow::openPointCloudReplayFile` -> `AsyncPointCloudReplay` worker ->
`PointCloudReplayReader` -> complete `PointCloudSnapshot` ->
`MainWindow::displayPointCloudSnapshot` -> `PointCloudWidget`.

Replay no longer traverses the acquisition controller, FFT backend or an inference
queue. Reading stays off the GUI thread. Opening another file cancels the old
generation. Acquisition start cancels file replay so live and saved frames do not mix.

## Build And Verification

- Windows output: `build/package/FMCW_LiDAR_PCDReplay/FMCW_LiDAR.exe`.
- Jetson uses `deploy/jetson/build.sh`, Qt >= 6.2, CMake >= 3.18, CUDA/cuFFT,
  and the configured Alazar SDK. CenterPoint, cuDNN and model weights are not required.
- Reader tests cover ASCII XYZ PCD, binary XYZI PCD, XYZIV text and multi-frame binary.
- `fmcw_point_cloud_ui_tests` exercises actual MainWindow replay, step, rewind,
  play/pause without connected hardware, and checks that model controls are absent
  even when a model path is inherited from the environment.
- Hardware timing and actual Jetson execution are outside this replay-only check.

Windows verification on 2026-09-07: all 19 CTest cases passed. Four replay-related
tests also passed five consecutive runs each. The packaged executable loaded the
existing merged Waymo file and rendered 185,289 points through GPU VBOs. The package
contains no model weights or cuDNN DLLs. Hardware acquisition was not started.

Earlier review documents describe their historical baseline, including a previous
CenterPoint-enabled package. They do not override the scope of this worktree.
