# Phase 7 Execution Plan

이 문서는 Phase 7 구현과 검증의 단일 실행 기준이다. 이후 작업을 시작하거나 재개할 때 이 문서와 `docs/phase_status.md`를 먼저 확인하고, 완료된 subphase의 상태와 검증 결과를 함께 갱신한다.

Last reviewed: 2026-07-15

## 1. Fixed Decisions

1. Windows와 Jetson은 같은 core와 Qt 6 UI 소스를 사용하고 target package만 분리한다.
2. Windows는 console 없이 `FMCW_LiDAR.exe`로 실행하고, Jetson은 local Qt launcher로 실행한다.
3. hardware/simulator/replay 선택과 모든 운용 설정은 GUI와 profile에서 수행한다. PowerShell 또는 command-line option을 정상 운용 경로로 사용하지 않는다.
4. ATS9371은 System 1 / Board 1로 고정하고 A 또는 B 단일 channel만 수집한다. A+B 동시 수집은 지원하지 않는다.
5. hardware trigger는 UP chirp 시작에서 한 번만 발생하고, Alazar record 하나가 전체 UP+DOWN chirp period를 포함한다.
6. `records_per_buffer`는 한 B-scan line의 A-scan 수이고, `scan.y_line_count`는 사용자가 설정하는 B-scans/frame 수다.
7. Time Domain과 FFT는 선택한 A-scan만 표시하지만 peak, distance/velocity, B-scan, 3D, UDP, raw/processed storage는 모든 A-scan을 처리한다.
8. CPU backend는 FFTW3f, GPU backend는 CUDA/cuFFT를 사용한다. 두 backend는 실행 주체만 다르며 preprocessing부터 validity까지 동일한 신호처리 순서, 수식, 설정, 입력과 출력 계약을 사용한다.
9. 현재 version의 peak는 threshold를 초과하는 최대 정수 FFT bin이며 interpolation이나 sub-bin estimation을 사용하지 않는다.
10. real-time queue overflow의 기본 정책은 acquisition `STOP`이다.
11. EDFA는 `none`, `manual`, `controlled`를 지원하며 EDFA가 없어도 acquisition이 가능해야 한다.
12. global START/STOP 하나가 processing, storage, digitizer, MCU, optional EDFA를 함께 제어한다.
13. 각 subphase는 구현, 테스트, 문서 갱신 후 독립 commit/push한다.

## 2. Active Data Contract

실제 운용 및 성능 합격 profile은 다음 값으로 고정한다.

```text
Laser full-period rate: 200 kHz
Digitizer sample rate: 1 GS/s
Record length: 4992 samples
UP segment length: 2048 samples
DOWN segment length: 2048 samples
Records per B-scan line: 998
B-scan line rate: 200 Hz

One Alazar DMA buffer
  -> 998 full-period records
  -> each record splits into one UP segment and one DOWN segment
  -> 1996 FFT rows of length 2048
  -> 998 UP/DOWN peak pairs
  -> 998 distance/velocity/XYZ points
  -> one completed B-scan line
```

`998 / 200000 = 4.99 ms`이므로 다음 DMA line 도착 주기는 약 5 ms다. Phase 7.3의 hard real-time gate는 DMA completion부터 998번째 point와 B-scan line 완성까지 `5.00 ms` 이내다.

legacy GPU code의 `record 2개 = UP/DOWN 1쌍` 규칙은 직접 이식하지 않는다. 새 trigger 계약에서는 `record 1개 = full period = point 1개`가 우선한다.

DMA buffer는 acquisition, processing, raw storage 사이의 기본 운반 단위다. UI 갱신을 위해 acquisition 단위를 A-scan 하나로 다시 쪼개지 않는다.

## 3. Execution Rules

- Status는 `pending`, `in_progress`, `done`, `blocked` 중 하나를 사용한다.
- 동시에 하나의 subphase만 `in_progress`로 둔다.
- 다음 subphase로 이동하기 전에 현재 subphase의 exit criteria를 모두 확인한다.
- simulator/replay 검증과 실제 hardware acceptance를 구분해서 기록한다.
- hardware acceptance가 필요한 항목을 simulator test 통과만으로 `done` 처리하지 않는다.
- 기존 user 변경과 관련 없는 파일은 수정하거나 되돌리지 않는다.
- 각 commit 전에 Release build와 해당 CTest를 실행하고 결과를 `docs/phase_status.md`에 기록한다.
- commit/push 후 commit hash를 status 문서에 기록한다.

## 4. Status Board

| Subphase | Status | Main result | Hardware required |
| --- | --- | --- | --- |
| 7.1 ADC and configuration correctness | done | 정확한 ATS9371 sample과 일관된 chirp profile | 최종 capture 비교 시 필요 |
| 7.2 Hardware runtime and DMA batch | blocked | EXE에서 실제 ATS9371 연결 및 지속 DMA 수집 | Windows ATS9371 acceptance 필요 |
| 7.3A 998-record processing baseline | done | strict DMA simulator, 1996 FFT/998 XYZIV와 deadline 계측 | reference raw로 검증 가능 |
| 7.3B FFTW processing optimization | done | 16 fixed FFTW batches, parity and selected-spectrum copy | target CPU acceptance remains in 7.3D |
| 7.3C CUDA processing optimization | done | signed raw to cuFFT/peak/XYZIV full GPU batch pipeline | Jetson parity remains required |
| 7.3D 200 Hz performance acceptance | blocked | strict runtime probe와 고정 10분 gate 구현, 현재 5 ms 초과 | target Windows/Jetson 필요 |
| 7.4 High-speed raw storage | blocked | contiguous DMA-block v2 recording/replay 구현 | NVMe 10-minute sustained test 필요 |
| 7.5 Laser, MCU, and scan alignment | pending | 실제 chirp/scan 위치와 point cloud 정합 | laser, oscilloscope, MCU 필요 |
| 7.6 EDFA and Windows acceptance | pending | 안전한 광 출력과 Windows release candidate | EDFA와 광 안전 환경 필요 |
| 7.7 Jetson integration and release | pending | Jetson 현장 운용 package | Jetson과 arm64 ATS driver 필요 |

## 5. Phase 7.1: ADC and Configuration Correctness

### Scope

- ATS9371 12-bit sample의 16-bit DMA alignment를 SDK 방식으로 변환한다.
- ADC 변환을 독립 helper로 분리하고 minimum, midpoint, maximum, clipping test를 추가한다.
- legacy `raw - 32768` 표시와 SDK `sample >> 4` 변환의 full-scale 관계를 검증한다.
- `sample_rate_hz`, `chirp_period_samples`, `sample_point`, trigger offset의 full-period acquisition contract를 강화한다.
- 기본 profile의 1 GS/s, 20 us, 3840-sample 불일치를 제거한다.
- raw format이 original DMA `uint16`을 보존할지 converted `int16`을 저장할지 확정하고 format version 영향을 기록한다.

### Exit Criteria

- SDK 예제와 동일한 ADC code 변환 단위 테스트가 통과한다.
- default profile이 chirp period mismatch warning 없이 검증된다.
- FFTW synthetic tone과 replay test가 변환 변경 후에도 통과한다.
- Windows Release build와 전체 CTest가 통과한다.

### User-visible Result

- 실제 ATS9371 waveform의 DC, amplitude, FFT input scale을 신뢰할 수 있다.
- legacy peak threshold 값은 그대로 복사하지 않고 현재 dBFS 기준으로 다시 설정한다.

### Completion Record

- ATS SDK 25.1.0 예제와 같은 right shift 규칙을 독립 sample helper에 적용했다.
- 12-bit 전체 4096 code가 legacy `raw - 32768` signed full-scale 값과 일치함을 검증했다.
- 기본 개발 profile은 `1 GS/s`, `3840 period samples`, `4096 record samples`를 사용한다. 256-sample capture margin은 의도 확인용 Warning으로 표시한다.
- Laser UI는 거리 변환용 measured bandwidth와 full triangular sweep rate를 `Hz` 단위로 직접 설정한다.
- raw format v1은 converted signed `int16`을 유지하고 original DMA `uint16` block은 Phase 7.4의 version 2로 추가한다.
- Windows Release build에서 ATS-SDK, FFTW, CUDA/cuFFT가 활성화되었고 CTest 5/5가 통과했다.
- Implementation commit: `82b8369`

## 6. Phase 7.2: Hardware Runtime and DMA Batch

### Scope

- `ApplicationController`의 fixed fake members를 runtime adapter factory로 교체한다.
- GUI/profile에서 `Alazar`, `Simulator`, `Replay` source를 선택한다.
- 실제 `AlazarDigitizer`, `McuSerialController`, `EdfaSerialController`를 EXE runtime에 연결한다.
- Qt 8 ms polling timer를 전용 continuous acquisition worker로 교체한다.
- configured `records_per_buffer` 전체와 DMA metadata를 보유하는 immutable/pool-backed batch contract를 추가한다.
- CPU 경로는 큰 연속 복사 후 buffer를 재게시하고, CUDA 경로는 H2D completion event 후 재게시한다.
- DMA buffer sequence, completion timestamp, measured B-scan rate, overflow, trigger miss, queue telemetry를 유지한다.
- Stop 시 MCU trigger 제거, Alazar abort/release, EDFA off 순서를 보장한다.

### Exit Criteria

- simulator와 replay가 새 batch contract로 기존 기능을 유지한다.
- Windows ATS9371에서 buffer post/wait/repost가 continuous worker 안에서 동작한다.
- selected A-scan waveform이 실제 DMA buffer마다 갱신된다.
- queue overflow와 DMA overflow가 조용히 drop되지 않고 STOP 원인을 기록한다.
- 최소 10분 continuous DMA 시험에서 locked-page leak과 handle leak이 없다.

### User-visible Result

- 명령행 설정 없이 EXE에서 실제 ATS9371을 연결하고 START/STOP할 수 있다.
- UI에서 실제 DMA rate, period, buffer/trigger 상태를 확인할 수 있다.

### Phase 7.2 Software Verification Record (2026-07-14)

- GUI/profile runtime selection now supports Simulator, Alazar ATS9371, and Raw Replay without command-line source options.
- The packaged EXE creates real `AlazarDigitizer`, `McuSerialController`, and `EdfaSerialController` adapters for the hardware source.
- `ContinuousAcquisitionWorker` replaces Qt acquisition polling; the Qt timer is now limited to telemetry and snapshot publication.
- One pool-backed immutable `RawFrameBatch` owns every record from one DMA completion. Raw storage uses aliasing frame references and processing queues the complete batch.
- The Alazar path waits once, copies/converts every record, and reposts the SDK buffer immediately after the ownership copy.
- Windows MSVC Release built with ATS-SDK 25.1.0, FFTW, and CUDA/cuFFT enabled; CTest passed 5/5.
- A deterministic device-order test verifies MCU stop, digitizer abort/stop, and controlled EDFA off in that exact sequence.
- Packaged simulator runtime delivered 141 batches and 9,024 records with processing queue 0/32, DMA drops 0, and trigger misses 0 in the GUI smoke run.
- Remaining Phase 7.2 acceptance: connect ATS9371 and complete the 10-minute DMA, locked-page, handle, STOP, and overflow checks. Status remains `in_progress` until that hardware evidence exists.
- Software implementation commit: `3cfcea3`

## 7. Phase 7.3: Signal Processing Optimization

Phase 7.3은 batch API 구현만으로 완료하지 않는다. 7.3A부터 7.3D까지 모두 통과해야 `done`이다.

### 7.3A: 998-Record Baseline And Timing Contract

#### Scope

- `4992 samples/record`, `998 records/buffer`, `2048 UP + 2048 DOWN`, FFT length 2048 reference input을 고정한다.
- 현재 single-record FFTW/CUDA 경로의 copy, preprocessing, FFT, magnitude, peak, geometry, snapshot 시간을 구간별로 계측한다.
- DMA completion timestamp부터 B-scan line completion까지 end-to-end processing timer를 추가한다.
- 998개 독립 peak pair와 distance/velocity/XYZ 결과를 만드는 single-record reference를 보존한다.
- 이전 record 또는 batch의 peak를 재사용하지 않고 invalid float 값은 `NaN`을 유지한다.

#### Exit Criteria

- deterministic simulator/replay 입력이 항상 1996 FFT와 998 point를 생성한다.
- timing telemetry가 p50, p95, p99, maximum, deadline miss count를 보고한다.
- 현재 batch-1 병목과 allocation/copy 횟수가 수치로 기록된다.

#### Software Verification Record (2026-07-15)

- EXE 시작 profile과 `config/profiles/ats9371_200hz_simulator.yaml`은 1 GS/s, 4992 samples, 998 records, UP/DOWN 각 2048 samples, 8 DMA buffers, 200 kHz sweep를 고정한다.
- 이 profile의 UP `[0, 2048)`와 DOWN `[2944, 4992)` 위치는 부하 및 parity 검증용 synthetic reference다. 실제 trigger 배선 후 측정한 안정 구간으로 교체하며 길이와 처리 계약만 이 단계에서 고정한다.
- strict simulator는 소비 속도에 맞춰 느려지지 않는다. 4.99 ms 절대 주기로 DMA completion을 bounded ring에 넣고, 8개 buffer가 모두 차면 drop을 증가시킨 뒤 overflow를 latch하고 acquisition `STOP` 오류를 전달한다.
- development simulator는 기존 UI/단위 테스트용 30 Hz pacing을 유지하며 strict 동작은 profile의 `runtime.simulator_realtime_dma: true`에서만 활성화된다.
- DMA metadata는 completion과 ownership-copy 완료 timestamp를 각각 보존한다. Processing telemetry는 copy, signal, end-to-end batch latency와 최근 p50/p95/p99, 전체 max, 5 ms miss 누계를 보고한다.
- deterministic Release baseline은 정확히 998 records, 1996 FFT, 998 valid XYZIV와 한 B-scan line을 생성했다.
- 현재 single-record FFTW baseline은 `28.1651 ms/batch`였고 5 ms deadline miss 1회를 기록했다. record마다 UP/DOWN input 2개, spectrum 2개, magnitude 2개와 `ProcessedFrame`을 생성하고 FFT를 1996회 동기 호출하는 구조가 7.3B의 제거 대상이다.
- Release build와 CTest 6/6이 통과했으며 strict 4992 x 998 payload, 4.99 ms cadence, 8-buffer overflow를 자동 검증한다.
- Packaged EXE smoke test passed. SHA-256: `BE628C72E69EFEF613BE349B97A9446368793DB6651D5A5B5D1AF993F0948B23`.

### 7.3B: FFTW Processing Optimization

#### Scope

- 998 records의 UP/DOWN을 preallocated contiguous aligned input 1996개로 구성한다.
- `fftwf_plan_many_dft_r2c` batch 1996을 기준 구현으로 사용한다.
- full batch와 소수의 고정 chunk 구성을 benchmark하고, 실제 측정으로 더 빠른 구성을 선택한다.
- FFTW threads, SIMD-friendly preprocessing, parallel magnitude/peak/geometry를 적용한다.
- steady state에서 record별 vector 생성, FFT plan 생성, heap allocation을 제거한다.
- selected record의 Time Domain/FFT만 UI snapshot으로 복사한다.

#### Exit Criteria

- 한 DMA buffer가 1996개의 개별 synchronous FFT call을 만들지 않는다.
- batch 결과가 single-record reference와 정의된 tolerance 안에서 일치한다.
- output point count가 항상 998이고 invalid 결과는 `NaN`이다.
- CPU mode의 B-scan, 3D, UDP, processed storage가 동일한 batch 결과를 사용한다.

#### Software Verification Record (2026-07-15)

- `SignalProcessor::processBatch` now preprocesses one DMA batch into reusable contiguous UP/DOWN workspaces and returns all peak, distance, velocity, and XYZIV results together.
- FFTW uses a measured fixed chunk of 64 records, or 128 interleaved UP/DOWN transforms. The 998-record qualification workload therefore uses 16 FFT batch executions instead of 1,996 synchronous single-transform executions; the final chunk is zero padded to keep the plan fixed.
- Each FFTW chunk is split across up to 16 independent plan/workspace lanes. OpenMP parallelizes preprocessing, lane execution, peak search, and geometry without changing the common FFTW/CUDA algorithm contract.
- Only the selected record retains UP/DOWN magnitude spectra for Time Domain and FFT UI publication. All 998 records still produce independent peak and XYZIV results.
- Automated parity tests compare batch results against the original single-record FFTW path for integer bins, dBFS magnitude, distance, velocity, XYZIV, and below-threshold `NaN` behavior. The 998-record execution-count test verifies exactly 16 FFT batch calls and 998 outputs.
- Historical 2026-07-15 measurements used three warm-up batches before 32 complete batches. Across four independent runs, average `SignalProcessor::processBatch` time was 2.46-3.11 ms and end-to-end p50 was 2.41-2.98 ms. The 2026-07-16 no-warmup procedure below supersedes this benchmark method.
- Short-run maximum end-to-end latency varied from 4.46 to 8.29 ms, with 0-2 deadline misses. Phase 7.3B is complete, but these measurements are not a Phase 7.3D pass because maximum latency is not yet deterministic and the required 10-minute run has not been completed.
- Windows MSVC Release and CTest passed 6/6. The packaged EXE smoke test passed with SHA-256 `D1D6A6E75EC5C41DADADE1C1C37512B829F3286529EAD7C0061803C42C02AEB4`.

### 7.3C: CUDA Processing Optimization

#### Scope

- legacy two-slot stream/event 구조를 새 full-period record 계약에 맞게 재구성한다.
- pinned host memory와 persistent device workspace를 사용한다.
- ADC conversion, DC removal, UP/DOWN extraction, polarity, window를 CUDA kernel로 처리한다.
- cuFFT plan-many batch 1996, magnitude dBFS, independent maximum integer-bin peak search, distance/velocity/XYZ를 GPU에서 수행한다.
- CUDA kernel은 FFTW reference와 동일한 preprocessing 순서, dBFS scaling, strict threshold, 무보간 peak, calibration, `NaN` 규칙을 사용한다.
- Persistent pinned staging and device workspaces remove steady-state allocation. Inter-batch H2D overlap moves to 7.3D/7.4 with the contiguous DMA-block ownership contract.
- selected FFT 한 쌍과 998개 peak/point 결과만 host로 복사한다.

#### Exit Criteria

- FFTW와 CUDA의 validity와 integer peak bin은 정확히 일치하고 magnitude, distance, velocity, XYZ는 정의된 tolerance 안에서 일치한다.
- 매 record 또는 UP/DOWN마다 stream synchronize를 호출하지 않는다.
- selected FFT 외 전체 spectrum을 매 batch D2H하지 않는다.
- local RTX와 Jetson에서 steady-state allocation과 memory growth가 없다.

#### Software Verification Record (2026-07-15)

- The legacy `gpu_strea.cu` flow was audited before implementation. Its whole-buffer H2D, windowed preprocessing, cuFFT plan-many, independent peak reduction, XYZIV output, and selected-spectrum D2H structure were retained.
- Legacy assumptions were not copied blindly: current input is converted signed `int16`, one record is one full UP+DOWN period, UP/DOWN are segmented on the GPU, dBFS uses coherent window gain, threshold comparison is strict `>`, peaks are integer bins without interpolation, and Cartesian axes use the current X-lateral/Y-forward/Z-vertical contract.
- `processing/cuda/cuda_signal_pipeline.cu` now owns persistent pinned host staging, persistent device input/window/FFT/peak/measurement buffers, one cuFFT plan-many batch, and one non-blocking stream. Production CUDA batch processing no longer routes full spectra back through the CPU FFT path.
- Per DMA batch, the GPU performs DC removal, UP/DOWN extraction, down-polarity handling, windowing, 1,996 length-2048 FFTs, strict peak search, distance, velocity, calibration, and XYZIV. Host transfer returns 1,996 compact peaks, 998 measurements, and only the selected UP/DOWN spectra.
- FFTW/CUDA full-batch tests require exact validity and integer peak bins, 0.05 dB agreement for signal/peak magnitudes, 2 dB agreement for the numerical floor below -100 dBFS, matching distance/velocity/XYZIV, and identical threshold-rejected `NaN` behavior.
- The historical 2026-07-15 local RTX qualification used three warm-up batches and 32 measured 4992 x 998 batches. All 31,936 measured records produced valid XYZIV outputs, but observed p50 remained roughly 6-7 ms. The current no-warmup result is recorded below and Phase 7.3D still does not pass.
- Nsight Systems measured only about 0.063 ms of GPU kernels per batch. Full-period H2D averaged about 2.0 ms and reached about 3.9 ms; WDDM submission/synchronization and gathering 998 separate host sample vectors dominate the remaining latency.
- Inter-batch overlap is intentionally deferred to the contiguous DMA-block ownership work because the current synchronous per-record `RawFrame` container must first be gathered into pinned memory. No per-record or per-segment CUDA synchronization remains; there is one synchronization at batch result completion.
- Windows MSVC Release and CTest passed 7/7, including FFTW/CUDA full-result parity and the strict CUDA qualification workload. The packaged EXE smoke test passed with SHA-256 `2C03B0FDE71B703CFFCBC86195AFDEA6A005A343ABB0AE91AD06F9252FE824B2`.

### 7.3D: 200 Hz Real-Time Performance Acceptance

#### Measured Scope

측정 시작은 Alazar DMA completion이고 종료는 998번째 distance/velocity/XYZ와 한 B-scan line snapshot이 완성된 시점이다. 다음 항목을 포함한다.

- ATS wait completion, DMA ownership lease, and record descriptor materialization
- UP/DOWN segmentation, DC removal, polarity, and window
- 1996 real-to-complex FFTs of length 2048
- magnitude dBFS, strict threshold, maximum integer-bin peak, and `NaN` validity
- 998 distance/velocity/XYZ points and B-scan line assembly

Disk write, UDP transmission, and Qt paint time은 이 5 ms signal-processing gate에서 제외하며 각 subsystem acceptance에서 별도로 측정한다.

#### Hard Pass Criteria

- laser 200 kHz, digitizer 1 GS/s, record 4992 samples, records/buffer 998 조건을 사용한다.
- B-scan 200 Hz deadline은 DMA buffer당 `5.00 ms`다.
- normal configure/start 직후 첫 batch부터 최소 10분 동안 어떤 batch도 end-to-end processing `5.00 ms`를 초과하지 않는다. 별도 warm-up batch는 사용하지 않는다.
- p50, p95, p99, maximum latency와 deadline miss count를 모두 기록하며 평균값만으로 합격시키지 않는다.
- processing queue가 지속적으로 증가하지 않고 DMA drop, processing drop, stale result가 모두 0이다.
- 각 batch가 정확히 998개의 결과를 생성하고 다음 batch 도착 전에 line 결과가 완성된다.
- FFTW와 CUDA를 각각 시험하며, 두 backend가 각 target platform에서 위 기준을 통과해야 Phase 7.3을 완료한다.

#### User-visible Result

- Processing 페이지에서 현재 backend의 batch latency, 5 ms deadline margin, miss count를 확인할 수 있다.
- 지원하지 못하는 backend/platform 조합은 실시간 가능 상태로 표시하지 않는다.

#### Software Qualification Record (2026-07-15)

- `fmcw_phase7_realtime_probe.exe` connects the strict `FakeDigitizer` to the production `AcquisitionSession`, `ContinuousAcquisitionWorker`, bounded `ProcessingService`, snapshot aggregation, and global STOP path. The producer stays on the absolute 4.99 ms cadence and never waits for the consumer.
- The fixed two-second FFTW probe delivered and processed 401 complete DMA batches, 400,198 records, and the same number of valid XYZIV outputs. Queue high-water was 3/32 with zero DMA drops, but end-to-end p50 was 10.712 ms, maximum was 22.065 ms, and 400 batches missed the 5 ms deadline.
- The fixed two-second CUDA probe processed 158 complete batches and 157,684 XYZIV outputs. Its processing queue reached exactly 32/32, the next batch was rejected, and the acquisition worker preserved `Processing queue capacity exceeded` as the STOP reason. No batch was silently dropped.
- The probe returns success only for payload/result integrity and a valid clean-duration or overflow-to-STOP route. It prints `HARD_PASS` or `HARD_FAIL` separately so a functional test cannot be mistaken for real-time acceptance.
- `fmcw_phase7_realtime_acceptance.exe` is a separate option-free executable with a fixed 600-second run per backend. It returns failure for any deadline miss, drop, growing queue, incomplete 998-record line, unavailable CUDA runtime, or backend failure. It is intentionally excluded from normal CTest.
- The 20-minute combined FFTW/CUDA acceptance was not run. Current Windows results fail the 5 ms gate, and Jetson evidence is unavailable, so Phase 7.3D and overall Phase 7.3 remain blocked rather than complete.

#### Single-Slot ATS DMA Event Update (2026-07-16)

- This update supersedes the earlier warm-up-based benchmark procedure. Qualification now measures the first submitted batch after normal configure/start; no dummy processing batch is part of production or acceptance.
- The CUDA processing capacity remains one slot. H2D completion and full result completion use separate CUDA events, allowing ATS DMA repost after H2D while processing and compact D2H continue.
- Native ATS9371 12-bit left-aligned samples travel from the contiguous SDK DMA allocation to the GPU without a CPU-wide conversion or duplicate whole-buffer staging copy. Signed simulator/replay input remains supported through the same explicit sample-format contract.
- The strict lifetime test verifies early DMA-owner release and later full result collection. The direct no-warmup CUDA benchmark still measures about 10 ms p50 for 4992 x 998, so the requested event and DMA ownership changes are complete but the 5 ms performance gate remains open.
- Release CTest passes 9/9. The refreshed packaged GUI passes its hidden smoke test and matches the Release executable at SHA-256 `4FCE36B2FFABF7E4A27080675CB2290F38C4C258965613556CA8EE985469A4C6`.

#### Windows Latency Optimization Update (2026-07-17)

- The single CUDA slot now waits directly on its H2D and completion events when the slot is full. The previous 100 us condition-variable poll could sleep for roughly 10 ms on Windows even though Nsight measured only about 0.31 ms of GPU work per batch.
- Snapshot publication now consumes all 998 results under one lock and timestamps B-scan completion before optional disk/UDP callbacks. FFTW preprocessing keeps the 64-record cache-local chunk but fixes OpenMP at 16 lanes and combines zero-fill, conversion, polarity, and window work to remove two full segment-memory passes.
- The strict simulator uses a Windows high-resolution waitable timer. Acquisition, processing, and simulator source threads use critical priority without changing the process or Qt UI priority; OpenMP workers use high priority. CPU-set pinning was tested and removed because it increased latency on the local hybrid CPU.
- Final direct no-warmup measurements produced CUDA p50 0.514 ms and maximum 0.948 ms with zero misses. Ten independent 32-batch FFTW runs produced p50 1.460-1.686 ms and maximum at most 4.307 ms with zero direct misses.
- The fixed 600-second acceptance was run before the final preprocessing pass. Each backend processed 120,242 complete DMA batches and 120,001,516 valid XYZIV results with zero drops or rejections. FFTW recorded 3 deadline misses and 5.763 ms maximum; CUDA recorded 17 misses and 8.497 ms maximum. Functional and sustained-throughput checks passed, but the hard gate did not.
- After the final preprocessing pass, ten two-second strict runs produced about 4,000 complete batches per backend with queue high-water 1 and zero drops. FFTW and CUDA each had one Windows scheduling outlier: FFTW signal latency reached 5.331 ms, while CUDA ownership handoff reached 4.979 ms and total latency reached 5.689 ms.
- The 5 ms requirement remains unchanged. Phase 7.3D is still pending because Windows scheduling tail and actual ATS9371 hardware behavior require separate acceptance evidence; no interpolation, dropped result, extra CUDA slot, or relaxed deadline was used to obtain the improved numbers.
- The refreshed packaged GUI passed its hidden smoke test. The Release and packaged executables both have SHA-256 `5BD1213A5A51DA5A39BFEB217D64262B428DE6140A3CA1E67DB6F45C12E48405`.

#### Post-Reboot Idle-Machine Acceptance Rerun (2026-07-17)

- The rerun started with the RTX 5080 at P8, 2-4% GPU utilization, and 716-862 MiB allocated. Ten direct 32-batch runs had zero misses: FFTW p50 was 1.530-1.819 ms with a 4.954 ms maximum, and CUDA p50 was 0.490-0.543 ms with a 1.122 ms maximum.
- Ten two-second strict probes produced FFTW HARD_PASS in 9/10 runs and CUDA HARD_PASS in 10/10 runs. All runs kept queue high-water at 1, DMA drops and rejections at zero, and exact 998-record XYZIV accounting. The single FFTW miss reached 5.193 ms total.
- The fixed 600-second rerun processed 120,241 batches and 120,000,518 valid XYZIV records per backend with queue high-water 1 and zero drops or rejections. FFTW measured p50 2.087 ms, p95 2.638 ms, p99 2.930 ms, maximum 7.481 ms, and 5 misses. CUDA measured p50 1.005 ms, p95 2.256 ms, p99 2.517 ms, maximum 6.476 ms, and 8 misses.
- Maximum signal-processing latency stayed below the gate for both backends: 4.197 ms for FFTW and 4.640 ms for CUDA. Maximum simulator ownership/materialization latency reached 5.873 ms and 5.795 ms respectively, identifying the remaining synthetic-test tail before the FFT backend rather than sustained FFT throughput.
- Rebooting reduced the CUDA long-run miss count from 17 to 8 and its maximum from 8.497 ms to 6.476 ms, but did not produce zero misses. Phase 7.3D therefore remains pending, and the same timestamps must be collected with the ATS9371 DMA path before changing the processing pipeline again.

#### Ownership-Stage Instrumentation and Descriptor Reuse (2026-07-17)

- Runtime-only timestamps now split the DMA-to-result path into acquisition wakeup, digitizer materialization, session validation, enqueue dispatch, queue wait, and processing compute. Their sum equals the existing end-to-end batch latency. Raw v2 serialization is unchanged.
- The simulator and ATS9371 adapters prebuild the static metadata for all 998 records at start and stamp only per-batch values during acquisition. The ATS SDK does not expose a separate hardware-completion host timestamp, so the ATS adapter records completion and acquisition wakeup at the successful wait return and measures materialization from that point.
- Release CTest passed 9/9. Ten direct runs had zero misses: FFTW arithmetic mean end-to-end latency was 1.648-1.922 ms with 4.204 ms maximum, and CUDA was 0.517-0.611 ms with 1.044 ms maximum.
- Ten two-second strict probes produced FFTW HARD_PASS in 9/10 runs and CUDA HARD_PASS in 10/10 runs. Simulator descriptor materialization averaged 0.026-0.040 ms, session validation about 0.004 ms, and queue wait about 0.01-0.02 ms. The one FFTW miss reached 5.335 ms and was dominated by a 4.497 ms compute tail.
- The 600-second run processed 120,241 FFTW batches and 120,242 CUDA batches, totaling 120,000,518 and 120,001,516 valid XYZIV results respectively. DMA drops and rejections stayed at zero; queue high-water was 1 for FFTW and 2 for CUDA.
- FFTW measured 2.009 ms arithmetic mean, 1.988 ms p50, 6.944 ms maximum, and 6 misses. Its mean wakeup/materialization/compute stages were 0.342/0.027/1.622 ms; maximum wakeup and compute were 4.491 and 6.372 ms.
- CUDA measured 1.607 ms arithmetic mean, 1.512 ms p50, 8.128 ms maximum, and 38 misses. Its mean wakeup/materialization/compute stages were 0.391/0.040/1.160 ms; maximum wakeup and compute were 6.203 and 2.759 ms. A following short probe returned CUDA compute to 0.571 ms mean, showing that the long-run CUDA compute level was not a permanent pipeline regression.
- Descriptor construction, session validation, dispatch, and normal queueing are not the remaining bottlenecks. Sustained 200 Hz throughput passes, while rare Windows simulator wakeup and compute scheduling tails keep the strict 5 ms hard gate open. Actual ATS9371 measurements remain required before Phase 7.3D hardware acceptance.
- The refreshed packaged GUI passed `--smoke-test`. Release and package SHA-256 both equal `19C0938C8998500EA3788A33B0B0C1E7DCA0A90F1B927712472136EFAE47F81C`.

## 8. Phase 7.4: High-Speed Raw Storage

### Scope

- raw writer 입력을 A-scan record가 아니라 DMA block으로 변경한다.
- compact block header와 contiguous payload를 사용하고 per-record stream call을 제거한다.
- raw와 processed writer queue/worker를 분리한다.
- large sequential asynchronous write, file preallocation, split part, free-space check를 구현한다.
- Windows와 Jetson native storage backend는 공통 writer interface 뒤에 둔다.
- replay가 block metadata에서 원래 full-period record와 scan position을 복원한다.
- queue use, throughput, last accepted/written block, disk error를 UI와 log에 표시한다.

### Exit Criteria

- target data rate의 최소 1.3배 sustained write benchmark를 통과한다.
- raw+processed 동시 저장에서 processing 또는 acquisition thread가 disk I/O를 직접 기다리지 않는다.
- 최소 10분 기록에서 누락, queue overflow, corrupt split part가 없다.
- replay 결과가 live FFTW reference와 일치한다.

### Software Verification Record (2026-07-15)

- `RawFrameBatch` now owns one contiguous signed `int16` payload. Its 998 `RawFrame` entries use non-owning sample views, while standalone frame copies retain deep-copy compatibility with raw v1 and legacy callers.
- Fake and ATS acquisition convert directly into the contiguous block, removing 998 steady-state sample-vector allocations. CUDA staging uses one batch `memcpy` instead of 998 record copies. Post-change two-second CUDA probes ranged from 313 batches followed by bounded-queue STOP to 401 completed batches with queue high-water 14/32; the latter still had 39.70 ms p50 because backlog accumulated.
- The application enqueues raw recording once per DMA block. Raw and processed storage have separate bounded queues and worker threads, so a slow raw disk write does not directly block processed-result writing, acquisition, or processing.
- Raw format v2 writes one compact block/record-metadata header buffer and one contiguous payload per DMA block. Split rotation occurs only before a complete block, files are preallocated and truncated to committed bytes, and START checks that at least two complete blocks fit in free space.
- Raw v1 replay remains supported. Raw v2 block replay restores all 998 full-period records, trigger/scan metadata, contiguous samples, split-part boundaries, and exact FFTW peak/distance/velocity/XYZ parity.
- The Storage page reports independent raw/result queue use and high-water marks, raw block/byte counts, per-writer throughput, and storage STOP reason.
- Four fixed 32-block probes wrote 0.319 GB at 3.24-4.66 GB/s, or 1.62-2.33 times the 1.997 GB/s target input rate. These are short Windows cached-write probes, not sustained NVMe acceptance.
- `fmcw_phase7_storage_acceptance.exe` has a fixed 120,240-block, 4.99 ms cadence and preflights 1,198,075,207,680 payload bytes before starting. It was intentionally not run because the required 10-minute recording is about 1.2 TB.
- Release CTest passed 9/9, including the v2 block codec, split/runtime replay, FFTW parity, free-space rejection, and independent raw/processed worker checks. The packaged EXE smoke test passed with SHA-256 `E759122932CC8BDC4F228DFD0753970E20A51933C42F9F235D533F4A6C52B02E`. Phase 7.4 hardware acceptance remains blocked until a target NVMe completes the ten-minute run without queue growth, overflow, or corrupt parts.

### User-visible Result

- START 한 번으로 고속 raw recording을 켜고 저장본을 동일한 B-scan/3D 화면에서 replay할 수 있다.

## 9. Phase 7.5: Laser, MCU, and Scan Alignment

### Scope

- oscilloscope로 UP trigger, full chirp period, trigger offset, stable UP/DOWN ranges를 측정한다.
- measured bandwidth와 sweep rate를 distance profile에, velocity wavelength와 scale/offset을 calibration profile에 반영한다.
- degree 또는 calibration table을 MCU normalized XY와 DAC ABCD code에 연결한다.
- scan angle setting 변경이 physical MCU waveform과 point-cloud metadata에 함께 반영되게 한다.
- full-frame waveform은 `records_per_buffer * y_line_count` point를 사용한다.
- marker는 각 B-scan 시작점에서만 출력한다.
- MCU waveform cycle과 measured Alazar DMA frame time 차이를 검사한다.

### Exit Criteria

- physical target의 angle/distance가 calibration tolerance 안에서 3D point와 일치한다.
- MCU upload count, marker position, START/STOP ACK가 firmware와 일치한다.
- B-scan line 방향과 bidirectional index가 실제 scanner motion과 일치한다.
- timing mismatch가 기준을 넘으면 START 전 또는 runtime UI에서 명확히 표시된다.

### User-visible Result

- UI의 scan angle, B-scan 수, waveform 정보와 실제 MEMS 동작이 같은 의미를 가진다.

## 10. Phase 7.6: EDFA and Windows Acceptance

### Scope

- EDFA `none`, `manual`, `controlled` 실제 serial path를 검증한다.
- APC setpoint, output ON/OFF, alarm, disconnect, emergency off를 광 안전 절차와 함께 시험한다.
- global START/STOP과 실패 rollback 순서를 실제 장비로 확인한다.
- ATS DMA, FFTW/CUDA, raw/processed storage, UDP, 3D를 동시에 장시간 실행한다.
- Windows GUI subsystem package와 dependency를 최종 정리한다.

### Exit Criteria

- EDFA가 없는 profile이 오류 없이 동작한다.
- controlled EDFA가 safe setpoint와 output state를 실제 응답으로 확인한다.
- Stop/emergency stop 후 MCU, Alazar DMA, EDFA output이 모두 비활성화된다.
- Windows 장시간 시험에서 drop, memory growth, handle leak, UI freeze가 허용 기준 안에 있다.

### User-visible Result

- `FMCW_LiDAR.exe` 하나로 Windows 실장비 운용이 가능하다.

## 11. Phase 7.7: Jetson Integration and Release

### Scope

- 정확한 Jetson/JetPack kernel에 맞는 ATS9371 arm64 driver 지원을 확인한다.
- Qt, ATS API, FFTW, CUDA/cuFFT, serial, UDP, storage를 arm64 target에서 build/link한다.
- local Qt launcher, profile location, log/data directory를 package한다.
- GPU/CPU load, memory, PCIe throughput, NVMe throughput, thermal throttling을 장시간 측정한다.
- Windows와 Jetson이 같은 schema/profile/calibration을 공유하는지 확인한다.

### Exit Criteria

- Jetson에서 GUI를 통해 ATS9371 acquisition과 FFTW/CUDA 측정을 각각 수행한다.
- 장시간 시험에서 thermal throttling과 queue 상태를 기록하고 운용 한계를 문서화한다.
- Windows와 Jetson release package 및 hardware acceptance report가 완성된다.

### User-visible Result

- Jetson에서도 명령행 옵션 없이 local GUI로 동일한 FMCW LiDAR workflow를 운용할 수 있다.

## 12. Audit Finding Traceability

| ID | Finding | Resolution subphase |
| --- | --- | --- |
| P7-001 | ATS9371 12-bit DMA sample shift/centering 오류 | 7.1 |
| P7-002 | QTimer record polling과 늦은 DMA repost | 7.2 |
| P7-003 | FFTW/cuFFT plan이 실제 runtime에서 batch 1 | 7.3A, 7.3B, 7.3C |
| P7-004 | CUDA가 FFT만 수행하고 나머지가 CPU에 남음 | 7.3C |
| P7-009 | 998-record, 200 Hz 조건의 signal-processing deadline 합격 기준 부재 | 7.3D |
| P7-005 | chirp segmentation period와 captured sample profile 불일치 | 7.1, 7.5 |
| P7-006 | raw writer가 per-record metadata/write를 수행 | 7.4 |
| P7-007 | MCU DAC waveform과 configured scan degree가 분리됨 | 7.5 |
| P7-008 | packaged EXE runtime이 fake adapter에 고정됨 | 7.2 |

모든 ID가 해결되고 해당 exit criteria가 통과해야 Phase 7을 완료할 수 있다.

## 13. Required Hardware Inputs

7.1부터 7.4의 simulator/replay 구현은 hardware 없이 진행할 수 있다. 최종 완료에는 아래 입력이 필요하다.

- 7.1/7.2: ATS9371 raw capture와 SDK/vendor utility 비교 결과
- 7.5: actual chirp period samples, trigger offset, stable UP/DOWN range, bandwidth/sweep-rate 측정값
- 7.5: MEMS normalized XY 또는 DAC differential voltage와 physical angle의 calibration 자료
- 7.6: MCU COM port, EDFA serial setting, safe optical output range, 광 파워 미터와 interlock
- 7.7: Jetson model, JetPack version, kernel version, ATS9371 arm64 driver package

## 14. Commit and Push Boundaries

권장 commit 제목은 다음과 같다.

```text
Phase 7.1: correct ATS9371 samples and chirp validation
Phase 7.2: add hardware runtime and DMA batch acquisition
Phase 7.3A: add 998-record processing baseline
Phase 7.3B: optimize FFTW 1996-transform batches
Phase 7.3C: optimize full CUDA processing pipeline
Phase 7.3D: qualify 200 Hz signal processing
Phase 7.4: add DMA-block high-speed recording
Phase 7.5: align laser, MCU waveform, and scan geometry
Phase 7.6: complete Windows hardware acceptance
Phase 7.7: complete Jetson integration and release
```

## 15. Next Action

Current next subphase: **7.5 Laser, MCU, and scan alignment** after the required hardware timing inputs are available.

7.2, 7.3D, and 7.4 hardware acceptance remain blocked. The 7.4 contiguous DMA-block implementation improved CUDA feed throughput but did not make either backend pass the 5 ms end-to-end gate. The next software work should start only with measured chirp/trigger and MCU scan-alignment inputs, or with a focused 7.3D snapshot/ownership latency optimization decision.
