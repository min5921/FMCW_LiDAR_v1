# Qt GUI Runtime Requirements

## 1. Purpose

이 문서는 승인된 FMCW LiDAR GUI mockup을 실제 Qt 애플리케이션으로 구동하기 위한 소프트웨어 계약을 정의한다. 화면 모양보다 명령 소유권, 데이터 흐름, 실시간성, 안전 동작을 우선한다.

기준:

- Windows와 Jetson은 같은 Qt Widgets UI와 공통 core를 사용한다.
- 전역 Basic/Advanced 모드는 사용하지 않는다.
- 모든 설정 페이지는 항상 접근 가능하며, 자주 쓰지 않는 항목만 페이지 내부 `Details` 영역으로 접는다.
- UI thread는 장비 I/O, FFT, raw 저장, UDP 송신을 직접 실행하지 않는다.

## 2. Navigation And Ownership

왼쪽 navigation은 다음 8개 항목으로 고정한다.

1. Overview
2. Live View
3. Digitizer
4. Laser / EDFA
5. Scan / MCU
6. Processing
7. Storage / UDP
8. System Log

화면 소유권:

| Page | Owns | Must not duplicate |
|---|---|---|
| Overview | readiness, device status, active profile, session summary, throughput/drop counters | waveform, FFT, B-scan |
| Live View | Time Domain, FFT, Peak Analysis, Distance/Velocity, B-scan, 3D | hardware setup forms |
| Digitizer | supported ATS model, fixed System 1 / Board 1, A or B channel, model-specific sample rate/range, DMA, detailed trigger | scanner Start/Stop |
| Laser / EDFA | laser specification, EDFA none/manual/controlled, detected serial-port selection, 0~30 dBm APC setpoint, actual device readback, output safety control | acquisition Start/Stop |
| Scan / MCU | frame geometry, DMA-derived A-scans/B-scan, operator B-scans/frame, measured DMA rate/frame time, full-frame MCU waveform, M-only B-trigger offset | local scan Start/Stop |
| Processing | FFT backend, window/DC, independent peak threshold/search, segmentation snapshot | FFT spectrum plot |
| Storage / UDP | raw/processed save, path, queue, UDP endpoint/format | acquisition Start/Stop |
| System Log | log filtering, alarms, diagnostics export | duplicated setup controls |

Overview의 `FRAMES` card는 누적 A-scan이나 DMA batch 수 대신 실측 DMA B-scan rate를 `B-scans/frame`으로 나눈 연속적인 raster FPS, 그 역수인 frame time, 처리 완료 frame 수를 표시한다. 이 계산은 1 FPS 미만에서도 1초 정수 frame 카운트로 양자화하지 않는다. `QUEUES` card는 `Signal`, `Raw`, `Result` 대기 batch/block 수와 각 capacity를 구분해 표시한다.

## 3. Global Command Bar

상단 command bar의 시스템 명령은 한 군데에서만 제공한다.

- `Connect` / `Disconnect`
- profile select, load, save
- validation state and pending-change indicator
- global `START`
- global `STOP`
- `Emergency Stop`
- raw save and UDP enabled state indicators

전역 `START` 순서:

1. active config validation 및 pending restart 항목 확인
2. 필수 장비 readiness와 MCU waveform 확인
3. EDFA controlled profile이면 setpoint 적용, output enable, warm-up/ready 확인
4. digitizer AutoDMA arm
5. MCU scan start로 trigger 발생 시작
6. acquisition/processing/storage session을 Running으로 전환

전역 `STOP` 순서:

1. MCU scan stop으로 새 trigger 차단
2. digitizer abort/stop 및 DMA buffer 정리
3. processing/storage queue drain 또는 정책에 따른 중단
4. EDFA controlled profile이면 output off
5. session metadata 종료 및 summary 생성

EDFA output on/off는 광 출력 안전 명령이므로 Laser / EDFA 페이지에도 독립 control을 둔다. 이 control은 시스템 acquisition을 시작하지 않으며, emergency off는 언제나 전역 동작보다 우선한다.

설정 변경 순서:

- peak threshold/search range와 DC removal은 `Apply Processing` 후 다음 frame boundary에서 반영한다.
- board capability, sampling rate, sample point, channel, input, DMA, trigger, FFT backend/length, scan, storage 설정은 Running 중 control을 잠근다.
- restart-required 설정은 global `STOP` 후 수정하고 `Apply Setup`을 누른다.
- `Apply Setup`은 필요 시 disconnect, configure, reconnect까지 수행하고 Ready에서 멈춘다. 안전을 위해 acquisition을 자동 재시작하지 않는다.
- Digitizer sampling rate, input range, impedance는 자동 감지되거나 profile에 저장된 board capability가 제공하는 ComboBox 값만 허용한다.
- Digitizer record samples는 해당 모델의 최소값과 32/128-sample resolution을 강제하는 aligned numeric control을 사용하며, 지원되지 않는 typed value는 config에 확정하지 않는다.
- Alazar System ID와 Board ID는 모두 1로 고정하며 UI에서 변경하지 않는다.
- 실제 Alazar source의 `Board model`은 SDK에서 자동 감지한 모델 번호를 읽기 전용으로 표시하고 별도 진단 화면이나 수동 모델 선택을 두지 않는다. 감지 모델이 바뀌면 같은 페이지의 setup 선택값을 해당 capability로 다시 채운다. Simulator는 profile의 모델을 표시하고 Replay는 raw와 함께 저장된 setup의 모델을 자동 복원한다.
- profile `Load`는 controls만 staging하고 `Apply Setup` 성공 전까지 applied runtime config를 보존한다. 상단 profile 이름은 저장/불러오기 파일명을 반영한다.
- A-scans/B-scan은 records per buffer에서 파생하고 B-scans/frame은 사용자가 설정한다.
- B-scan rate와 period는 Alazar DMA buffer 완료 간격에서 실측하며, measured frame time은 `period * B-scans/frame`으로 계산한다.
- MCU cycle time은 전체 프레임 파형 point 수와 100 kHz point rate에서 별도로 계산한다.
- DMA frame time과 MCU cycle time의 차이가 5%를 넘으면 Scan / MCU 페이지에 mismatch warning을 표시한다.
- B-trigger offset은 MCU sample 단위로 표시하고 `0=원본`, 음수=advance, 양수=delay 의미를 tooltip에 명시한다. 이 값은 X/Y command를 움직이지 않고 업로드 M bit에만 적용하며, 실제 emitted M edge가 acquisition과 좌표의 공통 line anchor다. 변경 시 Apply Setup과 재업로드를 요구한다.
- legacy vector의 감소 line은 B-scan `x_index`에서 한 번만 역배치한다. 3D azimuth/elevation은 동일한 `x_index`/`y_index`에서 계산하여 B-scan과 point cloud의 공간 방향을 일치시키고, 원본 X/Y command는 진단용으로 유지한다.

## 4. Qt Software Boundaries

필수 계층:

- `MainWindow`: navigation, global command bar, status bar만 소유
- page widgets: 화면 구성과 사용자 입력만 소유
- `ApplicationController`: UI 명령을 core operation/config/device service로 전달
- page view models: immutable snapshot을 Qt signal로 page에 전달
- core services: acquisition, processing, storage, UDP, replay를 worker thread에서 실행
- snapshot publishers: UI 갱신 주기에 맞춘 최신 데이터 사본 제공

금지 사항:

- QWidget에서 driver 객체 직접 호출
- UI timer에서 FFT 실행
- acquisition callback에서 plot 갱신
- raw DMA buffer를 UI가 장시간 소유
- plot backlog를 모두 순차 렌더링

Qt signal/slot은 queued connection을 사용한다. 큰 배열은 매번 deep copy하지 않고 immutable shared snapshot 또는 bounded latest-value buffer로 전달한다.

## 5. UI Command Contract

`ApplicationController`는 최소한 다음 비동기 명령을 제공해야 한다.

- `connectSystem()` / `disconnectSystem()`
- `loadProfile(path)` / `saveProfile(path)`
- `validateConfig(config)` / `applyConfig(config)`
- `startSystem()` / `stopSystem()` / `emergencyStop()`
- `setEdfaOutput(enabled)` / `setEdfaSetpoint(value, unit)`
- `uploadMcuWaveform(frames)` / `reconnectMcu()`
- `captureSegmentationSnapshot()`
- `setRawRecording(enabled)` / `setUdpEnabled(enabled)`
- `freezeLiveView(enabled)` / `saveCurrentView(path)`

각 명령은 즉시 UI thread를 반환하고, `accepted`, `completed`, `failed` 상태와 사용자 행동이 포함된 오류를 signal로 보고한다. 중복 START, START 중 STOP, STOP 중 START는 state machine으로 직렬화한다.

## 6. Snapshot Contract

UI는 다음 snapshot만 읽는다.

| Snapshot | Typical rate | Required contents |
|---|---:|---|
| `SystemStatusSnapshot` | 10-20 Hz | operation state, device readiness, active revision, alarms, queue/drop counters |
| `WaveformSnapshot` | 20-60 Hz | frame id, timestamp, channel, downsampled full-period samples, trigger/segment indices |
| `FftSnapshot` | 20-60 Hz | up/down magnitude arrays, frequency/bin axis, detected peak markers |
| `PeakAnalysisSnapshot` | per completed scan line | A-scan index, up/down peak index, magnitude, validity |
| `DistanceVelocitySnapshot` | per completed scan line | pixel, distance, velocity, validity |
| `BScanSnapshot` | per complete raster frame, render capped at 5-30 Hz | X pixel by B-scan line forward-depth heatmap, frame index, min/max, validity mask |
| `PointCloudSnapshot` | per complete raster frame, render capped at 5-30 Hz | XYZ, intensity/velocity color scalar, frame/scan revision |
| `SegmentationSnapshot` | on demand | one frozen full-period frame and editable segment overlay metadata |

UI가 느리면 intermediate snapshot은 버리고 최신 snapshot을 표시한다. raw writer와 processed writer는 이 표시 정책과 무관하게 별도 bounded queue를 사용한다.

## 7. Chirp Segmentation Snapshot

Processing 페이지의 chirp segmentation graph는 실시간 plot이 아니다.

- `Capture Snapshot`은 Running 상태에서 최신 full-period raw frame을 복사해 화면에 고정한다.
- Idle 상태에서는 마지막 cached frame 또는 replay frame을 사용한다.
- 사용 가능한 frame이 없으면 숨은 acquisition을 시작하지 않고 `No frame available`을 표시한다.
- Full-period length는 Digitizer의 `sample_point`에서 가져오며 중복 입력하지 않는다. 사용자는 period start와 UP/DOWN 구간의 start/length를 입력하고, segment end는 `start + length`로 계산한다. 값을 바꾸면 고정된 frame 위 overlay와 validation 결과만 즉시 갱신한다.
- boundary가 record/period 밖으로 나가거나 겹치면 global START를 막는다.
- Live View의 Time Domain plot만 연속 실시간 waveform을 표시한다.

## 8. Peak Detection

Processing page의 필수 runtime 설정:

- `peak_threshold_db`
- `peak_search_start_bin`
- `peak_search_end_bin`

- candidate는 threshold를 초과하며 global search range 안에 있어야 한다.
- UP과 DOWN은 각 A-scan에서 독립적으로 최대 peak를 검출한다.
- 현재 version은 peak interpolation이나 sub-bin estimation 없이 최대 정수 bin을 사용한다.
- search end는 inclusive이며 Nyquist bin을 제외한다. FFT length 2048의 설정 및 Live FFT 표시 범위는 `0..1023`이다.
- 이전 A-scan의 peak index를 추적, 유지, 재탐색하지 않는다.
- threshold를 초과하는 candidate가 없으면 해당 chirp peak와 측정 결과를 invalid로 기록하고, 실수형 값은 `NaN`으로 표시·저장한다.

Peak Analysis 탭은 FFT spectrum을 다시 그리지 않는다. 다음 두 plot과 상태 요약만 표시한다.

- Peak Index vs A-scan
- Peak Value dB vs A-scan

## 9. Live Plot Rules

- Time Domain과 FFT는 최신 frame 우선이다.
- 200 Hz acquisition과 모든 A-scan 신호처리는 계속 수행하되, Live View는 `ui.plot_update_hz`에 맞춰 최신 snapshot만 표시한다. 중간 UI frame을 합치는 것은 DMA/processing drop으로 집계하지 않는다.
- Qt runtime은 현재 보이는 Live tab 한 개만 GUI thread로 전달한다. 숨은 Time Domain, FFT, Peak, Distance, B-scan, 3D tab은 GUI thread 전달, snapshot binding, repaint를 수행하지 않는다. 표시 중인 line plot도 immutable snapshot vector를 공유하며 Qt container로 전체 복사하지 않는다.
- dense Time Domain trace는 원본 snapshot을 변경하지 않고 화면 가로 픽셀별 min/max 수직선을 batch draw하여 narrow peak와 노이즈 범위를 보존한다. FFT trace는 표시 bin만 integer polyline으로 batch draw하며 live trace에는 antialiasing을 적용하지 않는다.
- Live View는 1초 집계로 DMA 생성 Hz, GUI 전달 Hz, 실제 data paint Hz, 표시에서 생략된 DMA sequence, paint 전에 합쳐진 GUI update, set/paint p95와 paint max를 보여준다. 이 값은 display 진단이며 acquisition drop과 구분한다.
- Windows 기본 plot 갱신은 최대 60 Hz, Jetson은 기본 30 Hz로 제한하고 숫자와 상태 telemetry 갱신은 10 Hz로 분리한다. 정지/오류 플래그는 plot cadence에서 가볍게 확인하되 percentile 등 상태 집계는 10 Hz에서만 수행하며, acquisition 및 signal-processing cadence와 독립적이어야 한다.
- Peak Analysis와 Distance/Velocity는 scan line 완료 시 publish한다. B-scan은 모든 line이 채워진 complete raster frame에서만 publish한다.
- B-scan은 `X Pixel x B Scan` forward-depth (`point.x`) heatmap으로 표시한다.
- B-scan과 3D point cloud는 partial line을 표시하지 않고 모든 B-scan line이 완료된 raster frame만 교체한다. 다음 frame 수집 중에는 직전 complete frame을 유지한다.
- freeze는 acquisition/processing/storage를 멈추지 않고 화면 snapshot만 고정한다.
- auto/manual range, cursor readout, plot save를 공통 plot toolbar로 제공한다.
- 3D는 Qt/OpenGL-backed point renderer를 사용하고 acquisition과 독립 rate로 갱신한다. 좌표계는 `X forward, Y left, Z up`으로 고정하고 축 표시를 끄고 켤 수 있다.
- 3D spatial bounds는 새 session의 첫 complete frame에서 한 번 맞춘 뒤 고정하고, 사용자가 Fit View를 실행할 때만 다시 계산한다. 물체 거리 변화만으로 view center/scale이 움직여서는 안 된다.
- 3D 기능은 Phase 6의 실제 동작 tab으로 제공하며 빈 tab이나 disabled placeholder를 노출하지 않는다.
- Processing의 `Batch Diagnostics` 상세 숫자는 Processing page가 보일 때만 Qt label에 반영한다. 다른 page에서는 마지막 표시값을 유지하되 signal processing, latency 계측, queue 감시와 STOP 정책은 계속 동작한다.

## 10. Selected A-scan And Phase 6 Runtime

- Live Time Domain and FFT display the selected zero-based A-scan record from each Alazar DMA buffer.
- Selected A-scan은 가로 slider와 숫자 입력을 함께 제공하고 양방향으로 동기화한다. slider drag는 release 시 한 번만 runtime selection을 갱신하여 command queue 적체를 만들지 않는다.
- Changing the selected A-scan is display-only and applies while acquisition is running; it does not restart or reconfigure the digitizer.
- Peak Analysis, Distance/Velocity, B-scan, 3D, UDP, and raw/processed storage continue to consume all A-scans.
- The 3D tab consumes complete immutable point-cloud frames only. `ui.point_cloud_update_hz` caps rendering independently from acquisition and never exposes partial raster assembly.
- The Qt/OpenGL-backed renderer supports rotate, pan, zoom, reset, point size, optional XYZ axes, color mode, freeze, PNG capture, and CSV point export.
- UDP uses a dedicated bounded sender queue. Socket I/O never runs on the UI, acquisition, or processing thread.
- Global STOP first stops device input, drains processing, then finalizes UDP and storage workers.
- Selecting a raw replay file restores the adjacent setup YAML, including the saved board profile and scanner/processing values. The UI stages the values for `Apply Setup`, disables MCU/EDFA outputs, UDP, and recursive recording, and falls back to the old JSON snapshot for earlier recordings.

## 11. Phase Ownership

Phase 4가 먼저 제공해야 하는 항목:

- FFTW/CUDA 공통 processing result contract
- up/down extraction, FFT, independent peak detection, distance/velocity
- line accumulator와 B-scan matrix
- async raw/processed writer 및 replay
- UI용 immutable processing snapshot publisher

Phase 5가 구현해야 하는 항목:

- MainWindow, navigation, global command bar
- ApplicationController와 page view models
- Overview 및 7개 setup/log page
- Live View의 Time Domain, FFT, Peak Analysis, Distance/Velocity, B-scan
- segmentation snapshot interaction
- profile validation/pending change UX
- global START/STOP/EDFA safety UX

Phase 6가 구현해야 하는 항목:

- 3D point cloud renderer
- UDP sender/receiver 도구 확장
- simulator fault injection과 diagnostics export

## 12. Acceptance Criteria

- 사용자는 한 개의 global START/STOP만으로 digitizer와 scanner session을 운용할 수 있다.
- digitizer가 arm되기 전에 MCU trigger가 시작되지 않는다.
- STOP 시 scanner trigger가 digitizer보다 먼저 정지한다.
- EDFA none profile과 MCU disabled profile이 정상 동작한다.
- Live View를 닫거나 freeze해도 acquisition과 raw 저장이 계속된다.
- FFT spectrum은 Live View FFT 탭에만 존재한다.
- Peak Analysis에서 threshold/search 변경이 적용된 frame revision을 확인할 수 있다.
- segmentation 설정 화면은 고정 snapshot임을 명확히 표시하며 실시간 plot으로 오인되지 않는다.
- 잘못된 설정, 장비 not-ready, queue overflow는 global START 또는 session 지속을 안전하게 차단한다.
