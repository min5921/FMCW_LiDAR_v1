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
8. CPU backend는 FFTW3f, GPU backend는 CUDA/cuFFT를 사용한다. 두 backend는 같은 입력과 출력 계약을 공유한다.
9. real-time queue overflow의 기본 정책은 acquisition `STOP`이다.
10. EDFA는 `none`, `manual`, `controlled`를 지원하며 EDFA가 없어도 acquisition이 가능해야 한다.
11. global START/STOP 하나가 processing, storage, digitizer, MCU, optional EDFA를 함께 제어한다.
12. 각 subphase는 구현, 테스트, 문서 갱신 후 독립 commit/push한다.

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
| 7.2 Hardware runtime and DMA batch | in_progress | EXE에서 실제 ATS9371 연결 및 지속 DMA 수집 | Windows ATS9371 acceptance 필요 |
| 7.3A 998-record processing baseline | pending | 1996 FFT workload와 5 ms deadline 계측 | reference raw로 검증 가능 |
| 7.3B FFTW processing optimization | pending | CPU 1996-transform batch 거리/B-scan/3D | target CPU benchmark 필요 |
| 7.3C CUDA processing optimization | pending | GPU batch 전처리부터 peak/point까지 처리 | local RTX, 이후 Jetson 필요 |
| 7.3D 200 Hz performance acceptance | pending | 998-point B-scan line을 5 ms 안에 완료 | target Windows/Jetson 필요 |
| 7.4 High-speed raw storage | pending | DMA-block recording과 replay | NVMe sustained test 필요 |
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

### 7.3C: CUDA Processing Optimization

#### Scope

- legacy two-slot stream/event 구조를 새 full-period record 계약에 맞게 재구성한다.
- pinned host memory와 persistent device workspace를 사용한다.
- ADC conversion, DC removal, UP/DOWN extraction, polarity, window를 CUDA kernel로 처리한다.
- cuFFT plan-many batch 1996, magnitude dBFS, independent peak search, interpolation, distance/velocity/XYZ를 GPU에서 수행한다.
- H2D, compute, D2H를 두 개 이상의 slot/stream으로 다음 DMA buffer와 overlap한다.
- selected FFT 한 쌍과 998개 peak/point 결과만 host로 복사한다.

#### Exit Criteria

- FFTW와 CUDA의 peak bin, distance, velocity, XYZ, validity가 정의된 tolerance 안에서 일치한다.
- 매 record 또는 UP/DOWN마다 stream synchronize를 호출하지 않는다.
- selected FFT 외 전체 spectrum을 매 batch D2H하지 않는다.
- local RTX와 Jetson에서 steady-state allocation과 memory growth가 없다.

### 7.3D: 200 Hz Real-Time Performance Acceptance

#### Measured Scope

측정 시작은 Alazar DMA completion이고 종료는 998번째 distance/velocity/XYZ와 한 B-scan line snapshot이 완성된 시점이다. 다음 항목을 포함한다.

- ATS sample conversion and ownership copy
- UP/DOWN segmentation, DC removal, polarity, and window
- 1996 real-to-complex FFTs of length 2048
- magnitude dBFS, threshold, peak interpolation, and `NaN` validity
- 998 distance/velocity/XYZ points and B-scan line assembly

Disk write, UDP transmission, and Qt paint time은 이 5 ms signal-processing gate에서 제외하며 각 subsystem acceptance에서 별도로 측정한다.

#### Hard Pass Criteria

- laser 200 kHz, digitizer 1 GS/s, record 4992 samples, records/buffer 998 조건을 사용한다.
- B-scan 200 Hz deadline은 DMA buffer당 `5.00 ms`다.
- warm-up 이후 최소 10분 동안 어떤 batch도 end-to-end processing `5.00 ms`를 초과하지 않는다.
- p50, p95, p99, maximum latency와 deadline miss count를 모두 기록하며 평균값만으로 합격시키지 않는다.
- processing queue가 지속적으로 증가하지 않고 DMA drop, processing drop, stale result가 모두 0이다.
- 각 batch가 정확히 998개의 결과를 생성하고 다음 batch 도착 전에 line 결과가 완성된다.
- FFTW와 CUDA를 각각 시험하며, 두 backend가 각 target platform에서 위 기준을 통과해야 Phase 7.3을 완료한다.

#### User-visible Result

- Processing 페이지에서 현재 backend의 batch latency, 5 ms deadline margin, miss count를 확인할 수 있다.
- 지원하지 못하는 backend/platform 조합은 실시간 가능 상태로 표시하지 않는다.

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

Current software next subphase: **7.3A 998-record processing baseline**.

7.2의 software implementation은 완료됐지만 ATS9371 hardware acceptance는 계속 `in_progress`로 유지한다. Hardware 연결을 기다리는 동안 7.3A simulator/replay baseline을 진행한다.
