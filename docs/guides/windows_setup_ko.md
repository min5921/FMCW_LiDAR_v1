# 기본 FMCW · Windows 개발 시작

[프로젝트 첫 화면](../../README.md) · [전체 문서](../README.md)

현재 기본 제품 `codex/fmcw-base`를 빌드하는 절차입니다.
명령은 프로젝트 루트 `FMCW_LiDAR`에서 실행합니다. 아래 SDK 경로는 이 PC에서 확인한 예시이므로 다른 PC에서는 설치 위치에 맞춥니다.

## 1. 필요한 도구

| 용도 | 필요한 구성 |
|---|---|
| 공통 개발 | Git, VS 2022의 C++ 데스크톱 빌드 도구·MSVC x64·Windows SDK, CMake, Qt 6 MSVC x64, FFTW 단정밀도 라이브러리 |
| `windows-base` | 위 구성 + CUDA Toolkit/cuFFT, Alazar ATS-SDK |
| `windows-base-cpu` | Simulator·RAW·FFTW 개발용. Alazar SDK와 CUDA는 요구하지 않음 |
| 실제 장비 연결 | 보드·USB-UART 등의 장치별 드라이버와 올바른 장비 설정 |

현재 검증 환경은 MSVC 19.44, CMake 4.3.2, Qt 6.11.0 MSVC 2022 x64, CUDA 13.1.80, ATS-SDK 25.1.0입니다.
이는 확인한 조합이며 다른 버전의 호환성을 모두 검증했다는 뜻은 아닙니다.
Qt는 MSVC 빌드와 맞는 패키지를 사용합니다. MCU 펌웨어를 바꿀 때만 별도로 STM32CubeIDE가 필요합니다.

## 2. 소스 준비

Git으로 다른 PC에 전달하려는 변경은 해당 제품 브랜치에 커밋·푸시되어 있어야 합니다.
새 작업 폴더가 없는 위치에서 다음과 같이 가져옵니다.

```powershell
git clone --branch codex/fmcw-base https://github.com/min5921/FMCW_LiDAR_v1.git FMCW_LiDAR
Set-Location FMCW_LiDAR
git branch --show-current
git status --short --branch
```

이미 폴더가 있다면 중복 clone하지 않고 그 폴더에서 브랜치와 작업 상태를 먼저 확인합니다.
세 작업 공간을 함께 준비하는 방법은 [작업 공간 안내](workspaces_ko.md)에 있습니다.

## 3. x64 개발 환경 열기

Windows 시작 메뉴에서 **x64 Native Tools Command Prompt for VS 2022**를 엽니다.
그 창에서 아래를 실행하면 x64 빌드 환경을 상속한 PowerShell을 사용할 수 있습니다.

```cmd
powershell -NoProfile
```

이후 명령은 **PowerShell** 문법입니다. 프로젝트 폴더로 이동하고 도구가 보이는지 확인합니다.

```powershell
Set-Location 'C:\Users\user\Desktop\Project\FMCW_LiDAR'
Get-Command cmake, ctest, cl, nmake
$env:VSCMD_ARG_TGT_ARCH
```

다른 PC에서는 프로젝트 위치를 바꾸세요. 아키텍처는 `x64`여야 합니다.
일반 터미널에서는 MSVC가 설치되어 있어도 `nmake`·`cl`을 찾지 못할 수 있습니다.

## 4. 라이브러리 경로 지정

```powershell
$env:QT_ROOT = 'C:\Qt\6.11.0\msvc2022_64'
$env:FFTW_ROOT = 'C:\DEV\vcpkg\installed\x64-windows'
$env:ALAZAR_SDK_ROOT = 'C:\AlazarTech\ATS-SDK\25.1.0'
$env:PATH = "$env:QT_ROOT\bin;$env:FFTW_ROOT\bin;$env:PATH"
```

CPU 개발만 한다면 `ALAZAR_SDK_ROOT`는 필요하지 않습니다.
이 경로는 명령행이나 Git에서 제외하는 `CMakeUserPresets.json`에 둡니다.
다른 컴퓨터에서 만든 `build/` 캐시를 그대로 복사해서 사용하지 않습니다.

## 5. Configure → Build → Test

실제 계측 기능을 포함한 기본 구성:

```powershell
cmake --preset windows-base "-DCMAKE_PREFIX_PATH=$env:QT_ROOT" "-DFFTW_ROOT=$env:FFTW_ROOT" "-DALAZAR_SDK_ROOT=$env:ALAZAR_SDK_ROOT"
cmake --build --preset windows-base
ctest --preset windows-base
```

GPU·Alazar 없이 개발할 구성:

```powershell
cmake --preset windows-base-cpu "-DCMAKE_PREFIX_PATH=$env:QT_ROOT" "-DFFTW_ROOT=$env:FFTW_ROOT"
cmake --build --preset windows-base-cpu
ctest --preset windows-base-cpu
```

각 명령이 성공한 것을 확인한 뒤 다음 명령을 실행합니다.

| 단계 | 하는 일 | 성공 확인 |
|---|---|---|
| Configure | 컴파일러·소스·라이브러리 경로를 확인하고 빌드 준비 | `Configuring done`, `Generating done` |
| Build | C++/CUDA 소스로 실행 파일 생성 | 빌드 오류 없이 완료 |
| Test | 등록된 자동 검증 실행 | CTest의 실패 수가 0 |

## 6. 실행본 만들기

아직 존재하지 않는 새 출력 폴더를 사용해 이전 배포본을 보존합니다.

```powershell
# Alazar + CUDA + FFTW 실행본
.\deploy\windows\package.ps1 -OutputDirectory 'build/package/Windows-Basic-new'

# CPU 개발용 실행본은 별도 이름으로 보관
.\deploy\windows\package.ps1 -BuildDirectory 'build/preset-windows-base-cpu/src' -OutputDirectory 'build/package/Windows-Basic-CPU-new'
```

위 두 명령 중 빌드한 구성에 맞는 하나를 사용합니다. 폴더 이름이 이미 있다면 새로운 이름을 정합니다.
패키징 스크립트는 필요한 DLL·Qt 플러그인·설정을 복사하고 시작 검사를 수행합니다.

확인한 계측용 패키지를 현재 실행본으로 바꿀 때는 기존 `Windows-Basic` 폴더 전체를 `build/package_archive/날짜/` 아래에 보관하고,
새 패키지 폴더를 `Windows-Basic`으로 이름을 바꿉니다. 그러면 루트 `RUN.cmd`가 그 실행본을 엽니다.
CPU 개발본은 별도 폴더에서 직접 실행해 계측용 실행본과 구분합니다.

<a id="vscode"></a>
## 7. VS Code가 헤더를 인식하게 하기

1. PC에 **MSVC x64 빌드 도구**와 Qt MSVC 패키지가 설치되어 있는지 확인합니다.
2. `FMCW_Basic.code-workspace`를 엽니다. CMake Tools가 preset의 x64 정보와 작업 영역 설정을 사용해 Visual Studio 개발 환경을 불러옵니다.
3. VS Code에 **C/C++**와 **CMake Tools** 확장을 설치합니다.
4. `Ctrl+Shift+P` → **CMake: Select Configure Preset**에서 사용할 구성을 선택합니다.
5. **CMake: Configure**를 실행합니다. 처음 구성하는 PC라면 5절의 명령으로 SDK 경로를 먼저 지정할 수 있습니다.

| VS Code에 보이는 이름 | 실제 preset |
|---|---|
| Basic Windows Release (Alazar + CUDA + FFTW) | `windows-base` |
| Basic CPU Development (Simulator / RAW) | `windows-base-cpu` |

프로젝트 전체 설정을 사용하려면 `src/`만 열지 말고 **`FMCW_Basic.code-workspace`를 작업 영역으로 엽니다**.
이 작업 영역에는 CMake의 경로 정보를 C/C++ 코드 분석에 연결하는 설정이 있습니다.
각 preset의 빌드 폴더에는 `compile_commands.json`도 생성됩니다. CMake 연동을 사용할 수 없을 때는
**C/C++: Edit Configurations (UI)**의 **Compile commands**에 해당 파일을 지정하면 Qt 헤더와 컴파일 옵션을 읽을 수 있습니다.

Configure가 성공했는데 이전 밑줄이 남으면 **C/C++: Reset IntelliSense Database**를 실행합니다.
Configure 자체가 실패했다면 먼저 아래 오류를 해결합니다.

| 증상 | 먼저 확인할 것 |
|---|---|
| `nmake` 또는 `cl`을 찾지 못함 | 작업 영역의 `cmake.useVsDeveloperEnvironment`가 `always`인지 확인. 계속 실패하면 기존 VS Code 창을 닫고 x64 개발용 창에서 다시 실행 |
| Qt6를 찾지 못함 | `CMAKE_PREFIX_PATH`가 실제 Qt MSVC 설치 위치인지 |
| FFTW를 찾지 못함 | `FFTW_ROOT`와 `fftw3.h`, 단정밀도 `fftw3f` 라이브러리 |
| CUDA compiler/cuFFT를 찾지 못함 | CUDA Toolkit 설치와 해당 MSVC·GPU에 맞는 구성. CPU 개발이면 CPU preset 사용 |
| ATS-SDK를 찾지 못함 | SDK 헤더·ATSApi 라이브러리 위치. 장비 없는 개발이면 CPU preset 사용 |
| `RUN.cmd`가 실행 파일을 찾지 못함 | `build/package/Windows-Basic/` 배포본을 만들었는지 |

[소스 설명서](../source/source_guide_ko.md) · [배포 파일 위치](package_layout.md) · [실장비 검증 절차](../hardware/hardware_acceptance.md)
