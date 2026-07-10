# Processing And Storage Pipeline

## 1. Scope

Phase 4 pipeline은 single-channel `RawFrame` 한 개를 다음 결과로 변환한다.

1. full-period raw record 검증
2. up/down segment extraction
3. DC removal, optional normalization, window, zero padding
4. FFTW 또는 CUDA/cuFFT R2C FFT
5. dBFS magnitude와 peak detection/tracking
6. distance, velocity, XYZ 변환
7. scan line 및 B-scan Z matrix 누적
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

Profile의 `processing.fft_backend`와 전달된 backend 종류가 다르면 configure를 거부한다. FFTW 또는 CUDA가 build에 없거나 runtime device가 없으면 Start 전에 actionable error를 반환한다.

## 3. Segment Preprocessing

- ADC `int16`를 `[-1, 1)` 범위로 변환한다.
- `dc_removal=true`이면 segment 평균을 제거한다.
- `normalize=true`이면 segment 최대 절댓값으로 정규화한다.
- configured window를 실제 segment 길이에 적용한다.
- `segment_fft_length`까지 zero padding한다.
- down segment는 `invert_down` polarity를 적용한다.

Magnitude는 one-sided amplitude와 window coherent sum을 보정해 dBFS로 계산한다.

```text
magnitude_db = 20 log10(max(2 |FFT[k]| / sum(window), 1e-10))
```

## 4. Peak Detection And Tracking

Peak candidate는 configured search range에서 threshold 이상인 최대 magnitude bin이다. 좌우 bin이 있으면 parabolic interpolation으로 fractional bin을 계산한다.

Tracking은 한 B-scan line 안의 acquisition 순서에서 up/down chirp를 독립적으로 수행한다.

- `Detected`: line의 첫 valid peak 또는 tracking disabled
- `Tracked`: 이전 valid peak와의 차이가 max delta 이내
- `Reacquired`: local reacquire range 또는 global candidate로 track 재설정
- `HeldLast`: 마지막 bin은 표시하지만 measurement validity는 false
- `Lost`: candidate를 승인할 수 없음

`stop_acquisition` lost policy는 `ProcessedFrame.stop_requested`와 `ProcessingServiceStatus.stop_requested`를 설정한다. `HeldLast`와 `Lost` 결과는 distance/velocity/UDP의 valid measurement로 사용하지 않는다.

## 5. Distance And Velocity

```text
bin_frequency = sample_rate / fft_length
f_up          = up_peak_bin * bin_frequency
f_down        = down_peak_bin * bin_frequency
distance      = c * (f_up + f_down) / (4 * sweep_rate)
velocity      = wavelength * (f_up - f_down) / 4
```

이후 laser correction과 calibration scale/offset을 적용한다. 좌표계는 `+Z` forward, `+X` horizontal, `+Y` vertical이다.

```text
x = R cos(y_angle) sin(x_angle)
y = R sin(y_angle)
z = R cos(y_angle) cos(x_angle)
```

## 6. UI Snapshots

`ProcessingSnapshotStore`는 다음 최신 immutable snapshot을 제공한다.

- `WaveformSnapshot`: normalized full-period waveform과 up/down range
- `FftSnapshot`: up/down magnitude와 detected/tracked peak
- `ScanLineSnapshot`: X pixel별 peak index/value, distance, velocity, Z, tracking state, validity
- `BScanSnapshot`: `width x height` Z matrix와 validity mask

Waveform/FFT는 processed frame마다 교체한다. Scan line과 B-scan은 모든 X pixel이 채워졌을 때 publish한다. UI가 느리면 과거 snapshot을 누적하지 않고 최신 shared snapshot을 읽는다.

## 7. Processing Service

`ProcessingService`는 bounded raw queue와 한 개 worker thread를 소유한다.

- enqueue는 FFT 완료를 기다리지 않는다.
- runtime peak setting은 다음 frame boundary에서 적용한다.
- 적용된 `processing_config_revision`을 processed frame과 snapshot에 기록한다.
- overflow, backend failure, peak stop policy는 stop reason을 보존하고 새 enqueue를 거부한다.
- processed callback은 storage/UDP queue에 넘기는 non-blocking callback으로만 사용한다.

## 8. Binary Storage

기본 파일:

- `<stem>.raw.0000.bin`, `<stem>.raw.0001.bin`, ...
- `<stem>.raw.json`
- `<stem>.processed.bin`
- `<stem>.processed.json`

Raw binary는 stream header 뒤에 frame record를 순차 기록한다. 각 record에는 frame/config/trigger/scan/optical/segment metadata와 full-period `int16` payload가 들어간다. Raw frame을 segment로 자른 뒤 저장하지 않는다.

Processed binary에는 up/down FFT magnitude, peak와 tracking state, distance, velocity, XYZ, validity, latency, processing revision을 기록한다.

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
- peak lost with stop policy: processing stop request

상위 session controller는 이 상태를 `OperationController`의 queue overflow, writer failure, device error stop cause로 변환하고 global STOP sequence를 수행해야 한다.
