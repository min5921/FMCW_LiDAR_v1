# Basic 작업 공간

- 폴더: `FMCW_LiDAR`
- 브랜치: `codex/fmcw-base`
- 목적: 기본 계측 · FFT · Live 3D 표시 · 저장 · RAW 재생
- 실행: 루트의 `RUN.cmd` 더블클릭
- Windows preset: `windows-base` (Alazar ATS + CUDA FFT + FFTW)
- CPU 개발 preset: `windows-base-cpu` (Simulator / RAW, Alazar·CUDA 제외)
- 빌드 폴더: `build/preset-windows-base`
- Windows 패키지: `build/package/Windows-Basic/FMCW_LiDAR.exe`
- Jetson 소스 배포본: `build/package/Jetson-Basic/FMCW_LiDAR_Basic_Jetson_Source.zip`
- Jetson 안내: [빌드·배포 매뉴얼](deploy/jetson/README_KO.md) (CUDA 전용, PCD·Weight 제외)

이전 v2의 계측·FFT 구성을 유지하고, PCD 파일 재생·Weight 선택·CenterPoint 추론은 제외합니다.
RAW 재생, MCU/EDFA 제어, 실시간 3D 표시와 저장은 유지합니다.

빌드와 다른 PC 설정은 [세 프로젝트 관리 안내](docs/guides/workspaces_ko.md)를 따릅니다.
공통 수정은 작은 커밋으로 공유하고 제품 브랜치 전체를 병합하지 않습니다.
현재 실행본은 `build/package/`, 현재 검증 기록은 `build/reports/`에서 관리합니다.
이전 실행본·로그·복구 자료는 `build/package_archive/2026-09-22/`에 모아 보관합니다.

[문서 목록](docs/README.md) · [소스 파일 찾기](src/README.md) · [기본 v2 구성과 검증 범위](docs/archive/basic_v2_2026-09-22.md)
