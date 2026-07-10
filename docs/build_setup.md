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

## Jetson/Linux

Required for the Qt application:

- a C++17 compiler;
- CMake 3.24 or newer and Ninja;
- Qt 6 development packages;
- CUDA/cuFFT compatible with the installed JetPack release;
- AlazarTech Linux SDK and driver support verified for the exact board and Jetson PCIe connection.

```bash
cmake --preset jetson-release
cmake --build --preset jetson-release
ctest --preset jetson-release
```

## External SDK Roots

The following CMake cache variables are reserved for dependency discovery as the drivers are implemented:

- `ALAZAR_SDK_ROOT`: AlazarTech headers and platform libraries.
- `FFTW_ROOT`: FFTW headers and libraries when package discovery is insufficient.

CUDA is discovered through the installed CMake CUDA toolkit support. EDFA serial/FTDI support must use the operating system driver or a separately installed vendor package; vendor executables are not runtime dependencies of the new application.

## Current Workspace Check

The ordinary PowerShell `PATH` on the current Windows machine resolves `qmake` to Qt 5.15.2 and does not expose `cl`. The installed MSVC 19.44 and Qt 6.11.0 MSVC x64 toolchain were found through their installation roots. After activating the Visual Studio x64 developer environment and selecting Qt 6 through `CMAKE_PREFIX_PATH`, the Windows Qt application built successfully, the core contract test passed, and the offscreen UI smoke test exited successfully.
