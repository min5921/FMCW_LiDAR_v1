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

## Projection Fixes (2026-09-07)

- `apps/common/point_cloud_camera.h` owns the shared right-handed Qt lookAt and
  perspective matrix used by GPU points, Painter fallback, and reference overlays.
  The initial camera is above the XY plane. Stored XYZIV and scan direction are
  unchanged; no point data is mirrored or converted by this change.
- Real homogeneous near/far clipping replaces clamping negative eye depth to 0.2.
  Points behind the eye are hidden. Grid/axis segments are clipped before division,
  including segments crossing the eye. Reference guides remain overlay graphics.
- Far clipping follows the current display radius and camera distance. It does
  not alter the view, origin, or initial fit scale between playback frames.
- Wheel dolly range is 0.01 to 200 (formerly 0.2 to 8), bounded in log space.
  Moving the camera through a surface correctly hides points now behind the eye.
  Reset restores the initial pose and fits the current frame about the LiDAR origin.
- Horizontal left-button dragging makes the front of the cloud follow the pointer.
  The yaw input sign is opposite to the old mirrored projection's sign. Vertical
  orbit, right/middle-button pan, and stored XYZIV coordinates are unchanged.
  Framebuffer regressions verify left/right orbit, downward orbit and rightward pan
  on both GPU and Painter at 100% and 200% scaling.
- A newly opened file or new acquisition session resets the camera and fit.
  Same-file rewind clears frame history but retains both camera pose and fit.
- GPU sprite size uses logical size times device pixel ratio, limited to the
  device's reported maximum point size. Painter points use logical pixels.
- Painter fallback clips identically and sorts visible points far-to-near so
  farther input points cannot overwrite closer opaque points. Antialiasing and
  translucent coverage can still differ from GPU sprites.
- The count label says `display points`, not `shown`: it counts submitted points,
  not on-screen pixels after clipping or occlusion.

Five added CTest cases cover camera mathematics and actual GPU/Painter framebuffers
at 100% and 200% display scaling. Cases include zoom-out, farther subsequent frames,
behind-eye points, new-file reset, rewind preservation, near/far occlusion, rotation,
and wide/tall window aspect ratios. Windows: all 24 CTest cases passed.
Screenshots are generated under `build/replay-release/tests/projection-*`.
The render test also accepts `--sample=<path>` for optional local-data screenshots;
normal CTest cases have no external dataset or hardware dependency.

Projection and orbit-direction fix packages are separate from the previously opened executable:

- Windows: `build/package/FMCW_LiDAR_PCDReplay_OrbitFix/FMCW_LiDAR.exe`.
- Jetson sources: `build/package/FMCW_LiDAR_PCDReplay_Jetson_Source_OrbitFix.zip`.
  Build with the included `deploy/jetson/build.sh`. These use the same shared
  camera code and Qt 6.2-compatible APIs; Jetson hardware execution is not verified
  by the Windows checks above.
