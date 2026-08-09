# Jetson CenterPoint integration

This branch keeps object detection separate from the `main` acquisition branch while sharing
committed `main` changes through normal Git merges.

## Target device

- NVIDIA Jetson AGX Orin
- Ubuntu 22.04
- CUDA architecture `87` (compute capability 8.7)
- JetPack-provided CUDA, cuFFT, and cuDNN

Record the exact JetPack/L4T, CUDA, and cuDNN versions on the device before the first build:

```bash
head -n 1 /etc/nv_tegra_release
nvcc --version
dpkg-query -W 'libcudnn*' 2>/dev/null
```

## Current input contract

- Coordinate system: `+X` forward, `+Y` left, `+Z` up, in meters.
- Feature order: `[x, y, z, intensity, fifth]` as five `float32` values.
- The current no-velocity weights use `fifth = 0`.
- A future velocity model can select `fifth = velocity_mps` without changing the snapshot API.
- Invalid points and points outside the configured range are removed before inference.
- Intensity uses fixed dB bounds, never per-frame min/max normalization.

The initial configuration deliberately retains the pretrained model grid:

```text
voxel size: [0.32, 0.32, 6.0] m
range:      [-74.88, -74.88, -2.0, 74.88, 74.88, 4.0] m
grid:       468 x 468
```

The latest model CUDA code still contains hard-coded `468 x 468` dimensions in the RPN,
CenterHead, and postprocess stages. Do not enable a cropped grid until those dimensions are
parameterized together. Keeping the same `0.32 m` voxel size allows the convolution weights to
be reused after that refactor.

## External model files

The selected training checkpoint is stored in WSL:

```text
/home/kopti/CenterPoint_Waymo/weights/
  centerpoint_waymo_pointpillars_full_novelocity_epoch12.pth
```

Its verified SHA-256 is:

```text
0dc61c02ef0385b9a7f768762f9154beaaa4a2b16dcef3340c2b9701d30de25b
```

The C++ runtime does not read the PyTorch checkpoint directly. The PFN, RPN, and CenterHead
exporters produce the runtime root below while leaving the original checkpoint unchanged:

```text
/home/kopti/CenterPoint_Waymo/weights/exported/pointpillars_full_novelocity_epoch12/
  04_pfn/
  06_rpn/
  07_head/
```

Windows can inspect the same root through:

```text
\\wsl.localhost\Ubuntu-22.04\home\kopti\CenterPoint_Waymo\weights\exported\
  pointpillars_full_novelocity_epoch12\
```

Keep the runtime weights outside both Git worktrees so they are not duplicated or committed.
Copy this exported root to an equivalent external path on Jetson and pass that root directory
through runtime configuration. The application must verify all three directories before enabling
inference.

## Enabling the Jetson backend

The build consumes the model implementation from a separate, pinned `LiDAR_recon` checkout so
the research model remains independently versioned. Copy or clone the repository on the Jetson,
then edit `deploy/jetson/jetson.env`:

```text
FMCW_JETSON_CUDA_ARCHITECTURES=87
FMCW_JETSON_WITH_CENTERPOINT=ON
FMCW_JETSON_CENTERPOINT_SOURCE_DIR=/home/kopti/LiDAR_recon
FMCW_JETSON_CENTERPOINT_WEIGHTS_ROOT=/home/kopti/CenterPoint_Waymo/weights/exported/pointpillars_full_novelocity_epoch12
```

`check_dependencies.sh` accepts either the `LiDAR_recon` root or its
`20_active_count_nms_project` directory. It verifies all CUDA sources, cuDNN, and the three weight
manifests before CMake starts. The normal build remains independent of CenterPoint while the
option is `OFF`.

The packaged `run.sh` loads `jetson.env` and exports the configured weights path to the Qt
application. In the **3D Point Cloud** toolbar, use **Objects OFF/ON** to control inference.
The button remains disabled until an applied processing configuration is using CUDA cuFFT.

## Windows CUDA build

Windows uses the same asynchronous detector service and Qt controls as Jetson. The development
machine must provide CUDA, cuFFT, `cudnn.h`, and the matching cuDNN runtime DLLs. If the Python
cuDNN package has no `cudnn.lib`, CMake generates an import library from the model project's
`cmake/cudnn64_9.def`. Build from an x64 Visual Studio Developer PowerShell:

```powershell
.\deploy\windows\build_centerpoint.ps1 `
  -CenterPointSource C:\path\to\LiDAR_recon `
  -WeightsRoot "\\wsl.localhost\Ubuntu-22.04\home\kopti\CenterPoint_Waymo\weights\exported\pointpillars_full_novelocity_epoch12" `
  -CudnnRoot C:\path\to\cudnn
```

### Verified Windows reference

The completed model project on the current development machine is the reference implementation:

```text
source:  C:\Users\user\Desktop\Onechip\Codex\my project
commit:  89793fe43d1ad130a8e4fd463e09c5c5090d717d
runtime: C:\Users\user\Desktop\Onechip\Codex\my project\20_active_count_nms_project
weights: C:\Users\user\Documents\객체인지\weights_full_novelocity
cuDNN:   C:\Users\user\Documents\객체인지\dependencies\nvidia_cudnn_cu13\nvidia\cudnn
sample:  C:\Users\user\Documents\객체인지\waymo_eval_tanh_pcdet_5frames\frame_000\points.bin
```

The exact reference build and inference smoke test is:

```powershell
.\deploy\windows\build_centerpoint.ps1 `
  -CenterPointSource "C:\Users\user\Desktop\Onechip\Codex\my project" `
  -WeightsRoot "C:\Users\user\Documents\객체인지\weights_full_novelocity" `
  -CudnnRoot "C:\Users\user\Documents\객체인지\dependencies\nvidia_cudnn_cu13\nvidia\cudnn" `
  -SamplePoints "C:\Users\user\Documents\객체인지\waymo_eval_tanh_pcdet_5frames\frame_000\points.bin"
```

This configuration built the complete Windows Qt application and passed all 8 configured tests.
The adapter inference smoke test consumed 183,680 points and returned 5 boxes in 24.82 ms on one
run. Treat that timing as a functional smoke-test result, not a p50/p95 benchmark.

The build script then creates the runnable package at:

```text
build\package\FMCW_LiDAR_CenterPoint\FMCW_LiDAR.exe
```

Launch this packaged executable. The executable under
`build\preset-windows-centerpoint-release\src` is a development artifact and does not contain
the Qt runtime DLLs or `platforms/qwindows.dll` needed for direct Explorer launch.

The weights stay outside the Git worktrees. The toolbar can select a different exported root at
runtime, or it can use `FMCW_CENTERPOINT_WEIGHTS_ROOT`. Windows packaging copies discovered
cuDNN runtime DLLs beside the executable.

## Runtime behavior

- Object detection is rejected unless the applied FFT backend is CUDA cuFFT.
- Inference runs on a separate capacity-one latest-frame queue, so it never blocks acquisition
  or adds backlog when the GPU is slower than the scan rate.
- Switching the processing configuration stops detection and clears the box overlay.
- Disabling the toolbar button stops the detector and removes boxes immediately.
- Windows and Jetson share this policy and runtime implementation.

## Integration order

1. Validate the point conversion contract and fixed intensity mapping on recorded FMCW frames.
2. Run the same full-grid offline inference on AGX Orin and record Windows/Jetson p50/p95.
3. Validate the asynchronous Qt toggle and matching box overlay on recorded FMCW frames.
4. Parameterize the CUDA grid and benchmark the narrow-FOV crop without changing voxel size.

The Qt view already accepts matching detection snapshots and renders oriented 3D boxes with
class, score, object count, and total inference time. It intentionally ignores detections from a
different scan-frame index so stale boxes are not drawn over a newer point cloud.
