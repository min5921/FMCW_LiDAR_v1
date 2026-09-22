> 2026-09-22: 현재 패키지는 `build/package/Windows-PCDReplay/`이며, 아래 `Windows/` 경로는 보존된 이전 패키지입니다. [현재 작업 공간 안내](workspaces_ko.md)를 확인하세요.

# Package Layout

This is the PCDReplay worktree, without object detection or model weights.

```text
build/package/
  README.md
  Windows/
    FMCW_LiDAR.exe
    config/
    *.dll and Qt plugin folders
    BUILD_FEATURES.txt
    BUILD_SOURCES.sha256
  Jetson/
    FMCW_LiDAR_Jetson_Source/
    FMCW_LiDAR_Jetson_Source.zip
```

## Run On Windows

Open `Windows/FMCW_LiDAR.exe`. This is the confirmed OrbitFix application.
Keep the entire Windows folder together when copying it to another PC.

- `Qt6*.dll` and plugin folders: user interface, OpenGL, and image support.
- `cufft64_12.dll`: CUDA FFT runtime; this is the largest file, not replay data.
- `fftw3f.dll`: CPU FFT runtime.
- `ATSApi.dll`: Alazar API runtime; board drivers are installed separately.
- `msvcp*`, `vcruntime*`, and other Microsoft DLLs: compiler runtimes.
- `config/`: default settings, profiles, calibration, and MCU waveform.
- `BUILD_*`: source identity and build-feature records.

Do not remove or relocate individual DLLs to simplify the folder view.

## Build On Jetson

Transfer either the Jetson source folder or its ZIP, not both. Extract the ZIP
and run `bash deploy/jetson/build.sh` inside `FMCW_LiDAR_Jetson_Source`.
This is buildable source, not a prebuilt ARM64 application.

## Previous Versions And Data

Previous packages are preserved outside this folder in
`build/package_archive/2026-09-08/`, with a file-hash relocation inventory.
They are not the current executable. No previous package was deleted.

Replay samples remain in the original `FMCW_LiDAR/data/samples` directory,
not in this worktree's package. Select the `.merged.pointcloud.bin` file in the GUI.
Original-project packages and other worktrees were not reorganized.
