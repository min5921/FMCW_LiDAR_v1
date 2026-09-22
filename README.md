# FMCW LiDAR — Basic

이 폴더는 **계측 · FFT · Live 3D · 저장 · RAW 재생** 프로젝트입니다. 전용 브랜치는 `codex/fmcw-base`입니다.

- [이 작업 공간의 시작 안내](WORKSPACE.md)
- [세 프로젝트 관리 규칙과 다른 PC 설정](docs/workspaces_ko.md)
- 현재 Windows 실행본: `build/package/Windows-Basic/FMCW_LiDAR.exe`

아래는 분리 이전의 공통 개발 기록입니다. 과거 브랜치명·패키지 경로·기능 설명보다 위 작업 공간 안내를 우선합니다.

---

# FMCW LiDAR v2

FMCW LiDAR 시스템을 Windows와 Jetson에서 함께 운용하기 위한 프로젝트입니다. v2는 기존 측정/파일 규약을 유지하면서 종료·기록·재생 신뢰성과 실제 runtime 검증을 개선합니다.

## Replay Worktree

`codex/pcd-replay-only`: saved point-cloud replay and 3D viewing, without model
weights, object inference, or detection boxes. Existing LiDAR functions remain.
This worktree is intentionally separate from CenterPoint development.
See [Replay Worktree](docs/pcd_replay_worktree.md) for scope and verification.

## Current Direction

- UI: Qt 6.2 이상
- Platforms: Windows, Jetson/Linux
- Digitizer: AlazarTech, Windows/Jetson 공통 adapter
- CPU FFT: FFTW
- GPU FFT: CUDA/cuFFT
- Raw storage: high-speed binary + JSON metadata
- 3D viewer: Qt/OpenGL GPU VBO point-sprite renderer with bounded temporal fusion and edge-aware Y interpolation
- Live Time/FFT: one operator-selected A-scan from each DMA buffer
- UDP: asynchronous versioned raster point-frame sender
- Point axes: ROS/RViz convention (`+X` forward, `+Y` left, `+Z` up)
- ROS1 viewer: C++ ROS Noetic catkin workspace under `Ros_project`

## Key Documents

- `docs/runtime_contract_v2.md`: 현재 코드 읽는 순서, 스레드/설정/단위의 기준
- `docs/v2_review_completion.md`: R1~R10 구현·검증·미검증 범위

- `docs/requirements.md`: 시스템 요구사항과 Phase 계획
- `docs/gui_runtime_requirements.md`: 승인된 GUI의 page ownership, global command, snapshot/thread 계약
- `docs/folder_structure.md`: 폴더 구조와 파일 배치 기준
- `docs/data_contract.md`: full-period raw frame과 metadata 규칙
- `docs/processing_storage.md`: FFT/peak/B-scan 처리와 binary 저장/replay 계약
- `docs/qt_ui_mvp.md`: Phase 5 Qt 화면, runtime thread 구조, 실행 및 operator flow
- `docs/configuration.md`: YAML profile schema v5, validation, field presentation 및 pending 정책
- `docs/device_protocols.md`: Alazar AutoDMA, MCU UART, EDFA binary protocol
- `docs/alazar_supported_models.md`: 지원하는 12-bit AUX trigger-enable ATS 모델과 모델별 설정
- `deploy/jetson/README_KO.md`: Jetson ARM64 소스 번들, 의존성 점검, 빌드 및 실행 절차
- `docs/hardware_acceptance.md`: Windows/Jetson 실제 장비 검증 절차
- `docs/build_setup.md`: Windows/Jetson 빌드 준비와 외부 SDK 경로
- `docs/phase7_execution_plan.md`: Phase 7 subphase 순서, 완료 조건, audit finding 추적 및 commit/push 기준
- `Ros_project/README.md`: ROS Noetic C++ UDP receiver, RViz, copy/build/run 절차

## Phase Policy

Phase별로 구현 단위를 나누고, 각 Phase가 끝날 때 commit/push한다.

- Phase 0: Legacy inventory and requirement lock
- Phase 1: Build system and core skeleton
- Phase 2: Configuration and system state
- Phase 3: Acquisition and device drivers
- Phase 4: Processing and storage pipeline
- Phase 5: Qt UI MVP
- Phase 6: 3D, UDP, and simulator expansion
- Phase 7: Hardware integration and release hardening

## Windows Application

Run the packaged application by double-clicking:

`build/package/FMCW_LiDAR_v2/FMCW_LiDAR.exe`

Keep the entire package folder together. Check `BUILD_FEATURES.txt` for included backends and source identity. Old package folders are preserved; their executables are not updated by a source-only build.

Create the self-contained Jetson source folder and ZIP on Windows with:

```powershell
.\deploy\jetson\export_source.ps1 -Destination build/package/FMCW_LiDAR_v2_Jetson_Source
```

After copying `build/package/FMCW_LiDAR_v2_Jetson_Source` to the Jetson, edit
`deploy/jetson/jetson.env` and run:

```bash
bash deploy/jetson/build.sh
```

The Jetson application is CUDA/cuFFT-only; FFTW remains available for the Windows CPU build.

The Scan / MCU page defaults to the packaged legacy X/Y/M waveform at
`config/waveforms/mems_xym_100ksps.txt`. Operators can select another X/Y/M file in the GUI;
the application converts it to the fixed 100 kS/s MCU rate, validates the 15,000-point limit,
and checks that its B-trigger edge count matches `B-scans / frame` before upload.
`B-trigger offset` fine-tunes only the uploaded M markers: negative values advance and positive
values delay the marker in 10 us MCU-sample steps, while zero preserves the source file timing.
The emitted M edges remain the coordinate anchors. `Bidirectional vector scan` is an operator
setting: OFF keeps every B-scan in acquisition order, while ON reverses the spatial `x_index` of
odd B-scan lines. Source X/Y commands and each A-scan's time samples remain unchanged.
