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
- Configuration schema version 5 fixes External TTL triggering and limits Laser Specification to distance bandwidth and sweep rate in Hz.
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
- Every A-scan is independent; a peak that does not exceed threshold is invalid, uses `NaN` for floating-point peak/measurement fields, and never carries a previous value.
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
- Digitizer setup exposes the fixed External TTL contract, edge, delay, pre/post-trigger, and timeout details without an analog threshold control.
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

Status: in progress

- Execution source of truth: [`phase7_execution_plan.md`](phase7_execution_plan.md)
- Phase 7.1 ADC and configuration correctness is complete.
- Current next subphase: 7.2 Hardware runtime and DMA batch
- Audit findings P7-001 through P7-008 must be closed before Phase 7 completion.
- Replace the simulator runtime selection with the compiled Alazar/MCU/EDFA adapters.
- Validate sustained ATS9371 DMA, NVMe raw recording, UDP throughput, and Jetson UI/thermal behavior on hardware.

Phase 7.1 verification:

- ATS9371 12-bit left-aligned DMA samples now use the SDK shift rule and signed full-scale conversion.
- Minimum, midpoint, maximum, padding-bit, 16-bit endpoint, invalid-width, and all 4096 ATS9371 code tests pass.
- Code defaults, layered Windows simulator profile, and Jetson profile validate with zero errors; the intentional 4096-sample record margin is shown as a non-blocking warning.
- Full-period timing is defined by sampling rate and `chirp_period_samples`; actual timing remains a Phase 7.5 measurement input.
- Laser Specification exposes only measured bandwidth and full triangular sweep rate in `Hz` for distance conversion.
- Raw format v1 remains converted signed `int16`; original DMA `uint16` block storage is reserved for version 2 in Phase 7.4.
- Windows MSVC Release built with ATS-SDK, FFTW, and CUDA/cuFFT enabled.
- All five CTest targets passed.
- Implementation commit: `82b8369`

Phase 7.2 software verification:

- Runtime source selection is available in the Digitizer UI and YAML profile for Simulator, Alazar ATS9371, and Raw Replay.
- The application runtime factory now selects real Alazar/MCU/EDFA adapters for hardware operation.
- A dedicated continuous acquisition thread consumes one complete pool-backed DMA batch at a time; the UI timer no longer polls records.
- Alazar repost occurs immediately after all records are copied from the completed SDK buffer.
- Processing queue capacity is batch based, while raw format v1 storage safely retains aliasing per-record references.
- Release CTest passed 5/5, including batch lifetime, finite worker shutdown, adapter factory, replay batch, and processing batch tests.
- The global STOP regression test fixes the safety order as MCU trigger off, digitizer abort/stop, then controlled EDFA output off.
- The packaged GUI simulator delivered 141 DMA batches / 9,024 records with queue 0/32 and no DMA drop or trigger miss.
- `build/package/FMCW_LiDAR/FMCW_LiDAR.exe` matches the Release build SHA-256 `7ADB64661441A87C748D6D7B8D48EF36204588DB8791766D5895A08303DB2BB6`.
- ATS9371 was not connected, so the 10-minute hardware acceptance remains pending and Phase 7.2 stays in progress.
- Software implementation commit: `3cfcea3`

Record-length policy refinement (2026-07-15):

- `sample_point` is now an operator-selected Alazar record length and is never derived from optical sweep rate.
- ATS9371 validation enforces minimum 256 samples, 128-sample record/pre-trigger alignment, 8176-sample maximum NPT pre-trigger, at least 64 post-trigger samples, and 16-sample single-channel trigger-delay alignment.
- A record longer than configured `chirp_period_samples` produces a visible non-blocking warning; segment or full-period data outside the record remains an error.
- The Digitizer page shows ATS validity, record duration, and excess time beyond one configured chirp period next to the editable record count.
- Windows MSVC Release and CTest 5/5 passed. The warning-state simulator START delivered 141 DMA batches / 9,024 records with queue 0/32 and no DMA drop or trigger miss.
- Packaged EXE SHA-256: `B14C0CD37954367988305F8349F8BF9FDCF562FB297F2C8CB2566975EAACAA25`.
- Implementation commit: `4fc5a1e`

TTL and laser distance contract refinement (2026-07-15):

- ATS9371 acquisition keeps `TRIG_EXTERNAL` and `AlazarSetExternalTrigger(..., ETR_TTL)`; the required SDK level argument retains the legacy fixed code 150.
- The Digitizer UI no longer exposes an analog full-scale trigger threshold or threshold code.
- Laser Specification now contains only `sweep_bandwidth_hz` and full triangular `sweep_rate_hz`, both entered in Hz.
- Distance uses `c * (f_up + f_down) / (8 * bandwidth * sweep_rate)`; the legacy hard-coded 200000 Hz value is replaced by the configured sweep rate.
- Chirp timing, simulator/replay pacing, record-margin warning, and raw throughput estimation use sampling rate plus `chirp_period_samples`, independently of Laser Specification.
- Laser period/slope consistency warnings were removed. The intentional record-margin warning remains the only default warning.
- Configuration schema version is now 5; velocity wavelength is owned by calibration instead of the Laser UI.
- Windows MSVC Release and CTest 5/5 passed. Digitizer and Laser pages were visually checked in the packaged GUI.
- Packaged EXE SHA-256: `742DC9FE18BF0AA5B16CA72CC0FC70D8E279FA350ECE8601EEDD66C3E4275A40`.
- Implementation commit: `315f374`

Peak-threshold NaN contract refinement (2026-07-15):

- A peak candidate must strictly exceed `peak_threshold_db`; equality does not pass detection.
- A rejected peak keeps integer `discrete_bin = -1` and exposes `NaN` for interpolated bin and magnitude.
- If either UP or DOWN peak is rejected, measurement validity is false and distance, velocity, intensity, and XYZ remain `NaN`.
- Scan-line, B-scan, point-cloud, and processed binary paths preserve invalid values instead of converting them to zero.
- Plot widgets skip non-finite samples, so an invalid A-scan appears as missing data rather than a false measurement.
- Windows MSVC Release, CTest 5/5, and the packaged EXE smoke test passed.
- Packaged EXE SHA-256: `BB4F66D48B3DFEDA3CEC0BBB5B5F2DB7D9067A0F5368493D1BF426A2E35043F5`.
- Implementation commit: `b482cad`

Signal-processing performance acceptance decision (2026-07-15):

- The qualification workload is laser 200 kHz, digitizer 1 GS/s, 4992 samples/record, 998 records/DMA buffer, and 2048-sample UP plus 2048-sample DOWN segments.
- One B-scan line requires 1996 length-2048 FFTs and produces exactly 998 peak pairs and points.
- `998 / 200000 = 4.99 ms`; the hard processing deadline is the 200 Hz B-scan period of 5.00 ms.
- Timing starts at DMA completion and ends when all 998 measurements and the B-scan line snapshot are complete.
- FFTW and CUDA must each sustain at least 10 minutes with no batch over 5.00 ms, no deadline miss, no drop, no stale result, and no growing processing queue.
- Disk, UDP, and Qt paint have separate subsystem acceptance and are not included in the 5 ms signal-processing timer.
- Phase 7.3 is now explicitly split into baseline, FFTW optimization, CUDA optimization, and 200 Hz performance acceptance. Implementation remains pending.
- Decision commit: `abc50aa`
