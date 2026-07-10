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

Status: done

- Typed `SystemConfig` covers digitizer, laser, optional EDFA, scan, full-period chirp segmentation, processing, UDP, storage, UI, calibration, and optional MCU settings.
- Strict YAML profiles support default, platform, user, and calibration layers with unknown-key and scalar-type rejection.
- Validation blocks unsafe Start requests while retaining actionable field paths and messages.
- Basic/Advanced exposure and runtime/preview/restart-required policies are defined per field.
- Active and pending configuration revisions are managed without changing restart-required hardware settings during acquisition.
- Start captures a JSON configuration snapshot and revision for later session metadata.
- Queue overflow forces `Stopping`, preserves diagnostic context, and completes in `Error` for operator acknowledgement.

Verification:

- Windows preset configured and built with MSVC 19.44, NMake, and Qt 6.11.0.
- `fmcw_core_tests` and `fmcw_config_tests` passed through the Windows CTest preset.
- Strict YAML parsing, layered profiles, validation, Basic/Advanced policies, pending changes, Start gating, snapshot capture, and overflow Stop behavior are covered by tests.
- The Windows Qt shell still passed its offscreen smoke test after linking the Phase 2 core.
- Jetson profile parsing is platform-independent; target-side compiler and hardware verification remain Phase 7 work.

## Phase 3: Acquisition and Device Drivers

Status: pending

## Phase 4: Processing and Storage Pipeline

Status: pending

## Phase 5: Qt UI MVP

Status: pending
