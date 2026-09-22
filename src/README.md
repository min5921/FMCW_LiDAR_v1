# 기본 FMCW 소스 안내

처음 읽는다면 [폴더·파일 상세 설명서](../docs/source/source_guide_ko.md)를 먼저 보세요.
파일 하나하나의 설명과 링크는 [전체 파일 목록](../docs/source/source_file_index_ko.md)에 있습니다.

Windows와 Jetson이 같은 계측·처리 코드를 사용합니다. `apps/`의 시작 파일만 플랫폼별로 나뉩니다.
`.h`는 자료형·함수의 선언, `.cpp`는 구현이며 같은 역할의 파일을 같은 폴더에 둡니다. `.cu`는 CUDA로 컴파일하는 GPU 구현입니다.

## 어디를 수정하면 되나요?

| 하고 싶은 작업 | 시작할 파일 / 폴더 |
|---|---|
| Windows 프로그램 시작, 명령행 옵션 | [apps/windows/main.cpp](apps/windows/main.cpp) |
| Jetson 프로그램 시작, 명령행 옵션 | [apps/jetson/main.cpp](apps/jetson/main.cpp) |
| 화면 구성, 버튼, 설정 입력 | [ui/main_window.cpp](ui/main_window.cpp) |
| 시간·FFT·거리 그래프 | [ui/plots/plot_widgets.cpp](ui/plots/plot_widgets.cpp) |
| 실시간 3D 표시, 카메라 조작 | [ui/point_cloud/](ui/point_cloud/) |
| 시작·정지, 장치·처리·저장 연결 | [application/application_controller.cpp](application/application_controller.cpp) |
| RAW 재생 시 측정 설정 복원 | [application/replay_setup_loader.cpp](application/replay_setup_loader.cpp) |
| 계측 세션, 연속 수집, 버퍼 재사용 | [core/acquisition/](core/acquisition/) |
| 설정 파일 읽기·검증·적용 정책 | [core/config/](core/config/) |
| 상태, 중단 요청, 스레드, 통계 | [core/runtime/](core/runtime/) |
| 장치 공통 인터페이스·지원 사양 | [core/devices/](core/devices/) |
| 프레임 자료형·스캔 좌표 | [core/types/](core/types/) |
| 로그·버전 정보 | [core/diagnostics/](core/diagnostics/) |
| Alazar 계측 보드 제어 | [drivers/alazar/](drivers/alazar/) |
| MCU·EDFA·시리얼 통신 | [drivers/mcu/](drivers/mcu/), [drivers/edfa/](drivers/edfa/), [drivers/serial/](drivers/serial/) |
| 장비 없는 시뮬레이터·RAW 재생 | [drivers/simulator/](drivers/simulator/), [drivers/replay/](drivers/replay/) |
| FFT·피크·거리·속도 계산 | [processing/](processing/) |
| CPU FFTW / GPU CUDA 구현 | [processing/cpu/](processing/cpu/), [processing/cuda/](processing/cuda/) |
| RAW·점군 저장, 처리 이력 | [storage/](storage/) |
| UDP 전송 | [network/](network/) |
| STM32 MCU에서 동작하는 펌웨어 | [firmware/mcu/FMCW_LiDAR_MCU/](firmware/mcu/FMCW_LiDAR_MCU/) |

## 폴더 구조

```text
src/
  apps/                Windows / Jetson 실행 시작점
  application/         프로그램 실행 흐름과 RAW 재생 준비
  ui/                  화면
    plots/             2D 그래프
    point_cloud/       3D 점군 표시와 카메라
  core/                공통 기반 코드
    acquisition/       계측 세션과 수집 버퍼
    config/            설정 읽기·검증
    runtime/           상태·중단·스레드·통계
    devices/           장치 공통 계약과 사양
    types/             데이터와 스캔 좌표
    diagnostics/       로그와 버전
  drivers/             실제 장치·시뮬레이터·RAW 재생
  processing/          FFT와 거리·속도 계산
  storage/             저장과 처리 이력
  network/             UDP 전송
  firmware/            MCU 펌웨어 (CubeIDE 프로젝트)
```

데이터는 보통 `drivers → core/acquisition → processing → storage/network/ui` 순서로 흐릅니다.
`application`이 이 작업들의 시작·정지와 상태 전달을 연결합니다.

## 테스트·과거 코드

- 기본 앱의 제품 코드는 이 `src/` 아래에 둡니다. 실제 빌드 대상은 [CMakeLists.txt](CMakeLists.txt)에 명시합니다.
- PCD 파일 읽기와 과거 설정 제어의 회귀 테스트 보조 코드는 [tests/support](../tests/support/README.md)에 있습니다. 기본 앱에는 링크하지 않습니다.
- 기본 제품에서 사용하지 않는 CenterPoint 원본과 관련 테스트는 [legacy/centerpoint_reference](../legacy/centerpoint_reference/README.md)에 보존했습니다. 실제 CenterPoint 개발은 별도 작업 공간에서 합니다.
- Jetson 소스 배포 ZIP에는 `legacy` 보관본을 포함하지 않습니다.
- MCU의 `Core/`, `Drivers/` 등 CubeIDE 내부 구조는 유지합니다. `src/drivers/`는 PC/Jetson 장치 제어 코드이고, `firmware/.../Drivers/`는 MCU용 ST 제공 코드입니다.
- 폴더 이름은 소스의 역할을 나타냅니다. 실제 SDK와 라이브러리의 설치 경로는 CMake에서 설정합니다.

2026-09-22 정리에서는 파일 위치와 include/CMake 경로를 변경했습니다. UI 동작이나 계측 알고리즘은 변경하지 않았습니다.
