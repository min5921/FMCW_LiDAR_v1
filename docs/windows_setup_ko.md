> 이 문서는 2026-09-17 GitHub 버전을 재현하는 과거 매뉴얼입니다. 2026-09-22 분리된 세 프로젝트의 현재 절차는 [작업 공간 안내](workspaces_ko.md)를 사용하세요.

# 다른 Windows PC에서 FMCW LiDAR 개발 시작하기

작성일: 2026-09-17

대상: Windows x64 PC에서 소스를 수정하고, 빌드하고, 실행하는 개발 환경

## 1. 이 매뉴얼의 Git 기준

| 항목 | 확인한 값 |
|---|---|
| 저장소 | [min5921/FMCW_LiDAR_v1](https://github.com/min5921/FMCW_LiDAR_v1) |
| GitHub 브랜치 | `main` |
| GitHub 커밋 | `5a820daeb3f55f5d3e622f84995cb88c11d52411` |
| 커밋 제목 | `docs: add Jetson maximum clock procedure` |
| 커밋 날짜 | 2026-08-21, 한국 시간 |

작성 시 `git ls-remote --heads origin`으로 GitHub의 실제 브랜치를 확인했다.
현재 PC의 로컬 `main`은 `f5e63ef`이며, GitHub보다 **14개 커밋이 앞서 있고 미커밋 변경도 있다.**
따라서 새 PC에서 지금 `git clone`하면 현재 PC 작업 폴더와 동일한 소스가 내려오지는 않는다.
이 문서는 요청한 **GitHub에 올라간 버전**으로 진행하는 절차다.
로컬의 v2 변경, CenterPoint/Waymo 추가 기능은 이 기준 커밋의 설치 대상에 포함되지 않는다.

이후 GitHub가 갱신되면 `git log -1 --oneline`으로 버전을 다시 확인한다.
이 매뉴얼 자체도 커밋·푸시하기 전까지는 현재 PC에만 존재한다.

## 2. 설치 범위 선택

**처음에는 A 구성으로 빌드와 시뮬레이터를 확인한 뒤, 필요한 구성만 추가한다.**

| 구성 | 할 수 있는 일 | 설치할 항목 |
|---|---|---|
| A. 기본 개발 | UI 수정, CPU FFT, 시뮬레이터, RAW replay | Git, VS 2022 C++ 도구, CMake, Qt 6 MSVC x64, FFTW |
| B. GPU 개발 | CUDA/cuFFT 처리 | A + 지원 NVIDIA GPU, 드라이버, CUDA Toolkit |
| C. 실제 계측 | Alazar 보드로 데이터 수집 | A 또는 B + 해당 보드 드라이버, ATS-SDK, ATS API runtime |
| D. MCU/EDFA 연동 | COM 포트로 스캐너·EDFA 제어 | C + 해당 USB-UART 드라이버; 펌웨어 수정 시 STM32CubeIDE |

Windows x64용 절차이며 Windows ARM용 빌드는 검증하지 않았다.
기본 PC 앱에는 Python, ROS, TensorRT, cuDNN, STM32CubeIDE가 필요하지 않다.
기존 `legacy/` 안의 Visual Studio 솔루션은 참고용이며, 현재 앱은 저장소 루트의 CMake로 빌드한다.

## 3. 새 PC에 개발 도구 설치

### 3.1 Git, Visual Studio 2022, CMake

1. [Git for Windows](https://git-scm.com/install/windows)의 x64 설치 프로그램을 설치한다.
2. Visual Studio 2022 또는 Build Tools 2022에서 **Desktop development with C++ / C++를 사용한 데스크톱 개발**을 선택한다.
3. MSVC v143 x64/x86 도구와 Windows SDK를 포함한다. Visual Studio 편집기가 필요 없으면 Build Tools만으로도 빌드할 수 있다. [Microsoft 설치 안내](https://learn.microsoft.com/en-us/cpp/build/vscpp-step-0-installation?view=msvc-170)
4. [CMake](https://cmake.org/download/) 3.24 이상을 설치하고 PATH에 추가한다. 아래 명령은 CMake와 CTest가 PATH에 있다는 전제다.

저장소의 Windows preset은 `NMake Makefiles`를 사용한다. NMake는 VS C++ 도구에 포함되며, 이 절차에서는 Ninja를 따로 설치할 필요가 없다.

### 3.2 Qt 6

Qt 설치 프로그램에서 **Qt 6 → MSVC 2022 64-bit**를 선택한다.
프로젝트에서 필요한 모듈은 Core, Gui, Widgets, OpenGL, OpenGLWidgets다.
Qt Creator는 선택 사항이다. 이 매뉴얼의 컴파일러와 맞도록 MinGW용 패키지 대신 MSVC용을 설치한다.

현재 PC에서 확인한 Qt는 **6.11.0 / msvc2022_64**다. 아래 예제도 이 경로를 사용한다.
다른 Qt 6 버전을 설치했다면 `QT_ROOT`를 실제 설치 경로로 바꾼다.
소스가 요구하는 최소 Qt는 6.2이며, 최소 버전 전체에 대한 새 PC 검증을 수행한 것은 아니다.
[Qt Windows 지원 구성 및 설치 안내](https://doc.qt.io/qt-6/windows.html)

예시 설치 위치:

```text
C:\Qt\6.11.0\msvc2022_64
```

### 3.3 PowerShell에 x64 빌드 환경 적용

이 문서의 명령은 **PowerShell 문법**이다. 일반 PowerShell 창에서 아래를 실행한다.
VS 설치 경로는 `vswhere`로 찾으므로 Community와 Build Tools의 경로 차이를 직접 맞출 필요가 없다.

```powershell
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vsRoot = & $vswhere -latest -products * -version '[17.0,18.0)' `
  -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
  -property installationPath
if (-not $vsRoot) { throw 'Visual Studio 2022의 C++ x64 빌드 도구를 설치하세요.' }

& "$vsRoot\Common7\Tools\Launch-VsDevShell.ps1" `
  -Arch amd64 -HostArch amd64 -SkipAutomaticLocation

Get-Command git, cmake, ctest, cl, nmake
$env:VSCMD_ARG_TGT_ARCH
cmake --version
```

마지막 아키텍처 출력이 `x64`인지 확인한다. 새 터미널에서는 빌드 환경을 다시 적용해야 한다.
명령 프롬프트의 `set`과 PowerShell의 `$env:` 문법을 섞어 쓰지 않는다.

스크립트 실행 정책 때문에 `Launch-VsDevShell.ps1`이 차단되면, VS의 **x64 Native Tools Command Prompt for VS 2022**를 열고 그 안에서 `powershell -NoProfile`을 실행해 같은 환경을 상속받을 수 있다.

### 3.4 FFTW 설치

CPU 처리에는 단정밀도 라이브러리 `fftw3f`가 필요하다. 여기서는 vcpkg의 `x64-windows` 패키지를 사용한다.
아래는 `C:\DEV\vcpkg`가 아직 없는 새 PC에서 실행하는 예제다. 이미 설치했다면 기존 위치를 사용한다.

```powershell
New-Item -ItemType Directory -Path C:\DEV -Force | Out-Null
git clone https://github.com/microsoft/vcpkg.git C:\DEV\vcpkg
if ($LASTEXITCODE -ne 0) { throw 'vcpkg clone 실패' }

Set-Location C:\DEV\vcpkg
.\bootstrap-vcpkg.bat
if ($LASTEXITCODE -ne 0) { throw 'vcpkg bootstrap 실패' }

.\vcpkg.exe install fftw3:x64-windows
if ($LASTEXITCODE -ne 0) { throw 'FFTW 설치 실패' }

Test-Path .\installed\x64-windows\include\fftw3.h
Test-Path .\installed\x64-windows\bin\fftw3f.dll
.\vcpkg.exe list fftw3
git rev-parse HEAD
```

두 `Test-Path`는 모두 `True`여야 한다. vcpkg 커밋과 패키지 버전을 기록해 두면 환경 차이를 추적하기 쉽다.
이 저장소는 의존성 버전을 고정하는 vcpkg manifest를 제공하지 않으므로, 새로 설치되는 FFTW 버전은 기존 PC와 다를 수 있다.
문서 검증 PC의 FFTW는 `3.3.10#10`이었다.
[vcpkg 설치](https://learn.microsoft.com/en-us/vcpkg/get_started/get-started), [패키지 설치 명령](https://learn.microsoft.com/en-us/vcpkg/commands/install), [FFTW 패키지](https://vcpkg.io/en/package/fftw3)

## 4. 프로젝트 받기

짧은 경로를 사용하면 빌드 도구의 경로 문제를 줄일 수 있다. 아래부터 프로젝트 위치는 `C:\DEV\FMCW_LiDAR`로 가정한다.

```powershell
Set-Location C:\DEV
git clone --branch main https://github.com/min5921/FMCW_LiDAR_v1.git FMCW_LiDAR
if ($LASTEXITCODE -ne 0) { throw '프로젝트 clone 실패' }

Set-Location C:\DEV\FMCW_LiDAR
git status --short --branch
git log -1 --oneline
git remote -v
```

작성 시점의 예상 커밋은 `5a820da`다. 저장소가 비공개이면 접근 권한이 있는 GitHub 계정으로 인증한다.

GitHub가 나중에 갱신되어도 **이 문서의 커밋을 정확히 재현**하고 싶다면, 새 clone에서 다음과 같이 별도 브랜치를 만든다.

```powershell
git switch -c codex/windows-setup-5a820da 5a820daeb3f55f5d3e622f84995cb88c11d52411
```

일반적인 최신 `main` 개발에서는 위 고정 브랜치 생성 명령을 생략한다.

## 5. 기본 빌드: CPU + 시뮬레이터

### 5.1 PC별 경로 지정

3.3의 x64 개발 환경을 적용한 **같은 PowerShell**에서 실행한다.

```powershell
Set-Location C:\DEV\FMCW_LiDAR
$env:QT_ROOT = 'C:\Qt\6.11.0\msvc2022_64'
$fftwRoot = 'C:\DEV\vcpkg\installed\x64-windows'
$env:PATH = "$env:QT_ROOT\bin;$fftwRoot\bin;$env:PATH"

Test-Path "$env:QT_ROOT\lib\cmake\Qt6\Qt6Config.cmake"
Test-Path "$env:QT_ROOT\bin\windeployqt.exe"
Test-Path "$fftwRoot\bin\fftw3f.dll"
```

세 항목이 모두 `True`인지 확인한다. 다른 버전의 Qt가 이미 PATH에 있어도 이 세션에서는 지정한 Qt의 `bin`을 앞에 둔다.

### 5.2 Configure → Build → Test

```powershell
cmake --preset windows-msvc-release `
  "-DCMAKE_PREFIX_PATH=$env:QT_ROOT" `
  "-DFFTW_ROOT=$fftwRoot" `
  -DFMCW_WITH_FFTW=ON `
  -DFMCW_WITH_CUDA=OFF `
  -DFMCW_WITH_ALAZAR=OFF
if ($LASTEXITCODE -ne 0) { throw 'CMake configure 실패' }

cmake --build --preset windows-msvc-release
if ($LASTEXITCODE -ne 0) { throw '빌드 실패' }

ctest --preset windows-msvc-release -E '^fmcw_phase7_realtime_probe$'
if ($LASTEXITCODE -ne 0) { throw '테스트 실패: 출력된 실패 항목을 확인하세요.' }

& .\build\preset-windows-msvc-release\tests\fmcw_phase7_realtime_probe.exe --backend=fftw
if ($LASTEXITCODE -ne 0) { throw 'CPU realtime probe 실패' }
```

**기준 커밋의 CPU 전용 테스트 주의점:** `ctest --preset windows-msvc-release`를 그대로 실행하면 `fmcw_phase7_realtime_probe`가 FFTW와 CUDA를 모두 요구한다.
CUDA가 없을 때 `CUDA result=SKIP`을 출력해도 종료 코드는 1이어서 CTest가 실패한다.
위 명령은 나머지 9개 테스트를 CTest로 실행하고, 해당 probe는 `--backend=fftw`로 별도 실행한다.
이는 테스트를 생략하는 방법이 아니라 CPU 환경에 맞춰 같은 probe의 backend를 지정하는 방법이다.
다른 테스트 실패나 `functional=FAIL`은 이 제약으로 간주하지 말고 원인을 확인한다.

Configure 로그에서 다음을 확인한다.

```text
FMCW target platform: WINDOWS
FFTW CPU backend: enabled
CUDA cuFFT backend: disabled
AlazarTech ATS-SDK adapter: disabled (set ALAZAR_SDK_ROOT to enable)
Qt version: 6.11.0
```

Qt 버전은 설치한 버전에 따라 달라진다. **FFTW가 disabled인데 configure가 성공할 수도 있으므로 반드시 enabled를 확인한다.**
이 버전은 일부 의존성이 없어도 빌드를 계속하고, 해당 처리를 실행할 때 오류를 알리는 구조다.

빌드 결과:

```text
build\preset-windows-msvc-release\src\fmcw_lidar_windows.exe
```

### 5.3 실행과 기본 동작 확인

프로젝트 루트에서 실행한다. 상대 경로의 파형 파일과 저장 폴더를 찾기 위한 기준 위치다.

```powershell
Start-Process -FilePath .\build\preset-windows-msvc-release\src\fmcw_lidar_windows.exe `
  -WorkingDirectory (Get-Location).Path
```

1. 상단 폴더 아이콘 **Load profile**에서 `config\profiles\lab_simulator.yaml`을 연다.
2. Digitizer의 acquisition source가 **Simulator**인지 확인한다.
3. Processing의 Backend가 **FFTW (CPU)**인지 확인한다.
4. EDFA는 `none`, MCU는 disabled, RAW/processed 저장과 UDP는 꺼진 상태를 확인한다.
5. **Apply Setup → Connect → START** 순서로 진행한다.
6. Live View에서 Time Domain, FFT, B-scan과 3D Point Cloud를 확인하고 **STOP**으로 종료한다.

이 기준 커밋의 GUI는 시작할 때 코드의 기본 simulator 설정을 사용한다.
`config/windows.yaml`을 편집했다고 시작 시 자동으로 반영된다고 가정하지 말고, GUI에서 프로필을 **Load → Apply Setup**한다.
Lab Simulator 프로필은 실시간 성능 평가용 기본 프로필보다 가벼운 첫 실행 경로다.

UI 초기화만 자동 확인하려면 다음을 실행한다. 이 검사는 실제 acquisition, GPU 화면, 실물 장비 동작까지 보증하지 않는다.

```powershell
$smoke = Start-Process `
  -FilePath .\build\preset-windows-msvc-release\src\fmcw_lidar_windows.exe `
  -ArgumentList '--smoke-test','-platform','offscreen' `
  -PassThru -Wait -WindowStyle Hidden
$smoke.ExitCode
```

종료 코드 `0`이 정상이다.

## 6. 더블클릭 실행용 패키지 만들기

Release 빌드 성공 후, 같은 PowerShell에서 실행한다.

```powershell
Set-Location C:\DEV\FMCW_LiDAR
.\deploy\windows\package.ps1
```

스크립트는 Qt 배포 도구 `windeployqt`로 DLL과 플랫폼 플러그인을 모으고, `config/`를 복사한 다음 smoke test를 실행한다.
출력 위치는 다음과 같다.

```text
build\package\FMCW_LiDAR\
  FMCW_LiDAR.exe
  Qt6Core.dll
  Qt6Widgets.dll
  fftw3f.dll
  platforms\qwindows.dll
  config\...
```

`FMCW_LiDAR.exe`를 더블클릭한다. 다른 PC에 실행본만 전달할 때는 **FMCW_LiDAR 폴더 전체**를 복사한다.
`build\...\src`의 EXE 하나만 복사하면 Qt DLL이나 플러그인이 없어 실행되지 않을 수 있다.
패키지를 다시 만들 때도 원본 Release 빌드의 Qt와 동일한 `QT_ROOT`를 사용한다.

CPU 전용으로 빌드한 패키지는 해당 빌드의 기능만 포함한다. GPU 드라이버, Alazar 보드 드라이버, USB-UART 드라이버는 패키지 복사와 별도로 설치한다.
CPU 전용 환경에서 `ATSApi.dll was not found` 경고가 뜨면 ATS를 사용하지 않는 빌드인지 확인한다.

개발 도구가 없는 실행 전용 PC에는 빌드 도구 버전 이상인 **Visual C++ v14 Redistributable x64**도 필요하다.
패키지에 `vc_redist.x64.exe`가 있으면 대상 PC에서 설치하거나 [Microsoft 공식 배포 페이지](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist?view=msvc-170)를 사용한다.
`--compiler-runtime`이 설치 프로그램을 복사해도 대상 PC에 자동 설치되지는 않는다.
패키징 시 `VCINSTALLDIR is not set` 경고가 나오면 3.3의 VS 개발 환경을 적용한 터미널에서 다시 실행한다.

패키지의 자동 실행 확인은 `--smoke-test`만 사용한다. 5.3의 `-platform offscreen`은 개발 환경 확인용이며, 기본 패키지에는 `qoffscreen.dll`이 포함되지 않는다.

## 7. 선택: CUDA/cuFFT 활성화

지원 NVIDIA GPU와 드라이버, MSVC와 호환되는 CUDA Toolkit을 설치한다.
현재 PC에는 CUDA Toolkit 13.1이 있으나, 다른 GPU에도 이 버전을 그대로 적용할 수 있다고 보장하지는 않는다.
GPU·Windows·컴파일러 호환성은 [NVIDIA Windows 설치 안내](https://docs.nvidia.com/cuda/cuda-installation-guide-microsoft-windows/index.html)에서 확인한다.

```powershell
nvidia-smi
nvcc --version
```

`nvidia-smi`의 CUDA 표시는 드라이버 지원 정보이며, Toolkit 설치 여부는 `nvcc --version`으로 별도 확인한다.
CPU 환경의 `$env:QT_ROOT`, `$fftwRoot`, PATH를 설정한 뒤 **별도 빌드 폴더**를 사용한다.

```powershell
cmake --preset windows-msvc-release -B build/windows-gpu-release `
  "-DCMAKE_PREFIX_PATH=$env:QT_ROOT" `
  "-DFFTW_ROOT=$fftwRoot" `
  -DFMCW_WITH_FFTW=ON `
  -DFMCW_WITH_CUDA=ON `
  -DFMCW_REQUIRE_CUDA=ON `
  -DFMCW_CUDA_ARCHITECTURES=auto `
  -DFMCW_WITH_ALAZAR=OFF
if ($LASTEXITCODE -ne 0) { throw 'GPU configure 실패' }

cmake --build build/windows-gpu-release
if ($LASTEXITCODE -ne 0) { throw 'GPU 빌드 실패' }

ctest --test-dir build/windows-gpu-release --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'GPU 테스트 실패' }

.\deploy\windows\package.ps1 `
  -BuildDirectory 'build/windows-gpu-release/src' `
  -OutputDirectory 'build/package/FMCW_LiDAR_GPU'
```

Configure에 `CUDA cuFFT backend: enabled`가 나와야 한다.
실행 후 Processing에서 **CUDA cuFFT → Apply Setup**을 선택한다. FFTW 프로필을 불러왔으면 backend를 다시 확인한다.
`auto`는 빌드 PC의 GPU를 기준으로 하므로, 다른 GPU에 배포할 때는 대상 PC에서 재빌드하거나 대상 architecture를 명시한다.
CUDA를 뒤늦게 설치한 경우 새로운 빌드 폴더를 사용하면 이전의 미검출 캐시를 피할 수 있다.

## 8. 선택: Alazar, MCU, EDFA 연결

### 8.1 Alazar

1. 정확한 보드 모델의 Windows 드라이버와 x64 ATS-SDK/runtime을 설치한다. 현재 개발 환경의 SDK 기준은 **25.1.0**이다. [AlazarTech 시작 안내](https://docs.alazartech.com/ats-sdk-user-guide/latest/getting-started.html)
2. 제조사 도구에서 보드가 인식되는지 먼저 확인한다.
3. CPU 실장비 빌드는 다음처럼 별도 디렉터리에 구성한다. GPU까지 사용할 경우 7장의 CUDA 옵션도 함께 지정한다.

```powershell
cmake --preset windows-msvc-release -B build/windows-hardware-release `
  "-DCMAKE_PREFIX_PATH=$env:QT_ROOT" `
  "-DFFTW_ROOT=$fftwRoot" `
  -DFMCW_WITH_FFTW=ON `
  -DFMCW_WITH_CUDA=OFF `
  -DFMCW_WITH_ALAZAR=ON `
  '-DALAZAR_SDK_ROOT=C:/AlazarTech/ATS-SDK/25.1.0'
if ($LASTEXITCODE -ne 0) { throw '실장비 configure 실패' }

cmake --build build/windows-hardware-release
if ($LASTEXITCODE -ne 0) { throw '실장비 빌드 실패' }

ctest --test-dir build/windows-hardware-release --output-on-failure `
  -E '^fmcw_phase7_realtime_probe$'
if ($LASTEXITCODE -ne 0) { throw '실장비 빌드의 소프트웨어 테스트 실패' }

& .\build\windows-hardware-release\tests\fmcw_phase7_realtime_probe.exe --backend=fftw
if ($LASTEXITCODE -ne 0) { throw 'CPU realtime probe 실패' }

.\deploy\windows\package.ps1 `
  -BuildDirectory 'build/windows-hardware-release/src' `
  -OutputDirectory 'build/package/FMCW_LiDAR_Hardware'
```

`AlazarTech ATS-SDK adapter: enabled`가 필수다. 헤더와 x64 `ATSApi.lib`가 함께 필요하며, 실행에는 `ATSApi.dll`이 필요하다.
이 기준 커밋의 패키징 스크립트는 ATS DLL을 Windows `System32`와 SDK **25.1.0의 고정 경로**에서 찾는다.
다른 SDK 위치를 사용하면 제조사 설치로 runtime을 제공하거나, 그 SDK와 일치하는 x64 `ATSApi.dll`을 패키지 폴더에 별도로 넣어야 한다.

GUI에서 acquisition source를 Alazar로 바꾸고 감지된 보드와 설정을 확인한다. System/Board ID는 `1 / 1`을 사용한다.
첫 수집은 EDFA `none`, MCU disabled로 확인한 뒤 [실장비 검증 절차](hardware_acceptance.md)를 따른다.
CTest 통과는 실제 보드의 DMA·trigger·광학계 검증을 대신하지 않는다.

### 8.2 MCU와 EDFA

새 PC의 장치 관리자에서 COM 번호를 확인하고 프로필의 MCU/EDFA 포트를 바꾼다. 이전 PC의 `COM3` 등이 그대로 유지된다고 가정하지 않는다.
UART 제어는 앱이 직접 수행하며, 장치에 맞는 USB-UART/FTDI 드라이버는 필요하다.

MCU 파형의 기본 위치는 `config/waveforms/mems_xym_100ksps.txt`이며 Git에 포함되어 있다.
MCU 설정 적용 후 파형을 업로드하고 START한다. EDFA 출력 시험은 [실장비 검증 문서](hardware_acceptance.md)의 순서를 따른다.

펌웨어까지 수정할 때만 STM32CubeIDE에서 다음 프로젝트를 가져온다.

```text
src\firmware\mcu\FMCW_LiDAR_MCU
```

각 PC에서 ST-LINK와 Debug Configuration을 다시 설정한다. 자세한 방법은 [MCU README](../src/firmware/mcu/FMCW_LiDAR_MCU/README.md)를 참조한다.

## 9. Git으로 옮겨지지 않는 것

| 항목 | 새 PC에서 할 일 |
|---|---|
| Qt, FFTW, CUDA, ATS-SDK 및 장치 드라이버 | 설치하고 실제 경로 지정 |
| `CMakeUserPresets.json` | 새 PC 경로로 다시 작성; Git에서 제외됨 |
| `build/`, `out/`, EXE/DLL/LIB | 개발용은 재빌드; 실행용은 패키지 전체 전달 |
| `data/raw/`, `data/processed/`, `outputs/`의 실측 데이터 | 필요한 세션을 별도로 복사 |
| 샘플 데이터 | 기준 커밋의 `data/samples/`에는 `.gitkeep`만 있으므로 별도 준비 |
| 미커밋 변경, 푸시하지 않은 커밋·브랜치 | 새 clone에 없음; 의도한 변경을 검토해 커밋·푸시한 뒤 동기화 |
| 개인 프로필, 보정값, 별도 파형 | 실제 Git 추적 여부 확인 후 누락 파일 복사 |
| COM 포트, UDP 대상 IP, 저장 경로 | 새 장비 환경에 맞게 수정 |

RAW replay용 데이터는 RAW 파일 하나만 옮기지 말고 **동일 세션의 setup YAML, JSON metadata와 필요한 파형 파일까지 함께** 옮긴다.
재생 파일을 선택하면 저장된 설정을 복원하므로 새 PC에서 backend와 파일 경로를 다시 확인한다.

기존 PC에서 누락 여부를 확인하는 명령:

```powershell
git status --short --branch
git log --oneline origin/main..HEAD
git ls-files config
git ls-files data
git check-ignore -v CMakeUserPresets.json
```

`origin/main`의 로컬 정보가 오래되었으면 먼저 `git fetch origin`으로 갱신한다.
기존 `build/`의 CMake cache에는 PC별 절대 경로가 있으므로 새 PC의 개발 빌드에 재사용하지 않는다.

## 10. 평소 개발과 두 PC 간 동기화

### 소스를 고친 뒤

실행 중인 앱을 닫고 같은 x64 개발 PowerShell에서 다시 빌드한다.

```powershell
cmake --build --preset windows-msvc-release
if ($LASTEXITCODE -ne 0) { throw '빌드 실패' }
ctest --preset windows-msvc-release -E '^fmcw_phase7_realtime_probe$'
if ($LASTEXITCODE -ne 0) { throw '테스트 실패' }
& .\build\preset-windows-msvc-release\tests\fmcw_phase7_realtime_probe.exe --backend=fftw
if ($LASTEXITCODE -ne 0) { throw 'CPU realtime probe 실패' }
```

더블클릭용 패키지를 사용 중이면 `deploy/windows/package.ps1`도 다시 실행해야 새 EXE가 패키지에 반영된다.
GPU/실장비 구성은 7·8장에서 지정한 빌드 디렉터리를 사용한다.

디버깅이 필요하면 5.2의 configure 명령에서 preset을 `windows-msvc-debug`로 바꾸고 동일한 경로와 기능 옵션을 전달한다.
빌드와 테스트도 `windows-msvc-debug` preset을 사용하고, 개별 probe EXE 경로도 `preset-windows-msvc-debug`로 바꾼다. Debug 실행 시 FFTW PATH는 `$fftwRoot\debug\bin`을 사용하고, Debug 산출물을 Release 패키징 스크립트에 전달하지 않는다.
코드 편집은 Visual Studio/VS Code 등에서 저장소 루트를 열어 진행할 수 있다.

### 한 PC에서 작업을 마칠 때

1. `git status`와 `git diff`로 변경을 검토한다.
2. 공유할 소스·설정·문서만 선택적으로 `git add <파일>` 후 커밋한다.
3. 작업 브랜치를 푸시한다. 최초에는 `git push -u origin <작업브랜치>`를 사용한다.
4. 상대 PC에서 같은 브랜치를 받아 작업한다.

예를 들어 두 PC가 모두 같은 `main`을 사용하고 작업 폴더가 깨끗한 경우:

```powershell
git switch main
git pull --ff-only origin main
git log -1 --oneline
```

작업 브랜치는 `main` 자리에 실제 브랜치명을 사용한다. 새 PC에서 원격 브랜치를 처음 가져올 때는 `git fetch origin` 후 `git switch --track origin/<작업브랜치>`를 사용한다.
`--ff-only`가 실패하면 두 PC에서 이력이 갈라진 것이므로 상태를 확인해 merge/rebase를 결정한다. 강제 reset이나 force push로 덮어쓰지 않는다.

## 11. 자주 생기는 문제

| 증상 | 확인 및 조치 |
|---|---|
| `cl`, `nmake`를 찾지 못함 | 3.3의 VS 2022 x64 개발 환경을 현재 PowerShell에 적용 |
| `Qt6Config.cmake`를 찾지 못함 | `CMAKE_PREFIX_PATH`가 Qt의 `msvc2022_64` 디렉터리인지 확인 |
| Qt 5 또는 MinGW와 섞임 | `QT_ROOT`, PATH와 선택한 Qt 패키지를 확인; 필요하면 새 빌드 폴더로 구성 |
| FFTW backend가 disabled | `FFTW_ROOT`, `include/fftw3.h`, `lib/fftw3f.lib`, `bin/fftw3f.dll` 확인 |
| CPU 구성에서 realtime probe만 실패하고 CUDA SKIP 출력 | 5.2처럼 probe를 `--backend=fftw`로 실행; FFTW 자체 실패와 구분 |
| EXE는 열리는데 START에서 FFT 오류 | GUI backend가 실제 빌드에 포함된 FFTW/CUDA와 맞는지 확인 |
| `Qt6Core.dll`, `Qt6Widgets.dll` 없음 | 개발 실행은 Qt `bin`을 PATH에 추가; 배포 실행은 패키지 전체 사용 |
| Qt platform plugin `windows` 오류 | 패키지의 `platforms/qwindows.dll`과 Qt 버전 일치를 확인 |
| `VCRUNTIME140.dll` 또는 `MSVCP140.dll` 없음 | 대상 PC에 Visual C++ v14 Redistributable x64 설치 |
| ATSApi DLL 관련 실행 실패 | SDK와 보드 runtime의 x64 버전 일치 및 DLL 검색 경로 확인 |
| CUDA compiler/backend를 찾지 못함 | `nvcc --version`, GPU·드라이버·MSVC 호환성 확인 후 새 빌드 폴더로 구성 |
| 다른 PC 경로나 generator 관련 CMake 오류 | 복사한 cache를 사용하지 말고 새 빌드 폴더에서 configure |
| Qt configure의 `WrapVulkanHeaders` 미검출 | 본 검증에서는 해당 메시지가 있어도 Qt/OpenGL 앱 빌드 성공; 최종 configure 결과로 판단 |
| MCU 파형 파일을 찾지 못함 | 프로젝트 루트/패키지 폴더에서 실행하고 waveform 경로 확인 |
| 장치 연결 실패 | COM 번호, 보드 드라이버, 다른 프로그램의 포트 점유 확인 |
| 새 PC에 최근 기능이 보이지 않음 | 두 PC와 GitHub의 브랜치 및 커밋 해시 비교 |

## 12. 설치 완료 확인과 검증 범위

- [ ] `cl`, `nmake`, `cmake`, `ctest`가 현재 터미널에서 실행된다.
- [ ] 의도한 Git 커밋과 브랜치를 받았다.
- [ ] Qt MSVC x64와 `FFTW CPU backend: enabled`를 확인했다.
- [ ] Release 빌드, CPU 구성의 CTest 9개와 FFTW 단독 probe가 통과했다.
- [ ] `--smoke-test` 종료 코드가 0이다.
- [ ] Lab Simulator 프로필로 START/STOP과 Live View를 확인했다.
- [ ] 필요한 경우 패키지 폴더 전체를 옮겨 대상 PC에서 실행했다.
- [ ] 실측 데이터, 프로필, 파형, COM/IP/저장 경로를 확인했다.

이 문서의 명령은 GitHub 기준 커밋을 별도 폴더에 추출한 소스와 대조했다.
2026-09-17에 기존 PC의 MSVC 19.44, CMake 4.3.2, Qt 6.11.0, FFTW 3.3.10#10으로 다음을 확인했다.

| 검증 | 결과 |
|---|---|
| CUDA/Alazar를 끈 CPU Release 전체 빌드 | 성공 |
| 원래 CTest 전체 실행 | 9/10 통과; realtime probe의 CUDA 미사용 처리 때문에 1개 실패 |
| 문서의 CPU용 CTest 명령 | 9/9 통과 |
| realtime probe `--backend=fftw` | 종료 코드 0, `functional=PASS`, `HARD_PASS` |
| 개발 EXE `--smoke-test -platform offscreen` | 종료 코드 0 |
| VS x64 개발 환경에서 Windows 패키징 | 성공 |
| 패키지 EXE `--smoke-test` | Qt/FFTW `bin`을 PATH에서 제거한 상태에서 종료 코드 0 |

새 PC에 도구를 처음 설치하는 과정, GPU 렌더링, CUDA 구성, 실물 장비 검증은 해당 PC에서 수행해야 한다.
2초 probe 결과는 10분 실시간 성능 인증을 의미하지 않는다. `*_acceptance` 실행 파일은 일반 CTest에 포함되지 않으며 초기 설치 확인용으로 실행할 필요가 없다.

프로젝트 내부 참고: [빌드 설정](build_setup.md), [설정 형식](configuration.md), [장비 검증](hardware_acceptance.md), [장치 프로토콜](device_protocols.md).
