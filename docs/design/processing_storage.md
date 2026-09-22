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

FFTW와 CUDA/cuFFT의 차이는 연산을 수행하는 processor와 memory execution path뿐이다. ADC conversion, segmentation, DC removal, polarity, window, zero padding, one-sided dBFS scaling, threshold comparison, maximum-bin selection, 3-point quadratic refinement, distance/velocity, calibration, XYZ, validity 순서와 수식은 동일해야 한다. Backend별로 다른 window, scaling, peak rule, refinement, calibration을 사용하지 않는다. Full CUDA pipeline도 이 algorithm contract를 GPU kernel로 그대로 실행할 뿐 별도 신호처리 알고리즘을 정의하지 않는다.

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

Peak candidate는 configured search range에서 가장 큰 magnitude bin이다. Strict threshold는 quadratic refinement 전의 center-bin dB 값에 먼저 적용한다. Center가 threshold를 초과하고 search range 안에서 좌우 이웃을 모두 사용할 수 있으면 세 dB 값 `L`, `C`, `R`로 다음 3-point quadratic vertex를 계산한다.

```text
curvature = L - 2*C + R
offset    = 0.5 * (L - R) / curvature
peak_bin  = discrete_bin + offset
peak_db   = C - 0.25 * (L - R) * offset
```

`curvature < -1e-6`이고 `offset`이 `[-0.5, 0.5]`일 때만 refinement를 적용한다. 경계 bin, 평탄하거나 위로 볼록한 세 점, 비정상 수치에서는 `peak_bin = discrete_bin`, `peak_db = C`로 안전하게 fallback한다. `discrete_bin`은 실제 최대 FFT bin의 정수 진단값이며, `peak_bin`은 거리/속도 계산에 사용하는 fractional estimate다. 이 보정은 FFT bin 사이의 국소 최대 위치를 추정하지만 광학적 거리 분해능 자체를 높이는 것은 아니다.

UP과 DOWN peak는 각 A-scan에서 독립적으로 검출한다. 이전 A-scan의 peak index를 추적하거나 유지하지 않는다. 한쪽이라도 center-bin threshold를 초과하지 못하면 해당 A-scan의 measurement validity는 false이며 실수형 peak, distance, velocity, intensity, XYZ는 `NaN`이다. 정수형 `discrete_bin`은 `-1`을 유지한다.

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
- This is the legacy-compatible display selection. Every A-scan still passes through FFT, peak measurement, scan-line/B-scan/point-cloud aggregation, raw storage, complete-frame point-cloud storage, and UDP assembly.

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
- `<stem>.pointcloud.bin`
- `<stem>.pointcloud.json`

Raw binary는 stream header 뒤에 frame record를 순차 기록한다. 각 record에는 frame/config/trigger/scan/optical/segment metadata와 full-period `int16` payload가 들어간다. Raw frame을 segment로 자른 뒤 저장하지 않는다.

Raw streams record their explicit `SampleFormat`. Simulator input may use signed `int16`, while ATS acquisition preserves native `UnsignedOffsetBinary12LeftAligned` codes in the same two-byte payload width. Raw v3 stores one compact metadata table plus one contiguous payload without converting native ATS samples and adds trajectory provenance plus an intrinsic ROS coordinate contract. FFTW, CUDA, waveform snapshots, storage, and replay all decode the descriptor through the same sample-format contract. Raw v1/v2 is accepted only after an adjacent sidecar explicitly identifies the standard coordinate frame; ambiguous legacy recordings require conversion.

Phase 7.4 changes production acquisition ownership: one `RawFrameBatch` references a contiguous DMA payload and each record exposes a sample view into that allocation. Raw storage enqueues and writes once per DMA block. CUDA copies native external DMA storage directly to its persistent device input; only non-external input uses one contiguous pinned staging copy. Raw/result writers have separate queues and workers. Raw parts are preallocated, split only between complete blocks, truncated to committed bytes, and guarded by free-space preflight.

Point-cloud binary에는 complete raster frame 단위로 XYZIV와 validity만 기록한다. UDP point payload도 동일한 `x, y, z, intensity, velocity` 순서를 사용한다.

Raw writer는 stream open 시점에 `<stem>.setup.yaml`을 먼저 기록하며 JSON sidecar의 `setup_file`과 `coordinate_frame`이 이를 참조한다. MCU legacy X/Y/M 파일을 사용하는 session은 적용된 waveform을 같은 session directory에 보관하고 setup의 상대 경로를 그 사본으로 바꾼다.

JSON sidecar에는 다음을 기록한다.

- session/profile/platform/application version
- config schema version과 config snapshot
- start/end UTC timestamp
- stream descriptor와 data file 목록
- frames written
- completed flag와 stop reason

`split_file_size_gb`를 넘으면 다음 numbered raw part를 연다. 한 frame은 두 part로 분할하지 않으므로 part 크기는 최대 한 frame만큼 설정값을 초과할 수 있다.

Point-cloud binary format v1 records one complete organized raster per block. Its per-point payload is only `x, y, z, intensity, velocity, valid`; setup and processing revision metadata remain at frame/session level. Point-cloud CSV exports `x_forward_m, y_left_m, z_up_m, intensity_db, velocity_mps, scan_x_command, scan_y_command`. UDP packet v2 keeps five floats per point and fixes XYZ as `X forward, Y left, Z up`; legacy-axis v1 is rejected.

## 9. Replay

`RawReplayReader`는 raw header와 frame record를 검증해 원래 `RawFrame`을 복원한다. `*.raw.0000.bin`을 열면 뒤의 numbered part를 자동으로 탐색해 연속 읽는다.

Reader는 vector allocation 전에 block의 record/sample 곱셈, exact header size, 남은 파일 byte, 최대 100,000 records와 256 MiB payload를 검사한다. 손상 파일의 비정상 크기와 `bad_alloc`/`length_error`는 GUI 종료 대신 명시적 replay error가 된다. Replay runtime은 저장된 spatial `x_index/y_index`와 command provenance는 보존하지만 azimuth/elevation은 현재 적용된 setup 범위에서 다시 계산한다. 따라서 과거 command-derived angle metadata가 B-scan과 point cloud 방향을 갈라놓지 않는다.

Replay frame은 hardware frame과 같은 `SignalProcessor` 또는 `ProcessingService` 입력으로 사용한다. 별도의 replay 전용 FFT/peak 계산을 만들지 않는다.

GUI에서 raw part를 선택하면 같은 stem의 `.setup.yaml`을 우선 읽고, 과거 recording은 `.raw.json`의 `config_snapshot`으로 fallback한다. 저장 당시의 ATS board profile, digitizer, laser/chirp, scanner geometry, calibration, processing 설정을 pending controls에 복원한다. 실제 MCU/EDFA 출력, UDP, raw/processed 재기록은 안전을 위해 비활성화하며 사용자가 `Apply Setup`으로 재생 설정을 확정한다.

### 9.1 Point-cloud file replay

`Live View > 3D Point Cloud`의 별도 replay toolbar는 acquisition START/STOP과 독립적으로
완성된 point cloud를 직접 표시한다. 실제 acquisition이 시작되면 파일 replay timer를
중지하고 display history를 비워 서로 다른 source가 temporal fusion되지 않게 한다.

지원 입력은 다음과 같다.

- 프로젝트 전용 `<stem>.pointcloud.bin`: 기존 FMCW `FMCWPCD1` 또는 Waymo 전용
  `WaymoPCD2` frame을 순서로 재생한다. Play/Pause, single-step, rewind, loop,
  0.5-60 FPS를 지원한다.
- PCD v0.7 `DATA ascii` 또는 `DATA binary`: `x`, `y`, `z`가 필수이고 `intensity`,
  `velocity`, `valid`는 선택 사항이다. `WIDTH`와 `HEIGHT`가 있으면 organized layout을
  보존한다. `DATA binary_compressed`는 지원하지 않는다.
- `.xyz`, `.xyzi`, `.csv`, `.txt`: header가 없으면 열 순서를 `X Y Z [I [V]]`로
  해석한다. Header가 있으면 `x/y/z`, `intensity`, `velocity` 또는 GUI export 이름인
  `x_forward_m/y_left_m/z_up_m`, `intensity_db`, `velocity_mps`를 인식한다.

외부 파일의 좌표를 회전하거나 축 교환하지 않는다. 모든 입력 XYZ는 meter 단위의
`X forward, Y left, Z up`으로 해석한다. XYZ-only 입력의 intensity/velocity는 `NaN`으로
유지하며 값을 만들지 않는다. 해당 color mode에 값이 없을 때 renderer만 distance color로
fallback한다. PCD/text는 한 개의 static frame이고 `FMCWPCD1`과 `WaymoPCD2`만
multi-frame replay이다.
입력 검증 한도는 frame당 20,000,000 points와 512 MiB payload이다.

#### 9.1.1 Waymo segment conversion

`tools/convert_waymo_zip.py`는 repository의 exported Waymo segment ZIP을 압축 해제 없이
순차 읽어 하나의 `WaymoPCD2` multi-frame file로 변환한다. 각 source frame마다
`TOP`, `FRONT`, `SIDE_LEFT`, `SIDE_RIGHT`, `REAR` LiDAR의 return 1/2를 모두 병합한다.
입력 좌표는 이미 Waymo vehicle frame인 `X forward, Y left, Z up` meter이므로 추가 축
교환이나 pose 변환을 하지 않는다.

Waymo intensity는 `tanh` 전처리하고 elongation과 함께 저장한다. Export에 없는 velocity는
기록하지 않는다. NLZ point는 삭제하지 않고 함께 저장하며 frame별
개수를 `<output>.pointcloud.json` manifest에 기록한다. 변환기는 `.partial` file에 먼저
기록하고 전체 frame 구조를 검증한 뒤 최종 파일명으로 교체한다.

```powershell
python tools/convert_waymo_zip.py data/samples/<segment>.zip
```

기본 출력은 같은 폴더의 `<segment>.merged.pointcloud.bin`이다. 각 Waymo source frame은
unorganized `width = point_count`, `height = 1` point-cloud frame 하나가 된다.

## 10. Failure Policy

- processing queue overflow: processing stop request
- storage queue overflow: storage stop request
- raw/processed write failure: writer failure stop request
- FFT backend failure: processing stop request

실제 Qt 앱의 `RuntimeWorker`가 오류를 받아 global STOP을 수행한다. `StopResult`는 hardware/acquisition/processing/UDP/storage 단계 오류를 모으고, `ApplicationController`의 실패 신호와 로그로 전달한다. `OperationController`는 초기 prototype 테스트용이며 실제 앱 제어 경로가 아니다.

Raw/point-cloud worker를 모두 join한 후 같은 세션 결과로 최종화한다. Overflow, writer 오류/예외, 외부 실패 정지는 `completed=false`다. 나중 writer의 finalize 실패도 먼저 닫힌 peer의 metadata를 실패로 낮춘다. 디스크 자체가 metadata 쓰기를 거부하면 실패 표시는 보장할 수 없으므로 runtime 오류와 부분 파일을 함께 확인해야 한다.

일반 Stop은 대기 processing batch를 버리고 현재 진행 작업을 종료한다. 자연스러운 raw replay EOF는 처리 큐를 비운 뒤 저장을 닫아 마지막 결과를 보존한다. 둘 다 불완전 raster는 완성 point cloud로 내보내지 않는다.

### v2 읽기 및 설정 이력

`AsyncPointCloudReplay`의 전용 worker가 파일 해석과 한 프레임 사전 읽기를 담당한다. GUI에는 `Pending` 또는 준비된 frame을 반환하며, 파일 교체/rewind/close의 세대 번호로 이전 결과를 차단한다. PCD 해석기는 검출 입력에서도 공유하고, 검출 전용 I/elongation 기본값 변환만 adapter에 둔다. 이 변환이 저장/화면의 missing-value 규약을 바꾸지는 않는다.

Raw 처리 설정 이력의 파일 구조·적용 경계·덮어쓰기 규칙은 `data_contract.md`와 `runtime_contract_v2.md`를 따른다. 초기 setup만 복원하는 기능과 실행 중 처리 revision을 재현하는 기능은 별개다.
