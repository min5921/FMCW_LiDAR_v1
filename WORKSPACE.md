# CenterPoint 작업 공간

- 폴더: `FMCW_LiDAR_CenterPoint`
- 브랜치: `codex/centerpoint`
- 목적: 기본 계측 + CenterPoint 객체 탐지 + 파일 입력
- 실행: 루트의 `RUN.cmd` 더블클릭
- Windows preset: `windows-centerpoint`
- 빌드 폴더: `build/preset-windows-centerpoint`
- Windows 패키지: `build/package/Windows-CenterPoint/FMCW_LiDAR.exe`

객체 탐지에는 외부 CenterPoint runtime, CUDA/cuDNN, 모델 가중치가 필요합니다.

빌드와 다른 PC 설정은 [세 프로젝트 관리 안내](docs/workspaces_ko.md)를 따릅니다.
공통 수정은 작은 커밋으로 공유하고 제품 브랜치 전체를 병합하지 않습니다.
이전 실행본은 `build/package_archive/`, 이전 검증 자료는 `build/archive/`에 보관합니다.

[문서 목록](docs/README.md) · [검증 결과와 한계](docs/workspace_verification_2026-09-22.md)
