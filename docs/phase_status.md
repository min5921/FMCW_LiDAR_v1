# Phase Status

이 파일은 phase별 구현 단위와 commit 기준을 추적한다.

## Phase 0: Legacy Inventory and Requirement Lock

Status: done

- Legacy PC/Jetson code, MCU firmware, and EDFA vendor material are stored under `legacy/`.
- Requirements and folder structure are documented.

## Phase 1: Build System and Core Skeleton

Status: done

- Root CMake project and `src/CMakeLists.txt` are present.
- Windows and Jetson CMake presets select one platform application at a time.
- Full-period raw frame, trigger, scan, optical-state, and revision metadata contracts are defined.
- Digitizer frame delivery/abort, EDFA safety control, and asynchronous storage boundaries are defined.
- Operation state transitions are explicitly validated and core contract tests are present.
- Build prerequisites are documented.

Verification:

- Windows preset configured and built with MSVC 19.44, NMake, and Qt 6.11.0.
- `fmcw_core_tests` passed through the Windows CTest preset.
- The Windows Qt shell constructed and exited successfully in offscreen smoke-test mode.
- The Jetson preset is host-gated and ready for target-side verification; actual Jetson SDK and hardware acceptance remain Phase 7 work.

## Phase 2: Configuration and System State

Status: pending

## Phase 3: Acquisition and Device Drivers

Status: pending

## Phase 4: Processing and Storage Pipeline

Status: pending

## Phase 5: Qt UI MVP

Status: pending
