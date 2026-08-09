# Jetson CenterPoint integration

This branch keeps object detection separate from the `main` acquisition branch while sharing
committed `main` changes through normal Git merges.

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

Keep weights outside both Git worktrees so they are not duplicated or committed:

```text
C:\Users\user\Desktop\Project\FMCW_LiDAR_Models\centerpoint\waymo_novelocity\
  04_pfn\
  06_rpn\
  07_head\
```

Use the equivalent external path on Jetson and pass the root directory through runtime
configuration. The application must verify all three directories before enabling inference.

## Integration order

1. Validate the point conversion contract and fixed intensity mapping on recorded FMCW frames.
2. Build the CenterPoint backend as a Jetson-only optional target for CUDA architecture 87.
3. Run offline single-frame inference with the full grid and record p50/p95 timing.
4. Parameterize the CUDA grid and benchmark the narrow-FOV crop without changing voxel size.
5. Run inference asynchronously on complete `PointCloudSnapshot` frames.
6. Publish `DetectionSnapshot` results to the Qt point-cloud view.

The Qt view already accepts matching detection snapshots and renders oriented 3D boxes with
class, score, object count, and total inference time. It intentionally ignores detections from a
different scan-frame index so stale boxes are not drawn over a newer point cloud.
