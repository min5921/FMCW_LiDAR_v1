# FMCW LiDAR — PCDReplay

**기본 계측 + PCD/pointcloud 파일 재생**

작업 브랜치: `codex/pcd-replay`

## 실행

이 폴더의 **[RUN.cmd](RUN.cmd)를 더블클릭**합니다.

실제 실행 파일: `build/package/Windows-PCDReplay/FMCW_LiDAR.exe`

다른 PC로 실행본을 옮길 때는 **`Windows-PCDReplay` 폴더 전체**를 복사합니다.
Git으로 소스만 받은 PC에서는 먼저 빌드·패키징해야 합니다.

계측 기능을 유지하며, 객체 탐지와 모델 가중치 선택은 포함하지 않습니다.

## 개발 시작

[FMCW_PCDReplay.code-workspace](FMCW_PCDReplay.code-workspace)를 열고, [작업 공간 관리·Windows 빌드 안내](docs/workspaces_ko.md)를 따릅니다.
이 제품의 preset은 `windows-pcd`이고, 빌드 출력은 `build/preset-windows-pcd/`입니다.

| 찾는 파일 | 위치 |
|---|---|
| 프로그램 소스 | `src/` |
| 실행 설정·보정·파형 | `config/` |
| 테스트 | `tests/` |
| 빌드·배포 스크립트 | `deploy/`, `tools/` |
| 설명서 | [docs/README.md](docs/README.md) |
| 측정·재생 데이터 | `data/` |
| 실행 결과 | `outputs/` |
| MCU 펌웨어 | `src/firmware/mcu/FMCW_LiDAR_MCU/` |
| ROS 수신·RViz | `Ros_project/` |
| 기존 원본·하드웨어 자료 | `legacy/`, `hardware/` |
| 이전 실행본 | `build/package_archive/` |
| 이전 검증·프로파일링 자료 | `build/archive/` |

## 확인할 문서

- [제품 범위](WORKSPACE.md)
- [실행 패키지 구성](docs/package_layout.md)
- [빌드·테스트 결과와 하드웨어 검증 범위](docs/workspace_verification_2026-09-22.md)
- [파일 정리 내역](docs/file_organization_2026-09-22.md)
- [정리 이전의 개발 기록](docs/archive/development_readme.md)
