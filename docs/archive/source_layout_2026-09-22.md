# 기본 FMCW 소스 정리 기록

대상은 `FMCW_LiDAR` / `codex/fmcw-base`이다. 다른 제품의 작업 공간은 변경하지 않았다.
현재 파일을 찾는 안내는 [src/README.md](../../src/README.md)를 따른다.

## 이동한 파일

| 이전 위치 | 현재 위치 | 목적 |
|---|---|---|
| `src/apps/common/application_controller.*`, `replay_setup_loader.*` | `src/application/` | 실행 제어·RAW 재생 준비 |
| `src/apps/common/main_window.*` | `src/ui/` | 화면·사용자 입력 |
| `src/apps/common/plot_widgets.*` | `src/ui/plots/` | 2D 그래프 |
| `src/apps/common/point_cloud_*` | `src/ui/point_cloud/` | 3D 표시·카메라 |
| `src/core/`에 모여 있던 제품 파일 | `core/acquisition`, `config`, `runtime`, `devices`, `types`, `diagnostics` | 역할별 분류 |
| `src/core/config_manager.*`, `operation_controller.*` | `tests/support/config_prototypes/` | 테스트에만 사용하는 초기 설계 |
| `src/storage/point_cloud_replay.*`, `async_point_cloud_replay.*` | `tests/support/point_cloud_replay/` | 파일 포맷·렌더링 회귀 테스트 |
| `src/detection/`와 연결되지 않은 관련 테스트 | `legacy/centerpoint_reference/` | 미사용 CenterPoint 참고 코드 보존 |

총 67개 파일을 이동했다. 이 중 15개 CenterPoint 참고 파일은 내용 그대로 보존했다.
`src`의 불필요한 `.gitkeep` 8개와 비어 있던 `visualization/` 폴더를 정리했다. 시각화 구현은 `ui/plots/`, `ui/point_cloud/`에 있다.
제품·테스트 C++ 코드의 수정은 include 경로 치환으로 제한했다. 계측·FFT·저장·UI 동작은 변경하지 않았다.
MCU CubeIDE 프로젝트 내부 경로는 유지했다.

## 빌드와 검증

- CMake 소스 경로와 테스트 전용 include 경로를 갱신했다.
- 소스 해시 목록에 `tests/**/*.h`도 포함해 이동한 테스트 보조 헤더를 추적한다.
- Windows Alazar + CUDA + FFTW: 빌드 및 CTest **24/24 통과**.
- Windows CPU 개발 구성: 빌드 및 CTest **22/22 통과**.
- 이동 전 사본과 비교해 제품·테스트 소스가 include 경로 외에는 달라지지 않았음을 확인했다.
- Windows 패키지는 개발 도구 PATH 없이 시작하는 smoke test를 통과했다.
- Jetson은 같은 폴더 구조로 소스 ZIP을 갱신한다. 실제 ARM64 빌드·실행은 별도로 확인해야 한다.

Windows 실행 위치는 `build/package/Windows-Basic/FMCW_LiDAR.exe`이다.
교체 전 Windows 패키지는 `build/package_archive/2026-09-22/Windows-Basic-before-source-layout/`에 보관한다.
Jetson 이전 소스 배포본은 exporter가 `build/package_archive/<날짜>/Jetson-Basic-Source-<시간>/`에 보관한다.

파일별 이전/현재 경로, 원본 사본, 해시 확인과 빌드 로그는
`build/reports/source-layout-20260922/`에 있다. 과거 날짜의 리뷰·보고서는 작성 당시 경로를 유지한다.
