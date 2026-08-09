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

Distance에는 calibration scale/offset을, velocity에는 wavelength와 scale/offset을 적용한다. 이후 azimuth에 `x_angle_offset_deg`, elevation에 `y_angle_offset_deg`를 더하고 Cartesian 좌표를 계산한다. 좌표계는 일반적인 ROS/RViz LiDAR convention인 `+X` forward, `+Y` left, `+Z` up의 right-handed frame을 사용한다.

```text
azimuth   = scan_x_angle + calibration.x_angle_offset_deg
elevation = scan_y_angle + calibration.y_angle_offset_deg
x         = R cos(elevation) cos(azimuth)
y         = R cos(elevation) sin(azimuth)
z         = R sin(elevation)
intensity = (up_peak_db + down_peak_db) / 2
velocity  = calibrated_velocity
```

`scan_position.x_angle_deg`는 azimuth, `scan_position.y_angle_deg`는 elevation 의미로 사용한다. legacy vector에서는 감소 line을 한 번 정렬한 B-scan `x_index`로 azimuth를, `y_index`로 elevation을 계산한다. 원본 X/Y command와 trajectory sample index는 provenance로 저장하지만 Cartesian 변환에서 command 방향을 다시 적용하지 않는다. 필드 이름은 저장 형식 호환을 위해 유지하며 별도의 보간 또는 좌표 추정은 수행하지 않는다.

## 6. UI Snapshots

`ProcessingSnapshotStore`는 다음 최신 immutable snapshot을 제공한다.

- `WaveformSnapshot`: ADC full-scale 기준 full-period waveform과 up/down range
- `FftSnapshot`: up/down magnitude와 independently detected peak
- `ScanLineSnapshot`: X pixel별 peak index/value, radial distance, velocity, forward depth, validity
- `BScanSnapshot`: `width x height` Cartesian forward-depth (`point.x`) matrix와 validity mask

Waveform/FFT는 processed frame마다 교체한다. Scan line은 한 B-scan line의 모든 X pixel이 채워졌을 때 publish한다. B-scan과 point cloud는 모든 B-scan line이 채워진 complete raster frame에서만 원자적으로 publish하며, 다음 frame 조립 중에는 직전 complete snapshot을 유지한다. UI가 느리면 과거 snapshot을 누적하지 않고 최신 shared snapshot을 읽는다.

### Selected Display And 3D Snapshot

- `BScanSnapshot` and `PointCloudSnapshot` carry one complete raster. Every published snapshot has `completed_lines == height` and `complete == true`; partial raster work buffers are never exposed to either viewer.
- After publication, the visible 3D viewer owns an independent display-only post-processor. It can median-fuse the most recent one to five complete organized frames and insert edge-gated Y rows at 2x or 4x density. The immutable source snapshots and all storage/UDP payloads remain unchanged.
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

Phase 7.2 changes the `ProcessingService` queue item to an immutable `RawFrameBatch`. Phase 7.3B processes that batch through reusable 64-record FFTW chunks, each containing 128 interleaved UP/DOWN transforms, and returns every peak and point result together. The 998-record qualification workload uses 16 FFT batch executions while preserving parity with the single-record reference. Only the selected record retains full spectra for UI publication. Queue capacity and high-water telemetry are measured in DMA batches. The compatibility `enqueue(RawFramePtr)` entry point remains only for existing tests and narrow callers.

Phase 7.3D의 real-time gate는 laser 200 kHz, digitizer 1 GS/s, 4992 samples/record, 998 records/buffer, UP/DOWN 각 2048 samples 조건이다. DMA completion부터 998 point와 한 B-scan line 완성까지 모든 signal processing이 5.00 ms 안에 끝나야 하며 평균이 아니라 maximum deadline miss로 합격을 판정한다.

CUDA mode uses `processing/cuda/cuda_signal_pipeline.cu` for the complete batch path rather than using CUDA as an FFT-only helper. ATS acquisition exposes the native left-aligned 12-bit `uint16` DMA block as one contiguous external `RawFrameBatch` view, so CUDA can enqueue one direct H2D copy without a CPU-wide sample conversion or duplicate payload copy. A dedicated H2D event releases the ATS DMA lease for repost before the full processing event completes. The pipeline uses one asynchronous slot, downloads only compact results plus the selected spectra, and retains only metadata plus the selected time-domain record for UI publication. Owned simulator/replay batches use persistent pinned staging as the compatibility path.

기본 파일:

- `<stem>.raw.0000.bin`, `<stem>.raw.0001.bin`, ...
- `<stem>.raw.json`
- `<stem>.setup.yaml`
- `<stem>.processed.bin`
- `<stem>.processed.json`

Raw binary는 stream header 뒤에 frame record를 순차 기록한다. 각 record에는 frame/config/trigger/scan/optical/segment metadata와 full-period `int16` payload가 들어간다. Raw frame을 segment로 자른 뒤 저장하지 않는다.

Raw streams record their explicit `SampleFormat`. Simulator input may use signed `int16`, while ATS acquisition preserves native `UnsignedOffsetBinary12LeftAligned` codes in the same two-byte payload width. Raw v3 stores one compact metadata table plus one contiguous payload without converting native ATS samples and adds trajectory provenance plus an intrinsic ROS coordinate contract. FFTW, CUDA, waveform snapshots, storage, and replay all decode the descriptor through the same sample-format contract. Raw v1/v2 is accepted only after an adjacent sidecar explicitly identifies the standard coordinate frame; ambiguous legacy recordings require conversion.

Phase 7.4 changes production acquisition ownership: one `RawFrameBatch` references a contiguous DMA payload and each record exposes a sample view into that allocation. Raw storage enqueues and writes once per DMA block. CUDA copies native external DMA storage directly to its persistent device input; only non-external input uses one contiguous pinned staging copy. Raw/result writers have separate queues and workers. Raw parts are preallocated, split only between complete blocks, truncated to committed bytes, and guarded by free-space preflight.

Processed binary에는 up/down FFT magnitude, peak와 validity, distance, velocity, XYZ, intensity, point velocity, latency, processing revision을 기록한다. UDP point payload도 동일한 `x, y, z, intensity, velocity` 순서를 사용한다.

Raw writer는 stream open 시점에 `<stem>.setup.yaml`을 먼저 기록하며 JSON sidecar의 `setup_file`과 `coordinate_frame`이 이를 참조한다. MCU legacy X/Y/M 파일을 사용하는 session은 적용된 waveform을 같은 session directory에 보관하고 setup의 상대 경로를 그 사본으로 바꾼다.

JSON sidecar에는 다음을 기록한다.

- session/profile/platform/application version
- config schema version과 config snapshot
- start/end UTC timestamp
- stream descriptor와 data file 목록
- frames written
- completed flag와 stop reason

`split_file_size_gb`를 넘으면 다음 numbered raw part를 연다. 한 frame은 두 part로 분할하지 않으므로 part 크기는 최대 한 frame만큼 설정값을 초과할 수 있다.

Processed binary format v2 additionally records the trajectory sample index, source X/Y commands, calibrated azimuth/elevation angles, detected fast axis/direction, coordinate source, and command coordinates copied into the final point. Point-cloud CSV exports `x_forward_m, y_left_m, z_up_m, intensity_db, velocity_mps, scan_x_command, scan_y_command`. UDP packet v2 keeps five floats per point and fixes XYZ as `X forward, Y left, Z up`; legacy-axis v1 is rejected.

## 9. Replay

`RawReplayReader`는 raw header와 frame record를 검증해 원래 `RawFrame`을 복원한다. `*.raw.0000.bin`을 열면 뒤의 numbered part를 자동으로 탐색해 연속 읽는다.

Reader는 vector allocation 전에 block의 record/sample 곱셈, exact header size, 남은 파일 byte, 최대 100,000 records와 256 MiB payload를 검사한다. 손상 파일의 비정상 크기와 `bad_alloc`/`length_error`는 GUI 종료 대신 명시적 replay error가 된다. Replay runtime은 저장된 spatial `x_index/y_index`와 command provenance는 보존하지만 azimuth/elevation은 현재 적용된 setup 범위에서 다시 계산한다. 따라서 과거 command-derived angle metadata가 B-scan과 point cloud 방향을 갈라놓지 않는다.

Replay frame은 hardware frame과 같은 `SignalProcessor` 또는 `ProcessingService` 입력으로 사용한다. 별도의 replay 전용 FFT/peak 계산을 만들지 않는다.

GUI에서 raw part를 선택하면 같은 stem의 `.setup.yaml`을 우선 읽고, 과거 recording은 `.raw.json`의 `config_snapshot`으로 fallback한다. 저장 당시의 ATS board profile, digitizer, laser/chirp, scanner geometry, calibration, processing 설정을 pending controls에 복원한다. 실제 MCU/EDFA 출력, UDP, raw/processed 재기록은 안전을 위해 비활성화하며 사용자가 `Apply Setup`으로 재생 설정을 확정한다.

## 10. Failure Policy

- processing queue overflow: processing stop request
- storage queue overflow: storage stop request
- raw/processed write failure: writer failure stop request
- FFT backend failure: processing stop request

상위 session controller는 이 상태를 `OperationController`의 queue overflow, writer failure, device error stop cause로 변환하고 global STOP sequence를 수행해야 한다.
