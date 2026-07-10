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
- Configuration schema version 2 removes the global UI mode and adds runtime peak tracking controls.
- Strict YAML profiles support default, platform, user, and calibration layers with unknown-key and scalar-type rejection.
- Validation blocks unsafe Start requests while retaining actionable field paths and messages.
- Primary/Detailed presentation and runtime/preview/restart-required policies are defined per field without a global UI mode.
- Active and pending configuration revisions are managed without changing restart-required hardware settings during acquisition.
- Start captures a JSON configuration snapshot and revision for later session metadata.
- Queue overflow forces `Stopping`, preserves diagnostic context, and completes in `Error` for operator acknowledgement.

Verification:

- Windows preset configured and built with MSVC 19.44, NMake, and Qt 6.11.0.
- `fmcw_core_tests` and `fmcw_config_tests` passed through the Windows CTest preset.
- Strict YAML parsing, layered profiles, validation, field presentation policies, pending changes, Start gating, snapshot capture, and overflow Stop behavior are covered by tests.
- The Windows Qt shell still passed its offscreen smoke test after linking the Phase 2 core.
- Jetson profile parsing is platform-independent; target-side compiler and hardware verification remain Phase 7 work.

## Phase 3: Acquisition and Device Drivers

Status: done

- `AcquisitionSession` coordinates optional EDFA warm-up, digitizer arm, MCU trigger start, and reverse stop safety order.
- Core telemetry exposes digitizer frame/DMA status, EDFA bypass/output state, and MCU waveform/scan state.
- Fake A/B single-channel digitizer produces deterministic up-triggered full-period frames with up/down segment metadata.
- Fake EDFA and MCU adapters allow acquisition with no connected hardware; EDFA `none` and MCU disabled are explicit bypass states.
- Windows COM and Jetson/Linux tty transports share MCU line and CivilLaser EDFA binary protocol controllers.
- The Alazar adapter uses NPT AutoDMA and is build-gated by ATS-SDK discovery for `ATSApi.lib` or `libATSApi.so`.
- Hardware acceptance procedures distinguish Windows SDK validation from exact-board Jetson arm64 driver validation.

Verification:

- Windows MSVC/Qt no-SDK build completed with the Alazar adapter disabled and simulator path enabled.
- Existing core/config tests and the new acquisition/device test passed through CTest.
- EDFA packet examples from the vendor document, checksum rejection, MCU ACK/count handling, scripted serial controllers, optional-device safety, channel A/B full-period frames, and telemetry are covered.
- The SDK-enabled Alazar source path compiled against the preserved vendor headers; only vendor-header code-page warnings remained.
- Actual Windows/Jetson board, MCU port, and EDFA optical-output acceptance remain hardware-required checks documented in `hardware_acceptance.md`.

## Phase 4: Processing and Storage Pipeline

Status: done

- FFTW3f and CUDA/cuFFT implement the common real-to-complex FFT backend contract with reusable plans.
- `SignalProcessor` extracts configured up/down segments, applies preprocessing/windowing, detects and tracks peaks, and produces calibrated distance, velocity, and XYZ results.
- Peak tracking supports detected, tracked, reacquired, held-invalid, lost, and stop-requested behavior per scan line.
- `ProcessingService` decouples acquisition enqueue from FFT work with a bounded worker queue and frame-boundary runtime settings.
- Immutable waveform, FFT, scan-line, and X-by-B-scan Z snapshots are published without exposing acquisition buffers to the UI.
- Raw full-period and processed binary writers run through a bounded asynchronous storage service with stop-on-overflow and writer-failure reporting.
- JSON sidecars record session/config identity, stream description, file parts, frame count, completion state, and stop reason.
- Raw replay restores the original frame contract, follows numbered split parts, and feeds the same signal processor used by live acquisition.

Verification:

- Windows MSVC Debug build enabled FFTW3f and CUDA 13.1/cuFFT through external SDK discovery.
- `fmcw_phase4_tests` passed FFTW tone detection and the runtime-available CUDA/FFTW spectrum comparison.
- Full-period UP/DOWN peak processing, hold-last invalid semantics, runtime revision application, scan-line/B-scan snapshots, async raw/processed writing, JSON metadata, split-part replay, processed callbacks, and forced processing/storage overflow are covered.
- All four CTest targets passed and the Qt offscreen smoke test remained successful.
- Sustained Alazar-to-NVMe throughput and long-run thermal/drop behavior remain hardware acceptance work for Phase 7.

## Phase 5: Qt UI MVP

Status: pending
