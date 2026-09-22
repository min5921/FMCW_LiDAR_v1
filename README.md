# FMCW LiDAR · Basic

**계측 데이터를 받아 FFT로 거리·속도를 계산하고, 그래프·실시간 3D로 확인하며 저장하는 기본 프로그램입니다.**

현재 작업 공간은 `FMCW_LiDAR`, 전용 브랜치는 `codex/fmcw-base`입니다. Windows와 Jetson이 공통 소스를 사용합니다.

## 처음 오셨나요?

| 하고 싶은 일 | 여기서 시작하세요 |
|---|---|
| 지금 프로그램 실행하기 | 이 폴더의 **[RUN.cmd](RUN.cmd)** 더블클릭 |
| 폴더와 코드 이해하기 | **[소스 설명서](docs/source/source_guide_ko.md)** → [파일별 역할 목록](docs/source/source_file_index_ko.md) |
| 다른 Windows PC에서 개발하기 | **[Windows 시작 안내](docs/guides/windows_setup_ko.md)** |
| Jetson에서 빌드·실행하기 | **[Jetson 배포 안내](deploy/jetson/README_KO.md)** |
| 설정·장비·설계 문서 찾기 | **[문서 목록](docs/README.md)** |

## 이 버전에서 할 수 있는 일

- **입력:** Alazar 계측 보드, 시뮬레이터, 저장된 RAW 재생
- **처리:** CPU FFTW 또는 GPU CUDA/cuFFT, 피크·거리·속도 계산
- **표시:** 시간 파형, FFT, 피크 분석, 거리·속도, B-scan, 실시간 3D 점군
- **연결·출력:** MCU·EDFA 제어, RAW·처리 결과 저장, UDP 전송

PCD 파일 열기·재생은 **PCDReplay**, 모델 가중치·객체 탐지는 **CenterPoint** 작업 공간에서 개발합니다.
[세 프로젝트의 범위와 브랜치](docs/guides/workspaces_ko.md)를 확인하세요.

## 실행하기

### Windows

배포본이 준비된 이 PC에서는 `RUN.cmd`를 실행합니다. 이 파일은 아래 실행 파일을 여는 바로가기 역할을 합니다.

```text
build/package/Windows-Basic/FMCW_LiDAR.exe
```

다른 PC로 실행본만 옮길 때는 **`Windows-Basic` 폴더 전체**를 복사합니다.
Git에는 실행 파일·DLL·SDK가 포함되지 않으므로, 소스만 받은 PC는 먼저 [빌드와 패키징](docs/guides/windows_setup_ko.md)을 해야 합니다.

### Jetson

Windows에서 만든 **소스 배포 ZIP**을 Jetson으로 옮겨 ARM64에서 빌드합니다.

```text
build/package/Jetson-Basic/FMCW_LiDAR_Basic_Jetson_Source.zip
```

Jetson은 CUDA/cuFFT 구성입니다. 설정·명령은 [Jetson 매뉴얼](deploy/jetson/README_KO.md)을 따릅니다.
패키지별 역할과 보관 위치는 [배포 파일 안내](docs/guides/package_layout.md)에 정리했습니다.

## 개발 시작하기

1. Visual Studio의 **x64 개발 환경**에서 [FMCW_Basic.code-workspace](FMCW_Basic.code-workspace)를 엽니다.
2. 아래에서 필요한 CMake preset을 선택하고 **Configure → Build**를 실행합니다.
3. 수정할 파일은 [소스 지도](src/README.md)에서 찾습니다.

| 빌드 설정 | 포함 기능 | 빌드 폴더 |
|---|---|---|
| `windows-base` | Alazar + CPU FFTW + GPU CUDA | `build/preset-windows-base/` |
| `windows-base-cpu` | Simulator·RAW·CPU FFTW, Alazar/CUDA 제외 | `build/preset-windows-base-cpu/` |

이 메뉴는 **우리가 정한 빌드 설정 묶음**입니다. 실행 중에는 포함된 입력·처리 방식 중 사용할 것을 선택합니다.
SDK 경로 설정과 VS Code의 헤더 인식 문제는 [Windows 시작 안내](docs/guides/windows_setup_ko.md)에 설명했습니다.

## 폴더 찾기

| 위치 | 용도 |
|---|---|
| [src/](src/README.md) | 앱 시작·화면·계측·장치 연결·신호 처리·저장 코드 |
| [docs/](docs/README.md) | 사용 안내, 소스 설명, 설계, 장비 문서 |
| `config/` | 실행 설정·보정값·MCU 파형 |
| `tests/` | 기능 검증과 테스트 보조 코드 |
| `deploy/`, `tools/` | 배포·데이터 변환·문서 목록 생성 도구 |
| `data/`, `outputs/` | 측정·재생 데이터와 실행 결과 |
| `build/package/` | 현재 배포본 |
| `build/package_archive/`, `build/reports/` | 이전 배포본 / 검증·작업 기록 |
| [MCU 펌웨어](src/firmware/mcu/FMCW_LiDAR_MCU/README.md) | STM32CubeIDE에서 따로 빌드하는 보드 코드 |
| `Ros_project/`, `hardware/`, `legacy/` | ROS 수신 코드, 하드웨어 자료, 과거 원본 |

## 확인된 상태

2026-09-22 소스 정리 후 Windows 기본 구성 **24/24**, CPU 구성 **22/22** 테스트와 Windows 패키지 시작 검사를 통과했습니다.
Jetson 소스 ZIP의 파일·해시는 확인했으며, 실제 Jetson ARM64 빌드·실측 장비 동작은 별도 확인 대상입니다.

[소스 정리·검증 기록](docs/archive/source_layout_2026-09-22.md) · [작업 공간 범위](WORKSPACE.md) · [과거 기록](docs/archive/README.md)
