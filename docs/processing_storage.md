# Processing And Storage Pipeline

## 1. Scope

Phase 4 pipeline은 single-channel `RawFrame` 한 개를 다음 결과로 변환한다.

1. full-period raw record 검증
2. up/down segment extraction
3. fixed ADC full-scale conversion, optional DC removal, window, zero padding
4. FFTW 또는 CUDA/cuFFT R2C FFT
5. dBFS magnitude와 independent peak detection
6. distance, velocity, XYZ 변환
7. scan line 및 B-scan forward-depth matrix 누적
8. immutable UI snapshot 발행
9. raw/processed binary 비동기 저장

Acquisition 측에서는 immutable `RawFramePtr`를 bounded queue에 enqueue한다. FFT, plot snapshot 생성, disk write는 acquisition 호출자 thread에서 실행하지 않는다.

## 2. FFT Backends

`IFftBackend`의 입력과 출력은 모든 backend에서 동일하다.

- input: real `float`, `length x batch`
- output: complex `float`, `(length / 2 + 1) x batch`
- FFT scaling: backend output은 unnormalized
- plan: length와 batch가 바뀌지 않으면 재사용

Backend:

- `FftwBackend`: FFTW3 single-precision `fftwf` R2C
- `CudaFftBackend`: CUDA runtime buffer와 cuFFT R2C

FFTW와 CUDA/cuFFT의 차이는 연산을 수행하는 processor와 memory execution path뿐이다. ADC conversion, segmentation, DC removal, polarity, window, zero padding, one-sided dBFS scaling, threshold comparison, maximum-bin selection, distance/velocity, calibration, XYZ, validity 순서와 수식은 동일해야 한다. Backend별로 다른 window, scaling, peak rule, 보간, calibration을 사용하지 않는다. Full CUDA pipeline도 이 algorithm contract를 GPU kernel로 그대로 실행할 뿐 별도 신호처리 알고리즘을 정의하지 않는다.

Implementation ownership:

- `src/processing/cpu/fftw_backend.cpp`: active FFTW implementation
- `src/processing/cuda/cuda_fft_backend.cu`: active CUDA runtime and cuFFT implementation
- `src/processing/fft_backends.cpp`: backend factory and unavailable-backend stubs only
- Files under `legacy/` are reference material and are never compiled or called by the runtime.

Profile의 `processing.fft_backend`와 전달된 backend 종류가 다르면 configure를 거부한다. FFTW 또는 CUDA가 build에 없거나 runtime device가 없으면 Start 전에 actionable error를 반환한다.

## 3. Segment Preprocessing

- ADC `int16`를 `[-1, 1)` 범위로 변환한다.
- `dc_removal=true`이면 segment 평균을 제거한다.
- configured window를 실제 segment 길이에 적용한다.
- `segment_fft_length`까지 zero padding한다.
- down segment는 `invert_down` polarity를 적용한다.

Magnitude는 one-sided amplitude와 window coherent sum을 보정해 dBFS로 계산한다.

```text
magnitude_db = 20 log10(max(2 |FFT[k]| / sum(window), 1e-10))
```

## 4. Peak Detection

Peak candidate는 configured search range에서 threshold를 초과하는 최대 magnitude bin이다. 현재 version은 peak interpolation이나 sub-bin estimation을 수행하지 않는다. `peak_bin`은 선택된 `discrete_bin`을 float로 표현한 값이며 유효할 때 항상 정수값이다.

UP과 DOWN peak는 각 A-scan에서 독립적으로 검출한다. 이전 A-scan의 peak index를 추적하거나 유지하지 않는다. 한쪽이라도 threshold를 초과하는 peak가 없으면 해당 A-scan의 measurement validity는 false이며 실수형 peak, distance, velocity, intensity, XYZ는 `NaN`이다. 정수형 `discrete_bin`은 `-1`을 유지한다.

## 5. Distance And Velocity

```text
bin_frequency = sample_rate / fft_length
f_up          = up_peak_bin * bin_frequency
f_down        = down_peak_bin * bin_frequency
distance      = c * (f_up + f_down) / (8 * sweep_bandwidth_hz * sweep_rate_hz)
velocity      = calibration.velocity_wavelength * (f_up - f_down) / 4
```

Distance에는 calibration scale/offset을, velocity에는 wavelength와 scale/offset을 적용한다. 이후 scanner angle에 `x_angle_offset_deg`, `y_angle_offset_deg`를 더하고 Cartesian 좌표를 계산한다. 좌표계와 축 순서는 legacy `Dis_value` 및 UDP `PointXYZIV`와 동일한 `+X` lateral, `+Y` forward, `+Z` vertical이다. Positive scanner Y angle은 legacy 부호 규칙에 따라 negative Z 방향이다.

```text
x_angle = scan_x_angle + calibration.x_angle_offset_deg
y_angle = scan_y_angle + calibration.y_angle_offset_deg
x         = R cos(y_angle) sin(x_angle)
y         = R cos(y_angle) cos(x_angle)
z         = -R sin(y_angle)
intensity = (up_peak_db + down_peak_db) / 2
velocity  = calibrated_velocity
```

이 식은 legacy가 사용한 `legacy_x = 90 deg - x_angle`, `legacy_y = 90 deg + y_angle` 변환을 최종 scanner angle 기준으로 전개한 식이다. 별도의 보간 또는 좌표 추정은 수행하지 않는다.

## 6. UI Snapshots

`ProcessingSnapshotStore`는 다음 최신 immutable snapshot을 제공한다.

- `WaveformSnapshot`: ADC full-scale 기준 full-period waveform과 up/down range
- `FftSnapshot`: up/down magnitude와 independently detected peak
- `ScanLineSnapshot`: X pixel별 peak index/value, radial distance, velocity, forward depth, validity
- `BScanSnapshot`: `width x height` Cartesian forward-depth (`point.y`) matrix와 validity mask

Waveform/FFT는 processed frame마다 교체한다. Scan line과 B-scan은 모든 X pixel이 채워졌을 때 publish한다. UI가 느리면 과거 snapshot을 누적하지 않고 최신 shared snapshot을 읽는다.

### Selected Display And 3D Snapshot

- `PointCloudSnapshot` carries raster XYZ/intensity/velocity points, completed-line count, frame index, and completion state.
- `WaveformSnapshot` and `FftSnapshot` are published only for the configured zero-based `record_index_in_buffer`.
- This is the legacy-compatible display selection. Every A-scan still passes through FFT, peak measurement, scan-line/B-scan/point-cloud aggregation, raw/processed storage, and UDP assembly.

## 7. Processing Service

`ProcessingService`는 bounded raw queue와 한 개 worker thread를 소유한다.

- enqueue는 FFT 완료를 기다리지 않는다.
- runtime peak setting은 다음 frame boundary에서 적용한다.
- 적용된 `processing_config_revision`을 processed frame과 snapshot에 기록한다.
- overflow와 backend failure는 stop reason을 보존하고 새 enqueue를 거부한다.
- processed callback은 storage/UDP queue에 넘기는 non-blocking callback으로만 사용한다.

## 8. Binary Storage

Phase 7.2 changes the `ProcessingService` queue item to an immutable `RawFrameBatch`. The worker currently processes every record in order with the existing single-record `SignalProcessor`; Phase 7.3B replaces those repeated FFT calls with a preallocated FFTW plan-many path for 998 records and 1996 UP/DOWN transforms while preserving identical per-record peak and point results. Queue capacity and high-water telemetry are measured in DMA batches. The compatibility `enqueue(RawFramePtr)` entry point remains only for existing tests and narrow callers.

Phase 7.3D의 real-time gate는 laser 200 kHz, digitizer 1 GS/s, 4992 samples/record, 998 records/buffer, UP/DOWN 각 2048 samples 조건이다. DMA completion부터 998 point와 한 B-scan line 완성까지 모든 signal processing이 5.00 ms 안에 끝나야 하며 평균이 아니라 maximum deadline miss로 합격을 판정한다.

기본 파일:

- `<stem>.raw.0000.bin`, `<stem>.raw.0001.bin`, ...
- `<stem>.raw.json`
- `<stem>.processed.bin`
- `<stem>.processed.json`

Raw binary는 stream header 뒤에 frame record를 순차 기록한다. 각 record에는 frame/config/trigger/scan/optical/segment metadata와 full-period `int16` payload가 들어간다. Raw frame을 segment로 자른 뒤 저장하지 않는다.

Phase 7.1 결정으로 raw format v1과 `RawFrame`은 SDK의 left-aligned padding bit를 제거하고 signed full-scale로 변환한 `int16` 계약을 유지한다. 따라서 기존 FFT와 replay 파일의 의미는 바뀌지 않는다. 원본 Alazar DMA `uint16` 블록을 복사 없이 보존하는 고속 포맷은 Phase 7.4에서 raw format v2로 추가하며, v1 replay 호환성을 함께 유지한다.

Processed binary에는 up/down FFT magnitude, peak와 validity, distance, velocity, XYZ, intensity, point velocity, latency, processing revision을 기록한다. UDP point payload도 동일한 `x, y, z, intensity, velocity` 순서를 사용한다.

JSON sidecar에는 다음을 기록한다.

- session/profile/platform/application version
- config schema version과 config snapshot
- start/end UTC timestamp
- stream descriptor와 data file 목록
- frames written
- completed flag와 stop reason

`split_file_size_gb`를 넘으면 다음 numbered raw part를 연다. 한 frame은 두 part로 분할하지 않으므로 part 크기는 최대 한 frame만큼 설정값을 초과할 수 있다.

## 9. Replay

`RawReplayReader`는 raw header와 frame record를 검증해 원래 `RawFrame`을 복원한다. `*.raw.0000.bin`을 열면 뒤의 numbered part를 자동으로 탐색해 연속 읽는다.

Replay frame은 hardware frame과 같은 `SignalProcessor` 또는 `ProcessingService` 입력으로 사용한다. 별도의 replay 전용 FFT/peak 계산을 만들지 않는다.

## 10. Failure Policy

- processing queue overflow: processing stop request
- storage queue overflow: storage stop request
- raw/processed write failure: writer failure stop request
- FFT backend failure: processing stop request

상위 session controller는 이 상태를 `OperationController`의 queue overflow, writer failure, device error stop cause로 변환하고 global STOP sequence를 수행해야 한다.
