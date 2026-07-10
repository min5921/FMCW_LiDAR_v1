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
cmake --preset windows-msvc-release -DALAZAR_SDK_ROOT=C:\AlazarTech\ATS-SDK\26.2.0
```

## Jetson/Linux

Required for the Qt application:

- a C++17 compiler;
- CMake 3.24 or newer and Ninja;
- Qt 6 development packages;
- CUDA/cuFFT compatible with the installed JetPack release;
- AlazarTech Linux SDK and driver support verified for the exact board and Jetson PCIe connection.

```bash
cmake --preset jetson-release -DALAZAR_SDK_ROOT=/usr/local/AlazarTech
cmake --build --preset jetson-release
ctest --preset jetson-release
```

## External SDK Roots

The following CMake cache variables are used for dependency discovery:

- `ALAZAR_SDK_ROOT`: AlazarTech headers and `ATSApi.lib`/`libATSApi.so` platform library.
- `FFTW_ROOT`: FFTW headers and libraries when package discovery is insufficient.

CUDA is discovered through the installed CMake CUDA toolkit support. EDFA/MCU serial support uses Win32 COM or POSIX tty directly; FTDI or USB-UART devices still require their operating system driver, but vendor control executables are not runtime dependencies.

## Current Workspace Check

The ordinary PowerShell `PATH` on the current Windows machine resolves `qmake` to Qt 5.15.2 and does not expose `cl`. The installed MSVC 19.44 and Qt 6.11.0 MSVC x64 toolchain were found through their installation roots. After activating the Visual Studio x64 developer environment and selecting Qt 6 through `CMAKE_PREFIX_PATH`, the Windows Qt application built successfully, the core contract test passed, and the offscreen UI smoke test exited successfully.
