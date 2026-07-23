# Build Setup

The repository does not vendor Qt, CUDA, FFTW, AlazarTech SDK binaries, or device drivers. Install them per platform and provide their roots through CMake or environment-specific user presets.

Do not add personal absolute paths to committed CMake files.

## Windows

Required for the Qt application:

- Visual Studio 2022 Build Tools or Visual Studio 2022 with Desktop development with C++;
- a Windows SDK and the MSVC x64 compiler;
- CMake 3.24 or newer;
- Qt 6 built for MSVC 2022 x64.

Configure Qt through `CMAKE_PREFIX_PATH` or `Qt6_DIR`. Keep machine-specific values in `CMakeUserPresets.json`, which is not intended for source control.

Run the commands from an x64 Visual Studio Developer Command Prompt so `cl`, `nmake`, the Windows SDK, and linker paths are active. Add the selected Qt MSVC `bin` directory to `PATH` before launching the built application.

The Windows presets use `NMake Makefiles`. In the current Korean Windows environment, CMake 4.3 encoded the localized MSVC `/showIncludes` prefix incorrectly for Ninja, causing incremental Ninja builds to miss header changes. NMake dependency tracking was verified by changing a shared header and observing the dependent test target rebuild.

```powershell
cmake --preset windows-msvc-debug
cmake --build --preset windows-msvc-debug
ctest --preset windows-msvc-debug
```

AlazarTech hardware adapter를 활성화하려면 ATS-SDK root를 지정한다. CMake가 `AlazarApi.h`, `AlazarCmd.h`, `AlazarError.h`와 `ATSApi.lib`를 모두 찾으면 adapter가 enabled로 표시된다. SDK가 없으면 simulator와 나머지 앱은 계속 빌드된다.

```powershell
cmake --preset windows-msvc-release -DALAZAR_SDK_ROOT=C:\AlazarTech\ATS-SDK\25.1.0
```

## Jetson/Linux

Required for the Qt application:

- a C++17 compiler;
- CMake 3.18 or newer and Ninja;
- Qt 6.2 or newer development packages;
- CUDA/cuFFT compatible with the installed JetPack release;
- AlazarTech Linux SDK and driver support verified for the exact board and Jetson PCIe connection.

For transfer to another Jetson, the Windows workspace can create a source-only bundle that excludes
`.git`, legacy vendor material, outputs, and existing build products:

```powershell
.\deploy\jetson\export_source.ps1
```

This produces:

```text
build/package/FMCW_LiDAR_Jetson_Source
build/package/FMCW_LiDAR_Jetson_Source.zip
```

On the Jetson, configure `deploy/jetson/jetson.env` and run one command:

```bash
bash deploy/jetson/build.sh
```

The script validates ARM64, CMake, Ninja, Qt 6.2 or newer, required CUDA/cuFFT, and the optional Alazar
ARM64 SDK before configuring. Jetson is built with FFTW disabled and CUDA required. It builds
Release, runs the applicable CTest targets and the Qt smoke test, then creates
`build/package/FMCW_LiDAR_Jetson`. The source manifest uses LF line endings and can be checked
on Jetson with `sha256sum -c SOURCE_MANIFEST.sha256`.

The direct `build.sh` path is the canonical Jetson build and supports CMake
3.18. `CMakePresets.json` uses preset schema version 3, so the optional
`jetson-release` preset requires CMake 3.21 or newer:

```bash
cmake --preset jetson-release \
  -DFMCW_CUDA_ARCHITECTURES=87 \
  -DALAZAR_SDK_ROOT=/usr/local/AlazarTech
cmake --build --preset jetson-release
ctest --preset jetson-release
```

Replace `87` with the numeric CUDA architecture for the target Jetson. The
canonical `build.sh` path performs this detection automatically.

## External SDK Roots

The following CMake cache variables are used for dependency discovery:

- `ALAZAR_SDK_ROOT`: AlazarTech headers and `ATSApi.lib`/`libATSApi.so` platform library.
- `FFTW_ROOT`: FFTW headers and libraries when package discovery is insufficient.

CUDA is enabled only when CMake finds both the `nvcc` CUDA compiler and `CUDAToolkit`. The active implementation is compiled from `src/processing/cuda/cuda_fft_backend.cu`. EDFA/MCU serial support uses Win32 COM or POSIX tty directly; FTDI or USB-UART devices still require their operating system driver, but vendor control executables are not runtime dependencies.

Qt 6.2 compatibility does not use `qt_standard_project_setup()`, which was
introduced in Qt 6.3. The project enables CMake `AUTOMOC`, `AUTOUIC`, and
`AUTORCC` directly so the same source builds with Jetson Qt 6.2 and newer
Windows Qt versions.

CMake 3.18 is the project minimum because it provides the CUDA architecture
target property used by the cuFFT build. The Jetson build script converts
`FMCW_JETSON_CUDA_ARCHITECTURES=auto` to a numeric architecture, avoiding the
CMake 3.24-only `native` value on older Jetson toolchains.

## Phase 4 FFT Backends

The CPU backend requires the single-precision FFTW3 library (`fftw3f`). Point `FFTW_ROOT` at an installation prefix containing `include/fftw3.h` and the platform library.

```powershell
cmake --preset windows-msvc-debug -DFFTW_ROOT=C:\path\to\fftw-prefix
```

CUDA/cuFFT is enabled when `nvcc` and `CUDAToolkit` are found. On Windows the build copies the discovered FFTW and cuFFT runtime DLLs next to the application and Phase 4 test executable. If a backend is not compiled or no CUDA device is available, the backend reports an actionable runtime error instead of silently selecting another backend.

The Windows Qt target is linked with the `Windows GUI` subsystem, so launching `fmcw_lidar_windows.exe` does not create a separate command window. Local SDK paths belong in ignored `CMakeUserPresets.json`; this workspace uses the `windows-local-debug` preset for ATS-SDK 25.1.0 and FFTW.

## Current Workspace Check

The ordinary PowerShell `PATH` on the current Windows machine resolves `qmake` to Qt 5.15.2 and does not expose `cl`. The installed MSVC 19.44 and Qt 6.11.0 MSVC x64 toolchain were found through their installation roots. After activating the Visual Studio x64 developer environment and selecting Qt 6 through `CMAKE_PREFIX_PATH`, the Windows Qt application built successfully, the core contract test passed, and the offscreen UI smoke test exited successfully.
