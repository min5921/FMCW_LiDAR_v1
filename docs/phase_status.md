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
- Immutable waveform, FFT, scan-line, and X-by-B-scan forward-depth snapshots are published to the UI.
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
- B-scan snapshots publish at completed line boundaries; point-cloud snapshots publish only for complete raster frames and remain unchanged while the next frame is assembled.
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
- Software implemented through 7.4; next planned subphase is 7.5 after hardware timing inputs
- Audit findings P7-001 through P7-008 must be closed before Phase 7 completion.
- Replace the simulator runtime selection with the compiled Alazar/MCU/EDFA adapters.
- Validate sustained ATS9371 DMA, NVMe raw recording, UDP throughput, and Jetson UI/thermal behavior on hardware.

Phase 7.1 verification:

- ATS9371 12-bit left-aligned DMA samples now use the SDK shift rule and signed full-scale conversion.
- Minimum, midpoint, maximum, padding-bit, 16-bit endpoint, invalid-width, and all 4096 ATS9371 code tests pass.
- Code defaults, layered Windows simulator profile, and Jetson profile validate with zero errors and no duplicate period-length warning.
- Captured full-period timing is derived from Digitizer sample rate and sample point; actual trigger timing remains a Phase 7.5 measurement input.
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
- Digitizer sample point is the single captured full-period length; segment data outside the record remains an error.
- The Digitizer page shows ATS validity and record duration next to the editable record count without a duplicate period-length comparison.
- Windows MSVC Release and CTest 5/5 passed. The warning-state simulator START delivered 141 DMA batches / 9,024 records with queue 0/32 and no DMA drop or trigger miss.
- Packaged EXE SHA-256: `B14C0CD37954367988305F8349F8BF9FDCF562FB297F2C8CB2566975EAACAA25`.
- Implementation commit: `4fc5a1e`

TTL and laser distance contract refinement (2026-07-15):

- ATS9371 acquisition keeps `TRIG_EXTERNAL` and `AlazarSetExternalTrigger(..., ETR_TTL)`; the required SDK level argument retains the legacy fixed code 150.
- The Digitizer UI no longer exposes an analog full-scale trigger threshold or threshold code.
- Laser Specification now contains only `sweep_bandwidth_hz` and full triangular `sweep_rate_hz`, both entered in Hz.
- Distance uses `c * (f_up + f_down) / (8 * bandwidth * sweep_rate)`; the legacy hard-coded 200000 Hz value is replaced by the configured sweep rate.
- Simulator/replay pacing and raw throughput estimation use Digitizer sample rate plus sample point, independently of Laser Specification.
- Laser period/slope consistency and duplicate record-margin warnings are removed.
- Configuration schema version is now 5; velocity wavelength is owned by calibration instead of the Laser UI.
- Windows MSVC Release and CTest 5/5 passed. Digitizer and Laser pages were visually checked in the packaged GUI.
- Packaged EXE SHA-256: `742DC9FE18BF0AA5B16CA72CC0FC70D8E279FA350ECE8601EEDD66C3E4275A40`.
- Implementation commit: `315f374`

Peak-threshold NaN contract refinement (2026-07-15):

- A peak candidate must strictly exceed `peak_threshold_db`; equality does not pass detection.
- A rejected peak keeps integer `discrete_bin = -1` and exposes `NaN` for `peak_bin` and magnitude.
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

Backend-equivalent processing and integer-peak refinement (2026-07-15):

- FFTW and CUDA/cuFFT now have an explicit common algorithm contract; backend selection changes the execution processor, not the signal-processing stages or equations.
- ADC conversion, segmentation, DC removal, polarity, window, zero padding, dBFS scaling, strict threshold, peak selection, distance/velocity, calibration, XYZ, and validity semantics are backend invariant.
- Parabolic interpolation was removed. A valid `peak_bin` is exactly the maximum threshold-qualified `discrete_bin`; invalid values remain `NaN` and `-1`.
- The processed binary layout keeps the same float slot but its source field is now named `peak_bin` and carries no fractional estimate.
- A runtime CUDA test on the local RTX compares the complete FFTW/CUDA processed result, including validity, integer peak bins, magnitude, distance, velocity, XYZ, and below-threshold `NaN` behavior.
- Windows MSVC Release, CTest 5/5, the explicit FFTW/CUDA processing parity test, and the packaged EXE smoke test passed.
- Packaged EXE SHA-256: `9D76895B37084DE83C846D82697C208A0B785C8D0255EAEADBCFA7EDAF3CDDD1`.
- Implementation commit: `0181afb`

ATS record-length and legacy XYZIV contract audit (2026-07-15):

- ATS9371 record length follows ATS-SDK 25.1.0 section 7.2: minimum 256 samples and exact 128-sample resolution. The Qt control only commits supported values; 4992 is accepted and unsupported 5000 is rejected without remaining in the profile.
- Peak search remains strict-threshold, integer-bin processing with no interpolation. Invalid UP or DOWN detection propagates `NaN` through distance, velocity, intensity, and XYZ.
- Distance and velocity equations were checked algebraically against the legacy CUDA pipeline while retaining the configured sweep rate instead of the legacy hard-coded 200 kHz value.
- Cartesian conversion restores the legacy contract: X is lateral, Y is forward range, and Z is vertical. Configured X/Y angle offsets are applied before conversion.
- The B-scan stores forward depth from `point.y`; the 3D viewer, processed storage, and UDP packet all use the same finite `x, y, z, intensity, velocity` output contract.
- Windows MSVC Release and CTest 5/5 passed, including the explicit local CUDA/FFTW full-result parity test. The packaged EXE smoke test passed.
- Packaged GUI verification confirmed 4992 acceptance, 5000 input rejection, live B-scan depth output, and a nonblank XYZ point cloud with intensity, velocity, and distance color modes.
- Packaged EXE SHA-256: `EB67F68B958C12FF6EE465715224C11DAE1C24385CFE1341B4B2F75053352C06`.

Complete-frame point-cloud publication refinement (2026-07-15):

- B-scan continues to publish every completed scan line, but the 3D point cloud now publishes only after all configured raster lines are complete.
- During assembly of the next raster, the last complete immutable point-cloud frame remains visible; partial rows never replace it.
- The 3D widget rejects incomplete and duplicate snapshot pointers, preventing both line-by-line unfolding and repeated Accumulate insertion of the same frame.
- Raster mapping remains legacy compatible: record index advances X within a B-scan and B-scan line index advances the Y scanner angle. The completed cloud may therefore span vertical Z according to the configured Y field of view.
- Release CTest passed 5/5, including incomplete/complete/next-frame boundary assertions and the explicit CUDA/FFTW processing parity test.
- Packaged GUI verification showed `Waiting for complete raster frame` at DMA 1 and switched only to `Frame 25 complete | 1600 points` after a full 64 x 25 raster was available.
- Packaged EXE smoke test passed. SHA-256: `17E75E786E61F62838073B489F8C500C1975FDFE4CF58DFDBC0EE31B938F7021`.

Phase 7.3A strict DMA simulation and processing baseline (2026-07-15):

- The Windows EXE now starts with the ATS9371 qualification simulator profile: 1 GS/s, 4992 samples/record, 998 records/buffer, 2048 UP, 2048 DOWN, and 200 kHz full-period trigger rate.
- Strict simulator mode produces one DMA completion every 4.99 ms into an eight-buffer ring. It never retimes to consumer speed; an unserviced ring reports one DMA drop, latches overflow, and follows the same acquisition error/STOP route as the Alazar adapter.
- DMA completion, ownership-copy completion, and B-scan line completion timestamps drive copy, signal, and end-to-end batch telemetry. The Processing page reports last, p50, p95, p99, maximum, margin, and deadline misses.
- The deterministic Release baseline produced 998 records, 1996 FFTs, 998 valid XYZIV values, and one complete B-scan line in 28.1651 ms. This correctly fails the 5.00 ms performance gate and establishes the Phase 7.3B comparison point.
- The baseline still makes 1996 synchronous FFT calls and steady-state record-level temporary vector allocations; Phase 7.3B replaces these with one preallocated FFTW plan-many batch path.
- Windows MSVC Release and CTest 6/6 passed, including exact qualification payload/cadence, bounded overflow, timing telemetry, and the full 998-record processing baseline.
- Packaged EXE smoke test passed. SHA-256: `BE628C72E69EFEF613BE349B97A9446368793DB6651D5A5B5D1AF993F0948B23`.

Phase 7.3B FFTW batch processing optimization (2026-07-15):

- `SignalProcessor::processBatch` replaces the 998 repeated record calls with fixed 64-record chunks containing 128 interleaved UP/DOWN transforms.
- FFTW uses up to 16 independent OpenMP execution lanes with persistent plans and workspaces. The strict 998-record workload now performs 16 FFT batch executions instead of 1,996 synchronous FFT calls.
- Preprocessing, strict integer peak detection, distance, velocity, calibration, XYZIV, and invalid `NaN` propagation remain common with the single-record and CUDA contracts.
- Only the operator-selected record copies complete UP/DOWN spectra; all records continue to generate B-scan and point-cloud results.
- Historical 2026-07-15 tests confirmed batch/single parity and exactly 998 outputs. After three warm-up batches, four independent 32-batch Release runs measured 2.46-3.11 ms average inside the batch processor, down from the 28.1651 ms baseline. The later no-warmup procedure supersedes this measurement method.
- End-to-end p50 was 2.41-2.98 ms, but maximum latency varied from 4.46 to 8.29 ms and deadline misses ranged from 0 to 2. Phase 7.3D therefore remains pending and no sustained 5 ms real-time pass is claimed.
- Windows MSVC Release and CTest passed 6/6. The packaged EXE smoke test passed with SHA-256 `D1D6A6E75EC5C41DADADE1C1C37512B829F3286529EAD7C0061803C42C02AEB4`.

Phase 7.3C full CUDA signal pipeline (2026-07-15):

- A dedicated `CudaSignalPipeline` now executes signed-int16 conversion, DC removal, full-period UP/DOWN segmentation, polarity, windowing, 1,996 cuFFTs, strict integer peak search, distance, velocity, calibration, and XYZIV on the GPU.
- Persistent pinned host staging and persistent device workspaces replace per-call CUDA allocations. Only compact peak/measurement results and the selected UP/DOWN spectra return to the host.
- The implementation follows the useful structure of legacy `gpu_strea.cu` while replacing its alternating-record, unsigned input, unnormalized dB, non-strict threshold, and old Cartesian assumptions with the current shared FFTW/CUDA contract.
- Full-batch parity passes for validity, integer bins, signal dBFS, distance, velocity, XYZIV, selected spectra, and invalid `NaN` propagation.
- The historical 2026-07-15 strict CUDA benchmark processed 32 measured batches and 31,936 valid XYZIV outputs after warm-up. Local RTX p50 remained about 6-7 ms and missed the 5 ms deadline. The later no-warmup result supersedes this measurement method, and no Phase 7.3D performance pass is claimed.
- Nsight Systems showed about 0.063 ms of GPU kernels per batch; the main limits are full-period H2D transfer, WDDM scheduling, and gathering 998 separate record vectors into pinned staging.
- Windows MSVC Release and CTest passed 7/7. The packaged EXE smoke test passed with SHA-256 `2C03B0FDE71B703CFFCBC86195AFDEA6A005A343ABB0AE91AD06F9252FE824B2`. Jetson runtime parity and sustained performance remain future hardware acceptance items.

Phase 7.3D strict real-time qualification harness (2026-07-15):

- Added an option-free two-second probe and a separate fixed ten-minute-per-backend acceptance EXE. Both use the production acquisition session, continuous worker, bounded processing queue, FFTW/CUDA pipelines, B-scan aggregation, and STOP path.
- FFTW processed 401 strict 4992 x 998 batches in two seconds with 400,198 valid XYZIV results, queue high-water 3/32, and zero DMA drops. End-to-end p50 was 10.712 ms, maximum was 22.065 ms, and 400 batches missed 5 ms.
- CUDA processed 158 strict batches and 157,684 valid XYZIV results before the queue reached 32/32. The next batch was rejected and the acquisition worker stopped with `Processing queue capacity exceeded`; no silent drop occurred.
- Payload shape, exact 998-result batch accounting, B-scan publication, and clean/overflow STOP routes passed. The probe reports these functional checks separately from the real-time hard result.
- The fixed 600-second acceptance executable has not been run. Both local backends currently fail the 5 ms requirement and Jetson evidence is unavailable, so Phase 7.3D remains blocked and no Phase 7.3 completion is claimed.

Phase 7.4 contiguous DMA-block storage (2026-07-15):

- `RawFrameBatch` owns one contiguous signed `int16` payload and record views. Fake/ATS acquisition fills it directly, CUDA staging copies it once, and raw recording receives one block enqueue instead of 998 frame enqueues.
- Raw and processed streams now use independent bounded queues and worker threads. Automated blocking-writer coverage proves processed output continues while raw I/O is stalled, and raw overflow retains a raw-specific STOP reason.
- Raw v2 uses a compact metadata table plus one contiguous payload write per block, whole-block split rotation, preallocation/truncation, and a two-block free-space preflight. Raw v1 remains readable.
- Two 4992 x 998 blocks were written across split parts and replayed through the runtime with exact samples, raster positions, FFTW peaks, distance, velocity, and XYZ parity. Release CTest passed 9/9. The packaged EXE smoke test passed with SHA-256 `E759122932CC8BDC4F228DFD0753970E20A51933C42F9F235D533F4A6C52B02E`.
- Four short 0.319 GB probes measured 3.24-4.66 GB/s, exceeding the 2.596 GB/s short-probe threshold. The result includes Windows cache and is not a ten-minute NVMe claim.
- The fixed ten-minute acceptance requires 120,240 blocks and approximately 1.198 TB. It was not run. Phase 7.4 hardware acceptance remains blocked until target NVMe evidence shows zero queue growth, overflow, missing block, and corrupt split part.
- After contiguous ownership, one two-second CUDA probe processed 313 batches before queue overflow versus 158 before the change; another completed 401 batches but accumulated queue high-water 14/32 and 39.70 ms p50. FFTW p50 remained about 10.87 ms. Both violate the no-growth and 5 ms gates, so Phase 7.3D remains blocked.

ATS DMA and single-slot CUDA event refinement (2026-07-16):

- ATS9371 acquisition now exposes native left-aligned 12-bit DMA memory as an external contiguous batch. The acquisition thread no longer converts or duplicates every sample before CUDA submission.
- `SampleFormat::UnsignedOffsetBinary12LeftAligned` is preserved through processing snapshots, raw v2 storage, and replay. FFTW and CUDA use the same explicit conversion rule and full pipeline contract.
- `CudaSignalPipeline` remains fixed at one slot. Submit enqueues H2D, segmentation, 1,996 cuFFTs, peak/XYZIV work, compact D2H, and a completion event without a per-batch `cudaStreamSynchronize`.
- A separate H2D event releases the original ATS DMA lease before full processing completion. A pooled result batch keeps metadata and only the selected raw record, avoiding a 4992 x 998 payload copy while preserving selected Time Domain and FFT publication.
- CUDA module loading is selected internally and the target is built for the active native GPU architecture. There is no production or qualification warm-up batch; performance tests measure from the first submitted batch after normal configure/start.
- The explicit CUDA lifetime test proves that the external DMA owner expires after the H2D event while the single slot remains in flight, then all results are collected by the completion event. Release CTest passes 9/9.
- The packaged Windows GUI was refreshed from the final Release build and its hidden `--smoke-test` exited with code 0. Build and package SHA-256 both equal `4FCE36B2FFABF7E4A27080675CB2290F38C4C258965613556CA8EE985469A4C6`.
- The final no-warmup 32-batch local run measured FFTW p50 2.3285 ms, p95 2.7824 ms, maximum 5.35 ms, and 1 deadline miss. CUDA measured p50 10.0609 ms, p95 10.4379 ms, maximum 10.5553 ms, and 32 deadline misses.
- Repeated two-second strict probes completed about 398-400 FFTW batches without DMA drop but still missed the end-to-end deadline. CUDA could not sustain the 4.99 ms producer cadence with one slot; repeated runs varied from 27 to 354 delivered batches before the eight-buffer simulated DMA ring overflowed. The STOP route and exact 998-result accounting passed; the 5 ms gate did not.
- ATS9371 hardware was not connected for this verification. Phase 7.3D and hardware acceptance remain pending rather than complete.

Windows latency optimization and sustained acceptance (2026-07-17):

- CUDA event polling was replaced by direct event waits for the fixed single slot. Batch snapshot publication uses one lock and excludes optional disk/UDP callbacks from the signal-processing deadline. The strict simulator now uses a high-resolution Windows timer.
- FFTW uses a fixed 16-worker OpenMP team, keeps the measured 64-record chunk, and combines segment initialization and windowing to remove redundant full-buffer passes. FFTW/CUDA parity, strict threshold behavior, integer bins, `NaN` propagation, distance, velocity, and XYZIV remained unchanged.
- Direct no-warmup results were FFTW p50 1.460-1.686 ms across ten 32-batch runs with zero misses, and CUDA p50 0.514 ms with 0.948 ms maximum and zero misses. The previous roughly 10 ms CUDA median was host timer polling, not cuFFT execution.
- The fixed ten-minute-per-backend acceptance processed 120,242 batches and 120,001,516 valid XYZIV results per backend with no DMA drop or rejection. FFTW had 3 deadline misses with 5.763 ms maximum; CUDA had 17 misses with 8.497 ms maximum. Both retained functional PASS and sustained throughput but correctly reported HARD_FAIL.
- The final short stress after the preprocessing change reduced normal FFTW latency to roughly 2.0 ms and CUDA to roughly 1.0 ms. Ten strict runs still exposed one FFTW signal-scheduling outlier and one CUDA simulator-ownership outlier above 5 ms, so the hard gate remains open.
- CPU-set pinning was evaluated and removed after it worsened both ownership and signal latency on the local Core Ultra 9 285K. The retained policy raises only critical acquisition/processing threads and OpenMP workers; the process and Qt UI remain at normal priority.
- Release CTest remains 9/9. Actual ATS9371 trigger/DMA hardware and Jetson were not connected, so Phase 7.3D is not marked complete.
- The refreshed packaged GUI passed its hidden smoke test. Release and package SHA-256 both equal `5BD1213A5A51DA5A39BFEB217D64262B428DE6140A3CA1E67DB6F45C12E48405`.

Post-reboot idle-machine acceptance rerun (2026-07-17):

- Ten direct runs completed with zero misses. FFTW p50 was 1.530-1.819 ms with 4.954 ms maximum; CUDA p50 was 0.490-0.543 ms with 1.122 ms maximum.
- Ten short strict runs gave FFTW HARD_PASS 9/10 and CUDA HARD_PASS 10/10. Every run preserved all 998-record results with queue high-water 1 and zero DMA drops or rejected batches.
- The 600-second-per-backend rerun processed 120,241 batches and 120,000,518 valid XYZIV records per backend. FFTW had 5 misses and 7.481 ms maximum; CUDA had 8 misses and 6.476 ms maximum. Functional and sustained-throughput checks passed, but both hard gates failed.
- Signal-processing maxima stayed below 5 ms at 4.197 ms for FFTW and 4.640 ms for CUDA. The maxima above the deadline occurred in simulator ownership/materialization, which reached 5.873 ms for FFTW and 5.795 ms for CUDA.
- Rebooting removed the former external training load and improved the CUDA long-run tail, but did not eliminate Windows/simulator outliers. Phase 7.3D remains pending until the ATS9371 path provides matching completion-to-ownership telemetry.

Ownership-stage instrumentation and descriptor reuse (2026-07-17):

- Runtime telemetry now reports arithmetic mean and maximum latency for acquisition wakeup, digitizer materialization, session validation, enqueue dispatch, queue wait, and processing compute. The timestamps are memory-only and preserve the raw v2 storage contract.
- Fake and ATS adapters reuse prebuilt metadata templates for all 998 records. Release CTest passed 9/9; ten direct runs had zero misses with FFTW mean 1.648-1.922 ms and CUDA mean 0.517-0.611 ms.
- Ten strict two-second runs produced FFTW HARD_PASS 9/10 and CUDA HARD_PASS 10/10. Descriptor materialization averaged 0.026-0.040 ms and session validation about 0.004 ms, excluding both from the primary bottleneck set.
- The 600-second run processed 120,241 FFTW and 120,242 CUDA batches with exact XYZIV accounting, zero drops/rejections, and queue high-water 1/2. FFTW had 6 misses and 6.944 ms maximum; CUDA had 38 misses and 8.128 ms maximum.
- FFTW's largest remaining tail was compute at 6.372 ms. CUDA's largest was simulator acquisition wakeup at 6.203 ms; materialization stayed below 0.334 ms. Sustained 200 Hz throughput passes, but the 5 ms hard gate and ATS9371 hardware acceptance remain pending.
- The refreshed packaged GUI passed `--smoke-test`; Release and package SHA-256 are `19C0938C8998500EA3788A33B0B0C1E7DCA0A90F1B927712472136EFAE47F81C`.

Live-view and segmentation usability refinement (2026-07-17):

- Chirp segmentation controls accept period start and UP/DOWN start/length values. Full-period length comes directly from Digitizer sample point, while segment values use the existing half-open `[start, end)` processing contract with overflow validation.
- Only the visible Live tab is delivered to the Qt GUI thread. Acquisition, all 998-record FFT/peak/distance processing, B-scan assembly, storage, and UDP remain active while hidden plot conversion and repaint work is skipped.
- Windows plot repaint is capped at 60 Hz, Jetson defaults to 30 Hz, and full status aggregation remains at 10 Hz. Line plots share immutable snapshot vectors instead of copying them into Qt containers; dense finite traces retain the per-pixel min/max envelope and threshold-invalid `NaN` gaps.
- Live View now reports measured DMA, selected snapshot delivery, and completed data-paint rates plus omitted DMA sequences, GUI-merged updates, and set/paint p95/max latency in a non-I/O 1-second diagnostic window.
- Selected A-scan can be changed with a synchronized horizontal slider or numeric control. Slider dragging applies the runtime selection once on release and does not restart or reconfigure acquisition.
- The digitizer simulator precomputes deterministic DMA-slot-varying templates with approximately 96 ADC-count RMS Gaussian-like noise. The runtime path does not generate random samples, preserving qualification cadence and DMA-overflow behavior.
- Windows MSVC Release CTest passed 9/9, and the refreshed packaged EXE passed `--smoke-test`. Release and package SHA-256 are `C71D59952652858E73139FD92B36BC84F95EAB545BC994FB0CB505A390D29938`.
- No new real-time acceptance claim is made from this run because an external model-training workload was active. The preceding idle-machine and sustained acceptance records remain the current performance evidence.

ATS trigger-delay and live-plot validation (2026-07-23):

- Trigger delay now defaults to `0 samples` in the C++ defaults and YAML profiles. The packaged Digitizer page was verified with 4992 post-trigger samples and zero pre-trigger samples.
- Windows line-plot delivery runs at up to 60 Hz, Jetson defaults to 30 Hz, and immutable waveform/FFT vectors are shared directly with the visible plot. Release Time Domain and FFT renders passed with the 4992 x 998 qualification simulator.
- An actual ATS9371 run with 200 kHz TRIG IN, 200 Hz AUX trigger-enable, 4992 samples, and 998 records/buffer measured approximately 139.08 B-scans/s and 7.190 ms/buffer with zero DMA drops. The same rate remained when Live View was hidden, so GUI plotting is not limiting DMA cadence.
- Removing the former 400-sample delay improved the earlier approximately 100 B-scans/s result but did not reach the 200 B-scans/s target. External-trigger acceptance and the NPT record-length/re-arm margin remain a separate hardware-timing investigation.
- The Windows MSVC Release build, Release CTest 9/9, package smoke test, and automated Time Domain/FFT/Digitizer renders passed. Package SHA-256 is `516804899E19047934E2006D529808C41325569441EA67857C178422FFF3831A`.

ATS9371 200 Hz acquisition and Qt paint verification (2026-07-23):

- The external trigger/arm issue behind the earlier 139.08 Hz result was corrected. A short real-board run then sustained 200.0 DMA buffers/s at 1 GS/s, 4736 samples/record, 998 records/buffer, eight DMA buffers, TTL rising edge, and trigger delay 0.
- Full-period segmentation used UP start/length 100/2048 and DOWN start/length 2500/2048 with guard 10. The CPU FFTW path averaged about 1.8 ms, with p95 2.17 ms, maximum 3.853 ms, and no observed deadline miss.
- Live View publishes only the latest selected A-scan. Time Domain and FFT each sustained about 59 Hz while acquisition remained at 200 Hz; intentionally omitted display sequences reflect the 60 Hz Windows UI cap rather than DMA or processing loss.
- Dense Time Domain drawing now uses a per-screen-column min/max envelope and one batched line draw. Sparse FFT drawing uses a batched integer-point polyline. Immutable waveform vectors are shared through the GUI path without full-vector copies.
- On the real data, Time Domain paint improved from p95/max 481.11/481.11 ms to 1.85/2.12 ms. FFT paint improved from 53.53/53.53 ms to 1.58/1.60 ms. GUI and paint coalescing remained 0.0/s.
- A 2048-point real-to-complex transform retains Nyquist index 1024 internally. Live FFT, peak search, and UI limits use 0 through 1023, preventing the terminal Nyquist spike from entering measurement.
- Operator STOP completed and returned to START; the digitizer disconnected cleanly. EDFA, MCU, raw recording, and UDP were intentionally disabled during this focused check.
- Release CTest passed 9/9. The packaged EXE SHA-256 is `53DE2B92C5FA8AA5E2EA73C379D0CD3057E410AC69F487656E4834FBDECE41F0`.
- This is short functional evidence, not the fixed 10-minute Phase 7.2/7.3D/7.4 hardware acceptance. Long-duration DMA, locked-page/handle, storage, thermal, and Jetson checks remain pending.

Alazar 12-bit AUX trigger-enable model expansion (2026-07-23):

- ATS-SDK 25.1.0 support is intentionally limited to ATS9120, ATS9130,
  ATS9350/51/52/53, ATS9360/62/64, ATS9371, and ATS9373. All eleven use the
  native left-aligned 12-bit DMA path and have an SDK `NPT_Scan` example with
  `AUX_IN_TRIGGER_ENABLE`.
- The existing Digitizer `Board model` combo shows only the model number.
  There is no separate diagnostic page. Changing the model repopulates its
  sampling rates, input ranges, record/pre-trigger/delay alignment, external
  trigger range, and SDK-recommended FIFO-only flag.
- Connect verifies the selected model against `AlazarGetBoardKind()` at fixed
  System 1 / Board 1. Unsupported models and model-selection mismatches stop
  before board configuration.
- The Windows ATS-SDK Release build and CTest passed 9/9. The refreshed package
  passed `--smoke-test`; build and package SHA-256 are
  `7FB143E4B27147F4E0331CD463FC75093B5D322284C12774C4D88C5FFE95182B`.
- The Jetson CUDA-only source bundle was regenerated, its source manifest
  verified with zero mismatches, and all Bash scripts passed syntax checks.
- Only ATS9371 has been exercised on the current hardware. Every other model
  remains subject to driver, trigger, DMA, and sustained-throughput acceptance
  on its actual board.

Jetson Qt/CMake compatibility baseline (2026-07-23):

- The shared Qt Widgets/OpenGL UI now declares Qt 6.2 as its minimum. The
  Qt 6.3-only `qt_standard_project_setup()` helper was removed and replaced
  with direct CMake AUTOMOC, AUTOUIC, and AUTORCC settings.
- The project CMake minimum is 3.18. Jetson no longer depends on the CMake
  3.24-only CUDA `native` architecture value.
- `FMCW_JETSON_CUDA_ARCHITECTURES=auto` maps Thor, Orin, Xavier, TX2, and
  Nano/TX1 to numeric CUDA architectures. Unknown models stop with an
  instruction to set the value explicitly.
- Windows Qt 6.11 was reconfigured through the same direct autogen path. The
  ATS-SDK Release build and CTest passed 9/9, and the refreshed package passed
  `--smoke-test`. Build and package SHA-256 are
  `33A582FE14075AAEE6EB7EB92B602AC37E8E4C793350D374BABD12EF968FEB5C`.
- The operator completed the native Qt 6.2/CMake 3.18 ARM64 build and launched
  the simulator on Jetson. The Qt smoke tests shown with that build passed.

Jetson Qt 6.2 dark-theme portability (2026-07-23):

- Jetson screenshots showed that the system palette leaked through scroll-area
  viewports, standard group boxes, disabled controls, and Overview cards. The
  previous constructor first polished widgets with a light stylesheet and then
  appended a dark stylesheet after construction, which behaved inconsistently
  on Qt 6.2.
- Both platform entry points now install one application-wide Fusion style,
  complete dark palette, and dark stylesheet before constructing `MainWindow`.
  Static surfaces use stable object names, while dynamic status properties are
  explicitly repolished. The native operating-system title bar remains
  controlled by the desktop theme.
- The CMake 3.18 Jetson build path no longer uses the CMake 3.20-only
  `ctest --test-dir` option. Preset schema version 3 is documented and declared
  as requiring CMake 3.21, while `deploy/jetson/build.sh` remains the canonical
  CMake 3.18 path.
- Windows Qt 6.11 Release build and CTest passed 9/9. Automated renders of
  all eight pages plus live Time Domain, FFT, B-scan, and the OpenGL point cloud
  retained dark backgrounds and readable enabled/disabled controls. The
  refreshed package passed `--smoke-test`; its EXE SHA-256 is
  `A870F2F6119B2F6914C84C5926D5672480CCB5AD2BA255C01014E3A119E58AE0`.
