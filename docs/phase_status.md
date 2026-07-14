# Phase Status

Each phase records the delivered scope and its verification point.

## Phase 0: Legacy Inventory and Requirement Lock

Status: done

- Legacy PC/Jetson code, MCU firmware, and EDFA vendor material are stored under `legacy/`.
- Requirements, decisions, and the simplified folder structure are documented.

## Phase 1: Build System and Core Skeleton

Status: done

- The root CMake project and Windows/Jetson presets select one platform application at a time.
- Full-period raw frame, trigger, scan, optical-state, and revision contracts are defined.
- Digitizer, optional EDFA, storage, and operation-state interfaces are separated from the UI.

Verification:

- Windows MSVC, Qt, and `fmcw_core_tests` passed.
- The Windows Qt shell passed the offscreen smoke test.
- Jetson SDK and hardware acceptance remain target-side work.

## Phase 2: Configuration and System State

Status: done

- Typed `SystemConfig` covers digitizer, laser, optional EDFA/MCU, scan, chirp segmentation, processing, UDP, storage, UI, and calibration.
- Configuration schema version 4 keeps independent peak detection and defines one frame as records-per-buffer A-scans times operator-selected B-scans.
- Strict YAML layers reject unknown keys, wrong scalar types, and unsupported hardware combinations.
- Runtime and restart-required changes are tracked separately; invalid or unapplied settings block START.
- Start captures the active configuration snapshot and revision for session metadata.
- Queue overflow transitions through `Stopping` and preserves diagnostic context.

Verification:

- `fmcw_core_tests` and `fmcw_config_tests` passed.
- Profile parsing, validation, pending changes, Start gating, snapshot capture, and overflow behavior are covered.

## Phase 3: Acquisition and Device Drivers

Status: done

- `AcquisitionSession` coordinates optional EDFA warm-up, digitizer arm, MCU trigger start, and reverse-order stop.
- The fake A/B single-channel digitizer produces deterministic UP-triggered full-period frames.
- EDFA `none` and MCU disabled are explicit bypass modes.
- Windows COM and Jetson/Linux tty transports share MCU and CivilLaser EDFA protocol controllers.
- The Alazar NPT AutoDMA adapter is enabled only when the ATS SDK is found.

Verification:

- Core, configuration, and acquisition/device tests passed with the simulator path.
- Vendor packet parsing, optional-device safety, single-channel capture, and telemetry are covered.
- Actual board, MCU, and EDFA acceptance remains hardware-required work.

## Phase 4: Processing and Storage Pipeline

Status: done

- FFTW3f and CUDA/cuFFT implement a common FFT backend contract.
- `SignalProcessor` extracts configured UP/DOWN segments and independently selects the highest threshold-qualified peak in each search range.
- Every A-scan is independent; a below-threshold result is invalid and never carries a previous peak.
- `ProcessingService` uses a bounded worker queue and applies runtime processing changes at frame boundaries.
- Immutable waveform, FFT, scan-line, and X-by-B-scan Z snapshots are published to the UI.
- Raw full-period and processed binary writers use bounded asynchronous storage with JSON sidecars and split-part replay.

Verification:

- `fmcw_phase4_tests` passed FFTW tone detection and the available CUDA/FFTW comparison.
- Independent invalid-peak behavior, runtime updates, B-scan snapshots, writers, replay, callbacks, and overflow are covered.
- Sustained Alazar-to-NVMe throughput remains Phase 7 hardware acceptance work.

## Phase 5: Qt UI MVP

Status: done

- Windows and Jetson use the same Qt Widgets `MainWindow` and `ApplicationController` boundaries.
- Eight operational pages cover Overview, Live View, Digitizer, Laser/EDFA, Scan/MCU, Processing, Storage/UDP, and System Log.
- One global START/STOP controls the digitizer, scanner, optional devices, processing, and storage session.
- Live View owns Time Domain, FFT, Peak Analysis, Distance/Velocity, and B-scan displays without duplicate FFT plots.
- Time Domain and FFT use the operator-selected A-scan record from every DMA buffer, matching the legacy `g_pick_r` behavior; peak, B-scan, UDP, and raw storage still process all records.
- SDK 25.1.0 hardware discovery identifies ATS9371 at fixed System 1 / Board 1; its discrete sample rates and alignment limits drive validation and UI choices.
- Digitizer setup exposes External TTL edge, threshold/code, delay, pre/post-trigger, and timeout details.
- A-scans per B-scan are derived from records per buffer; B-scans per frame are operator-selected; DMA B-scan rate and frame time are measured from buffer completion timestamps.
- MCU upload contains one complete raster frame and emits a marker only at each B-scan boundary.
- Native spin-box arrows are removed and numeric fields use clean right-aligned entry.
- The Windows executable uses the GUI subsystem and does not open a console window.
- Running locks restart-required controls. STOP followed by `Apply Setup` performs configure/reconnect and remains Ready without automatic START.
- Processing exposes only DC removal, peak threshold, and search range as frame-boundary runtime controls.
- Chirp segmentation uses a frozen full-period snapshot with UP, DOWN, and guard overlays.
- Form labels, field spacing, and segmentation controls use consistent alignment across all setup pages.

Verification:

- Windows MSVC Debug build and all four CTest targets passed.
- ATS-SDK adapter compiled and linked against `C:/AlazarTech/ATS-SDK/25.1.0`; `AlazarSysInfo` reported ATS9371, 12-bit, FPGA 35.3.
- Simulator demo and all eight page captures were reviewed at 1480 x 900 without text overlap.
- Runtime Digitizer lock and Processing snapshot captures remain part of the final UI regression check.
- Actual Alazar DMA, serial hardware, and sustained NVMe recording require Phase 7 hardware acceptance.

## Phase 6: UDP and 3D Visualization

Status: done

- Versioned `FMCW` UDP point packets carry raster frame ID, config revision, timestamp, segment indices, and little-endian XYZ/intensity/velocity arrays.
- A bounded asynchronous sender assembles complete raster frames and applies `latest_frame`, `preserve_frames`, or `stop_sending` queue policy without network I/O in the acquisition or processing loop.
- Live View includes a Qt/OpenGL-backed 3D point-cloud tab with color modes, rotate, pan, zoom, reset, point size, accumulation, freeze, and CSV export.
- Point-cloud snapshots publish at completed B-scan line boundaries and clear unfinished rows at each new raster frame.
- Storage / UDP exposes packet version, points per packet, sender queue, backpressure policy, send FPS, packet count, queue use, and dropped-frame telemetry.

Verification:

- Windows MSVC Release build completed with Qt OpenGL/OpenGLWidgets, ATS-SDK, FFTW, and CUDA enabled.
- All five CTest targets passed, including UDP v1 encode/decode and asynchronous localhost send tests.
- Simulator framebuffer validation rendered 576 points with X/Y/Z axes and a 3D ground grid; the UDP setup page was reviewed at 1480 x 900 without overlap.
- The user package was refreshed with the Phase 6 executable and Qt OpenGL runtime DLLs.

## Phase 7: Hardware Integration and Release Hardening

Status: next

- Execution source of truth: [`phase7_execution_plan.md`](phase7_execution_plan.md)
- Current next subphase: 7.1 ADC and configuration correctness
- Audit findings P7-001 through P7-008 must be closed before Phase 7 completion.
- Replace the simulator runtime selection with the compiled Alazar/MCU/EDFA adapters.
- Validate sustained ATS9371 DMA, NVMe raw recording, UDP throughput, and Jetson UI/thermal behavior on hardware.
