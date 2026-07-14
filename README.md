# FMCW LiDAR v1

FMCW LiDAR 시스템을 Windows와 Jetson에서 함께 운용하기 위한 v1 재구성 프로젝트입니다.

## Current Direction

- UI: Qt 6
- Platforms: Windows, Jetson/Linux
- Digitizer: AlazarTech, Windows/Jetson 공통 adapter
- CPU FFT: FFTW
- GPU FFT: CUDA/cuFFT
- Raw storage: high-speed binary + JSON metadata
- 3D viewer: Qt/OpenGL lightweight point cloud renderer
- Live Time/FFT: one operator-selected A-scan from each DMA buffer
- UDP: asynchronous versioned raster point-frame sender

## Key Documents

- `docs/requirements.md`: 시스템 요구사항과 Phase 계획
- `docs/gui_runtime_requirements.md`: 승인된 GUI의 page ownership, global command, snapshot/thread 계약
- `docs/folder_structure.md`: 폴더 구조와 파일 배치 기준
- `docs/data_contract.md`: full-period raw frame과 metadata 규칙
- `docs/processing_storage.md`: FFT/peak/B-scan 처리와 binary 저장/replay 계약
- `docs/qt_ui_mvp.md`: Phase 5 Qt 화면, runtime thread 구조, 실행 및 operator flow
- `docs/configuration.md`: YAML profile schema v4, validation, field presentation 및 pending 정책
- `docs/device_protocols.md`: Alazar AutoDMA, MCU UART, EDFA binary protocol
- `docs/hardware_acceptance.md`: Windows/Jetson 실제 장비 검증 절차
- `docs/build_setup.md`: Windows/Jetson 빌드 준비와 외부 SDK 경로

## Phase Policy

Phase별로 구현 단위를 나누고, 각 Phase가 끝날 때 commit/push한다.

- Phase 0: Legacy inventory and requirement lock
- Phase 1: Build system and core skeleton
- Phase 2: Configuration and system state
- Phase 3: Acquisition and device drivers
- Phase 4: Processing and storage pipeline
- Phase 5: Qt UI MVP
- Phase 6: 3D, UDP, and simulator expansion
- Phase 7: Jetson integration and release hardening

## Windows Application

Run the packaged application by double-clicking:

`build/package/FMCW_LiDAR/FMCW_LiDAR.exe`

Keep the complete `FMCW_LiDAR` package folder together. Phase 6 adds the Live View `3D Point Cloud` tab and activates the Storage / UDP sender settings under the same global START/STOP session.
