# FMCW LiDAR System Requirements

## 1. Project Goal

기존 FMCW LiDAR 코드를 Windows 버전과 Jetson/Linux 버전으로 나누고, 사용자가 장비 설정부터 실시간 측정, 신호처리, 3D 시각화, 데이터 저장까지 하나의 UI에서 다룰 수 있는 시스템으로 개선한다.

핵심 방향은 다음과 같다.

- Windows 버전: AlazarTech 보드 설정, 실시간 수집, 분석, 저장, DSO 스타일 UI 중심
- Jetson/Linux 버전: 현장 운용, AlazarTech 보드 직접 수집, UDP 송신/수신, Qt 기반 경량 UI 중심
- 공통 코어: 설정 파일, FFT 처리 인터페이스, 데이터 포맷, UDP 패킷 규격, raw 저장 포맷 공유

제품 품질 우선순위:

1. 실시간 수집 안정성
2. 설정 실수 방지
3. 장비 상태를 한눈에 알 수 있는 UI
4. raw data를 잃지 않는 저장 구조
5. Windows와 Jetson에서 같은 처리 결과를 내는 공통 코어
6. 장비 없이도 replay/simulator로 개발 가능한 구조

## 2. Target Versions

### 2.1 Windows Version

목적:

- 실험실 장비 제어 및 디버깅
- AlazarTech DSO처럼 보드 파라미터를 UI에서 직접 설정
- 실시간 time-domain, FFT, peak, distance/velocity, 3D point cloud 확인
- raw data 및 처리 결과 저장

주요 구성:

- UI: Qt 6 기반 상업용 장비 소프트웨어 느낌의 데스크톱 앱
- Digitizer: AlazarTech ATS SDK
- FFT: GPU CUDA FFT와 CPU FFT 선택 가능
- Plot: 2D plot, heatmap, 3D point cloud
- Network: UDP 송신/수신 설정 및 모니터링
- Storage: raw buffer, processed frame, config snapshot 저장
- Optical chain: Laser와 optional EDFA 제어

### 2.2 Jetson/Linux Version

목적:

- 현장/임베디드 운용
- 실시간 획득, FFT 처리, UDP 스트리밍
- Jetson 장비에서도 로컬 Qt UI 표시

주요 구성:

- UI:
  - Qt 6 기반 로컬 UI
  - 실시간성이 필요한 화면은 가벼운 OpenGL 기반 rendering 우선
  - 원격 dashboard/headless service는 후속 옵션으로만 검토
- Digitizer:
  - AlazarTech 보드를 Jetson에서 직접 사용
- FFT:
  - CUDA FFT
- CPU FFT fallback: FFTW
- Network:
  - UDP point cloud 송신
  - 원격 설정 수신 가능 구조
- Storage:
  - raw data 저장
  - frame 단위 point cloud 저장
- Optical chain:
  - EDFA 없이도 동작
  - EDFA가 연결된 profile에서는 상태 확인 및 on/off 제어

구현/검증 필요:

- Jetson AlazarTech SDK/driver 설치 및 빌드 절차
- Jetson PCIe 연결 방식 및 DMA 성능 검증
- CUDA/cuFFT 버전과 타깃 Jetson GPU 메모리 한계

## 3. Functional Requirements

### 3.1 UI Requirements

UI는 기존 단순 실시간 plot 화면에서 장비 운용용 패널로 확장한다.

필수 화면:

- Overview
- Live View: Time Domain, FFT, Peak Analysis, Distance/Velocity, B-scan, 3D
- Digitizer
- Laser / EDFA
- Scan / MCU
- Processing
- Storage / UDP
- System Log

필수 사용자 흐름:

1. 장비 연결 확인
2. 설정 profile 선택 또는 새 설정 작성
3. 설정값 검증
4. 설정 validation 및 chirp segmentation snapshot 확인
5. Start acquisition
6. 실시간 plot/point cloud 확인
7. raw/processed 저장 여부 확인
8. Stop acquisition
9. session summary 확인
10. 저장 데이터 replay

UI 결정:

- Windows와 Jetson 모두 Qt 6 기반 UI로 개발한다.
- 기본 UI는 복잡한 장비 폼과 데스크톱 계측기 구성을 우선해 Qt Widgets로 구현하고, 실시간 2D/3D rendering은 QOpenGLWidget 계열로 분리한다.
- 3D/point cloud viewer는 Qt UI 안에서 가볍게 동작하는 OpenGL 기반 renderer를 우선한다.
- Web dashboard, Unity/Unreal, WPF/WinUI, Avalonia는 초기 버전 범위에서 제외한다.

UI 선택 기준:

- 장비 설정 패널이 복잡하므로 폼, 테이블, 탭, 로그, 파일 다이얼로그가 자연스러워야 한다.
- 실시간 plot과 3D point cloud가 안정적으로 갱신되어야 한다.
- Windows 버전은 상업용 계측기처럼 정돈된 패널형 UI를 우선한다.
- Jetson 버전도 로컬 Qt UI를 띄우며, 화면 구성은 Windows와 최대한 동일하게 유지한다.
- UI 프레임워크는 acquisition/processing core와 분리되어야 한다.

사용자 친화 요구사항:

- 모든 설정값에는 단위와 허용 범위를 표시한다.
- 잘못된 설정은 Start 전에 막고, 왜 막혔는지 설명한다.
- 실행 중 바꿀 수 있는 값과 재시작이 필요한 값을 구분한다.
- 자주 쓰는 설정은 preset/profile로 저장한다.
- Start/Stop, Save, UDP, Laser, Scanner, Digitizer 상태를 색상과 텍스트로 동시에 표시한다.
- 오류 메시지는 장비명, 원인, 사용자가 할 다음 행동을 포함한다.
- 전역 Basic/Advanced 모드는 사용하지 않고 모든 설정 페이지를 같은 navigation에서 제공한다.
- 자주 쓰지 않는 설정은 해당 페이지 내부 `Details` 영역으로 접되 접근 권한이나 별도 UI mode로 숨기지 않는다.
- 시스템 acquisition과 scanner 동작은 상단의 한 개 global START/STOP으로 제어한다.
- 실시간 화면에서는 프레임률, drop count, 저장 속도, GPU/CPU 처리 시간, UDP 송신률을 항상 볼 수 있어야 한다.
- 로그는 Info, Warning, Error, Critical로 필터링할 수 있어야 한다.
- 측정 session마다 설정 snapshot, 시작/종료 시간, 저장 파일 경로를 자동 기록한다.

현재 결정:

- Windows UI: Qt 6
- Jetson/Linux UI: Qt 6 로컬 UI
- 3D viewer: Qt/OpenGL 기반 경량 point cloud renderer

화면 책임과 Qt runtime 계약은 `docs/gui_runtime_requirements.md`를 따른다. Overview는 상태와 session summary만 표시하고, 실시간 plot은 Live View가 전담한다. 실행 중 변경 가능한 값과 restart-required 값은 각 field 옆 상태로 구분한다.

### 3.2 Digitizer Board Setup

UI에서 다음 항목을 설정할 수 있어야 한다.

- Board model: `ATS9371`로 고정하고 SDK 연결 시 실제 board kind를 검증
- System ID / Board ID: `1 / 1`로 고정하며 UI 입력에서 제외
- Channel select: A 또는 B
- Sampling rate
- Sample point
- Records per buffer
- A-scan count: `Records per buffer`에서 파생
- B-scan count: 한 프레임의 Y line 수와 B-scan matrix 높이를 위해 사용자가 설정
- Input range
- Coupling: ATS9371 analog input은 DC로 고정
- Impedance
- Trigger source: `TRIG IN`, External TTL, DC coupling으로 고정
- Trigger slope
- Trigger threshold: 사용자 설정에서 제외. ATS SDK level 인자는 legacy와 동일한 내부 code `150`, external range는 `ETR_TTL`로 고정
- Trigger delay
- Laser trigger mode: `up_chirp_only`로 고정
- Full-period acquisition: 항상 enable
- Chirp period samples
- Pre-trigger samples
- Post-trigger samples
- Up segment start/end sample
- Down segment start/end sample
- Segment guard samples
- Timeout
- DMA buffer count
- Acquisition mode: single, continuous, finite frames

설정 검증:

- `sample_point`는 사용자가 Alazar record 길이로 직접 입력하며 laser sweep rate로부터 자동 계산하지 않는다.
- ATS9371 record 길이는 최소 256 samples이고 128 samples의 배수여야 한다.
- `sample_point`가 `chirp_period_samples`보다 크면 Warning을 표시하되 START를 차단하지 않는다.
- v1 hardware acquisition에서는 trigger 1개가 전체 up+down chirp period 1개를 의미한다.
- legacy `up_down_pair`는 hardware profile이나 UI에 노출하지 않고 replay/import 변환에서만 읽는다.
- `up_chirp_only` 모드의 record length는 설정된 full chirp period와 UP/DOWN segment를 포함해야 하며 추가 margin은 선택 사항이다.
- up/down segment 범위는 record 내부에 있어야 하며 서로 겹치지 않아야 한다.
- up/down segment 길이는 같은 FFT length를 사용하거나 padding/resampling 정책이 명확해야 한다.
- `B-scan count`는 scan setup의 Y line count와 일치해야 한다.
- A-scan count는 `records_per_buffer`와 항상 같아야 하며 별도 입력값으로 관리하지 않는다.
- sampling rate 선택값은 ATS9371 internal clock이 지원하는 discrete 값으로 제한한다.
- ATS9371 record/pre-trigger alignment는 128 samples, NPT pre-trigger 최대값은 8176 samples, 최소 post-trigger는 64 samples, single-channel trigger delay alignment는 16 samples를 사용한다.
- 초기 버전에서는 A+B 동시 수집을 지원하지 않는다.
- 선택 가능한 채널은 단일 채널 A 또는 단일 채널 B로 제한한다.
- selected channel이 바뀌면 buffer size, FFT batch, 저장 포맷을 다시 계산한다.
- DMA buffer 크기가 시스템 메모리/GPU 메모리 한계를 넘으면 시작을 막는다.
- trigger 설정이 external trigger일 때 trigger status를 UI에 표시한다.
- trigger period jitter와 missed trigger를 측정해 UI와 log에 표시한다.

실행 중 변경 정책:

- 실행 중 변경 가능: peak threshold/search range, DC removal, plot range, color map, segment overlay 표시
- Preview 중 변경 가능: up/down segment start/end, guard samples
- 재시작 필요: board capability, sampling rate, sample point, channel, record count, chirp period samples, DMA buffer count, FFT backend/length, raw/processed save 조건, UDP endpoint
- 재시작 필요 설정은 Running 중 잠그고, STOP 후 `Apply Setup`으로 disconnect/configure/reconnect한다. 자동 START는 하지 않는다.

현재 코드 기준 관련 항목:

- `sample_rate`
- `sample_point`
- `A_scanNum`
- `B_scannum`
- `ConfigureBoard()`
- `AcquireData()`

### 3.3 Laser Setup

UI의 Laser Specification은 거리 계산에 필요한 두 항목만 제공한다.

- Sweep bandwidth: `Hz`
- Sweep rate: full triangular waveform repetition rate, `Hz`

주의:

- legacy 코드는 `Sweeprate`를 읽고도 거리 계산에서 `200000`을 하드코딩했지만, 개선 버전은 두 UI 설정값을 직접 사용한다.
- 거리식은 `c * (f_up + f_down) / (8 * bandwidth * sweep_rate)`를 사용한다.
- bandwidth와 sweep rate는 sample point, chirp period, segmentation 또는 trigger timing을 결정하지 않는다.
- bandwidth/period/sweep slope 상호 일치 Warning은 생성하지 않는다.
- velocity wavelength와 scale/offset은 Laser UI가 아니라 calibration profile에서 관리한다.
- 레이저 trigger는 up/down chirp마다 각각 발생하지 않고 up chirp 시작점에서만 발생하는 모드를 지원한다.
- up chirp trigger 1개에 대해 digitizer는 전체 up+down chirp 주기를 캡처해야 한다.
- UI는 trigger 위치, up 구간, down 구간, guard 구간을 waveform 위에 표시해야 한다.
- 레이저 관련 설정은 session metadata에 반드시 저장한다.
- 레이저 enable 상태와 실제 acquisition 상태가 UI에서 분리되어 표시되어야 한다.

### 3.3.1 EDFA Setup

EDFA는 광 출력 향상을 위한 optional 장비로 취급한다. 시스템은 EDFA가 없어도 정상적으로 acquisition, processing, 저장, UDP 송신이 가능해야 하며, 사용자는 profile 또는 UI에서 EDFA 사용 여부를 선택할 수 있어야 한다.

EDFA 운용 모드:

- `none`: EDFA 없이 동작. EDFA 미연결은 오류가 아니라 정상 상태로 표시한다.
- `manual`: EDFA는 외부 장비에서 수동 설정하고, UI에는 사용 여부와 메모만 기록한다.
- `controlled`: UI에서 EDFA serial/FTDI 연결, output on/off, optical output setpoint를 제어한다.

UI 설정 항목:

- EDFA enable/use toggle
- EDFA mode: none, manual, controlled
- EDFA required before start: true/false
- Serial/FTDI port
- Baud rate, parity, stop bit, timeout
- Output on/off
- Optical output setpoint: mW 또는 dBm
- Output limit/min/max
- Control mode: device command set이 지원하는 ACC/APC/AGC 계열 모드
- Warm-up delay
- Interlock/status readback
- Alarm/reset

설정 검증:

- `edfa.mode=none`이면 EDFA 연결 실패가 Start를 막지 않는다.
- `edfa.mode=manual`이면 output setpoint는 session metadata에 기록하되 장비 명령은 보내지 않는다.
- `edfa.mode=controlled`이고 `edfa.required_before_start=true`이면 EDFA 연결, status OK, output ready가 아니면 Start를 막는다.
- optical output setpoint는 장비와 안전 한계 내 값으로 제한한다.
- EDFA output on/off 상태는 laser enable 상태와 별도로 표시한다.
- EDFA 설정 변경은 frame metadata에 변경 시점과 frame ID를 기록한다.

안전 요구사항:

- Emergency stop 시 EDFA output을 off로 전환한다.
- EDFA alarm, over-current, over-temperature, interlock open 상태가 감지되면 UI에 Critical로 표시한다.
- EDFA 통신이 끊긴 경우 acquisition을 계속할지 중지할지 profile에서 선택할 수 있어야 한다.
- EDFA가 없는 profile에서는 Health panel에 `Not used` 또는 `Bypassed`로 표시하고 오류 색상을 사용하지 않는다.

legacy 참고:

- EDFA vendor tool, driver, command PDF는 `legacy/EDFA-Amplifier-V20240219`에 보관한다.
- 새 구현에서는 vendor controller exe를 직접 의존하지 않고 command PDF를 기준으로 `src/drivers/edfa`에 driver/protocol을 정리한다.

### 3.4 Scan Setup

스캐너/미러/MTI 관련 설정을 UI에서 제어한다.

- X start angle
- X end angle
- Y start angle
- Y end angle
- Scan direction
- Bidirectional scan enable
- A-scan count: Digitizer의 `Records per buffer`에서 읽기 전용으로 표시
- B-scans / frame: 사용자가 직접 설정
- Positions / frame: `records_per_buffer * B-scans_per_frame`으로 계산하여 읽기 전용 표시
- DMA B-scan rate/period: Alazar DMA buffer 완료 timestamp 간격에서 실측하여 읽기 전용 표시
- Measured frame time: `DMA buffer period * B-scans_per_frame`으로 계산하여 읽기 전용 표시
- Trigger shift
- Scanner port
- Scanner sample rate
- Scanner waveform upload/reconnect/readiness

스캔 UX 요구사항:

- scanner 연결 상태와 응답 상태를 구분해서 표시한다.
- Scan / MCU 페이지에는 별도 Start/Stop을 두지 않고 global START/STOP과 연동 상태만 표시한다.
- Start acquisition 전에 scanner ready, digitizer ready, trigger ready를 모두 확인한다.
- 현재 MCU firmware의 TIM6 point rate는 100 kHz이며 MCU cycle time은 `full_frame_waveform_points / 100 kHz`로 계산한다.
- MCU에는 `A-scans_per_B-scan * B-scans_per_frame` 크기의 한 프레임 전체 파형을 업로드한다.
- legacy waveform과 동일하게 각 B-scan 시작점에서만 marker를 출력하고, 모든 waveform point에서 marker를 켜지 않는다.
- MCU cycle time은 scanner 파형 검증값이며 Alazar DMA B-scan 속도를 대신하지 않는다.
- 실측 DMA frame time과 MCU waveform cycle time 차이가 5%를 넘으면 UI에 timing mismatch warning을 표시한다.
- 스캔 패턴 preview를 2D로 표시한다.
- bidirectional scan일 때 짝수/홀수 line의 X 방향 반전을 UI에 표시한다.
- Stop 순서는 scanner stop, digitizer abort, buffer flush 순서로 안정화한다.
- Emergency stop 버튼을 제공하고 scanner/laser/digitizer 상태를 즉시 안전 상태로 돌린다.

현재 코드 기준 관련 항목:

- `mti_raster.cpp`
- `MTI_Init`
- `MTI_StartLinearRaster`
- `MTI_Stop`
- `x_start_angle`
- `x_end_angle`
- `y_start_angle`
- `y_end_angle`
- `Direction`

### 3.5 Signal Processing

신호처리는 공통 인터페이스 아래에서 GPU FFT와 CPU FFT를 선택할 수 있어야 한다.

처리 흐름:

1. Raw full-period ADC buffer 입력
2. Trigger timestamp와 chirp period 검증
3. Up/down segment extraction
4. 고정 ADC full-scale 변환과 optional DC removal
5. Window 적용
6. FFT
7. Magnitude dB 변환
8. Peak detection
9. UP/DOWN independent peak validity 판정
10. Distance calculation
11. Velocity calculation
12. XYZ point 변환
13. Heatmap/point cloud 갱신

### 3.5.1 Chirp Segmentation

신규 레이저 운용 방식에서는 trigger가 up chirp 시작점에서만 발생한다. 따라서 digitizer는 trigger 1개마다 전체 up+down chirp period를 받고, 소프트웨어가 record 내부에서 up 구간과 down 구간을 잘라 사용한다.

필수 요구사항:

- `chirp_segmentation.mode`: hardware acquisition에서는 `up_chirp_only`로 고정
- `legacy_pair`: 과거 raw data replay/import 변환기에서만 허용
- `trigger_to_period_offset`: trigger 이후 full period 시작 sample offset
- `chirp_period_samples`: full up+down period 전체 sample 수
- `up_segment.start_sample`, `up_segment.end_sample_exclusive`
- `down_segment.start_sample`, `down_segment.end_sample_exclusive`
- `guard_samples`: chirp 전환부와 불안정 구간 제외 sample 수
- `segment_fft_length`: FFT 입력 길이
- `segment_window`: Hann, Hamming, Blackman, Rectangular 등
- `segment_polarity`: up/down beat sign 또는 velocity sign 보정

UI 요구사항:

- Processing 페이지는 사용자가 요청한 한 개 full-period frame을 고정 snapshot으로 표시하고 trigger, up segment, down segment, guard zone을 색상 overlay로 표시한다.
- segment start/end는 sample index와 시간 단위(us/ns)를 함께 보여준다.
- 사용자가 고정 snapshot에서 segment boundary를 조정하면 overlay와 validation 결과를 즉시 갱신한다.
- 연속 실시간 waveform은 Live View의 Time Domain 탭에서만 표시한다.
- segment가 record 밖으로 나가거나 서로 겹치면 Start를 막는다.
- trigger period jitter, missed period, segment clipping count를 diagnostics에 기록한다.

저장/재생 요구사항:

- raw 저장은 segment로 자르기 전 full-period buffer를 보존한다.
- processed 저장은 적용된 segmentation 설정과 up/down segment별 결과를 metadata에 포함한다.
- replay 모드는 저장된 full-period raw buffer에 동일한 segmentation 설정을 적용해야 한다.
- segmentation 설정 변경 이력은 session metadata에 frame range와 함께 남긴다.
- `RawFrame` 1개는 단일 채널에서 up 시작 trigger 1회로 획득한 full up+down period record 1개로 정의한다.
- sample segment는 `[start_sample, end_sample_exclusive)` 규칙을 사용하며 자세한 계약은 `docs/data_contract.md`를 따른다.

FFT 모드:

- GPU FFT: CUDA/cuFFT 사용
- CPU FFT: FFTW 사용

CPU FFT 요구사항:

- GPU 없는 PC에서도 동작
- 디버깅 기준 결과 생성
- GPU FFT 결과와 비교 가능한 테스트 제공
- 작은 샘플 데이터로 unit test 가능
- FFTW plan 생성/재사용 정책을 명확히 하고, 실행 중 plan 재생성을 최소화한다.

실시간 처리 요구사항:

- acquisition thread는 UI, UDP, disk write를 기다리지 않아야 한다.
- FFT backend는 동일한 입력/출력 구조를 사용해 GPU/CPU를 런타임에서 선택할 수 있어야 한다.
- GPU FFT 실패 시 기본 정책은 acquisition stop으로 한다.
- FFTW fallback은 디버그/검증 모드에서만 수동 선택한다.
- processing queue 길이, 처리 지연, drop count를 실시간 표시한다.
- peak detection threshold와 search range는 실행 중 변경 가능해야 한다.
- 각 A-scan의 UP/DOWN peak는 이전 A-scan과 독립적으로 search range 안의 최대값을 검출한다.
- threshold 이상 peak가 없으면 이전 값을 유지하지 않고 해당 결과를 invalid로 기록한다.
- 설정 변경이 처리 결과에 반영된 frame 번호를 기록한다.
- raw replay 모드에서도 동일한 processing pipeline을 사용한다.
- GPU FFT와 CPU FFT 결과 차이를 검증하는 regression test를 제공한다.

처리 지연 목표:

- UI plot update: 20-60 Hz 범위에서 설정 가능
- acquisition loop: DMA buffer 처리 지연을 최소화하고 block을 피한다.
- 3D point cloud update: 필요 시 5-30 Hz로 throttling한다.
- raw 저장: 별도 writer thread에서 처리하고 acquisition thread를 막지 않는다.

### 3.6 2D Plotting

필수 plot:

- Raw time-domain waveform
- FFT magnitude
- Peak index per A-scan
- Peak dB value per A-scan
- Distance vs pixel
- Velocity vs pixel
- B-scan heatmap

Live View plot 소유권:

- FFT spectrum은 FFT 탭에서만 표시한다.
- Peak Analysis는 FFT를 중복 표시하지 않고 Peak Index vs A-scan과 Peak Value dB vs A-scan을 표시한다.
- B-scan은 `X Pixel x B Scan` Z heatmap으로 표시하며 distance trend line으로 대체하지 않는다.

UI 기능:

- Auto scale
- Manual axis range
- Cursor readout
- Peak marker
- Frame freeze
- Save current plot
- Plot update rate 표시

실시간 plot 요구사항:

- plot은 전체 raw buffer를 매 frame 그리지 않고 필요 시 downsample한다.
- UI 렌더링 속도와 acquisition frame rate를 분리한다.
- 오래된 frame을 모두 그리려 하지 않고 최신 frame 우선 정책을 둔다.
- 사용자가 freeze를 누르면 acquisition은 계속 돌고 화면만 정지한다.
- cursor가 가리키는 sample index, FFT bin, distance 값을 표시한다.

### 3.7 3D Plotting

3D plotting은 point cloud viewer로 구현한다.

필수 기능:

- XYZ point cloud 표시
- Intensity color map
- Velocity color map
- Distance color map
- Rotate/pan/zoom
- Reset camera
- Point size 조절
- Frame freeze
- Current frame save
- Accumulate frames option

실시간 3D 요구사항:

- point 수가 많을 때 decimation 또는 level-of-detail를 적용한다.
- 3D viewer update rate를 acquisition rate와 독립적으로 설정한다.
- 3D rendering이 느려져도 acquisition/processing/storage는 계속 동작해야 한다.
- color map 범위는 auto/manual을 모두 지원한다.
- point cloud 좌표계, 단위, 카메라 방향을 UI에 표시한다.
- 저장된 raw 또는 processed frame을 replay하여 동일한 3D viewer에서 확인할 수 있어야 한다.

권장 구현:

- Qt/OpenGL 기반 경량 point cloud renderer
- 대용량 point cloud는 GPU vertex buffer와 decimation을 우선 사용한다.
- VTK/Open3D/Three.js/Unity/Unreal은 초기 버전에서는 제외하고, 필요 시 후속 viewer plugin으로 검토한다.

### 3.8 UDP Communication

UDP 설정을 UI에서 수정할 수 있어야 한다.

설정 항목:

- Enable/disable UDP
- Target IP
- Target port
- Packet point count
- Packet format version
- Frame ID
- Segment count
- Timestamp
- Point fields: x, y, z, intensity, velocity
- Byte order
- Drop count
- Send FPS

현재 패킷 구조:

- magic
- frame number
- total segments
- segment index
- point count
- timestamp
- point array

개선 요구사항:

- UDP 패킷 규격 문서화
- 패킷 버전 필드 추가 검토
- 수신기 예제 제공
- 송신 실패/partial send 로그 표시
- UDP 송신은 acquisition thread와 분리한다.
- UDP queue 길이와 drop count를 표시한다.
- 네트워크가 느려질 때 최신 frame 우선, 전체 frame 보존, 송신 중지 중 정책을 선택할 수 있어야 한다.
- 패킷에는 config/profile ID 또는 frame metadata 참조 ID를 포함하는 방안을 검토한다.
- 수신기에서 frame 재조립 실패를 감지할 수 있도록 segment index와 total segments를 검증한다.
- endian, float format, coordinate convention을 문서화한다.

### 3.9 Raw Data Save

Raw data 저장 기능을 추가한다.

저장 대상:

- ADC raw buffer
- Frame metadata
- Board setup
- Laser setup
- EDFA setup/status
- Scan setup
- Processing setup
- Timestamp

저장 모드:

- Manual save current frame
- Continuous recording
- N-frame recording
- Triggered recording
- Ring buffer pre/post recording

실시간 저장 요구사항:

- raw writer는 acquisition thread와 분리된 비동기 writer thread로 동작한다.
- 저장 queue 사용량, disk throughput, dropped frame count를 UI에 표시한다.
- 저장 시작 전에 예상 데이터율과 남은 디스크 공간을 계산한다.
- 예상 raw 데이터율은 `record_length * bytes_per_sample * trigger_rate`를 기본으로 계산하고 DMA padding과 file header overhead를 별도 표시한다.
- 파일은 일정 크기 또는 일정 frame 수마다 자동 분할한다.
- 저장 중 설정 변경이 발생하면 metadata에 변경 시점과 frame ID를 기록한다.
- raw 저장 실패 시 기본 정책은 acquisition stop으로 한다.
- 저장 실패로 stop된 경우 마지막 정상 저장 frame, 실패 파일 경로, disk throughput, queue 상태를 session log에 기록한다.
- raw 저장과 processed 저장을 독립적으로 켜고 끌 수 있어야 한다.
- raw 저장 기본 정책은 고속 binary streaming이다.
- raw writer는 큰 연속 block 단위로 쓰고, metadata는 별도 JSON sidecar로 저장한다.
- 파일 header에는 magic, version, endian, sample type, channel, sample rate, record length, frame count를 포함한다.

파일 포맷 후보:

- `.bin` + `.json` metadata
- `.npy` 또는 `.npz`
- `.h5` HDF5
- `.csv`는 작은 디버그 데이터에만 사용

권장:

- 대용량 raw 기본 포맷: binary + JSON metadata
- 분석/연구용 HDF5 export는 후속 기능으로 둔다.
- 디버그 샘플: CSV 또는 NPY

### 3.10 Processed Data Save

처리 결과 저장 기능도 필요하다.

저장 대상:

- FFT spectrum
- Peak index/value
- Distance array
- Velocity array
- Heatmap
- Point cloud
- Processing metadata
- FFT backend 정보
- Calibration version
- Frame quality metrics

파일 포맷:

- Point cloud: PLY, PCD, LAS/LAZ 검토
- Frame result: HDF5, NPZ, CSV
- Screenshot: PNG

품질 지표:

- peak hit ratio
- saturated sample count
- dropped DMA buffer count
- dropped processing frame count
- UDP dropped packet/frame count
- disk writer queue high-water mark
- average/max processing latency

### 3.11 Configuration Management

기존 `Config.ini`는 확장성이 부족하므로 새 설정 구조를 둔다.

권장 파일:

- `config/default.yaml`
- `config/windows.yaml`
- `config/jetson.yaml`
- `config/profiles/*.yaml`

필수 설정 그룹:

- digitizer
- laser
- edfa
- scan
- chirp_segmentation
- processing
- udp
- storage
- ui
- calibration
- mcu

UI 요구사항:

- Load profile
- Save profile
- Save as
- Restore default
- Validate config
- Last used config 자동 로드

설정 UX 요구사항:

- 모든 설정값은 단위와 기본값을 가진다.
- profile에는 설명, 작성자, 작성일, 마지막 사용일을 기록한다.
- 설정 validation 결과는 Error/Warning/Info로 나눈다.
- 실행 중 변경 가능한 값은 즉시 적용하고, 재시작이 필요한 값은 pending change로 표시한다.
- Start 시점의 설정 snapshot은 session마다 저장한다.
- Windows와 Jetson에서 같은 profile을 최대한 공유하되, OS별 override를 허용한다.

### 3.12 Operation States

시스템은 명확한 상태 머신을 가져야 한다.

필수 상태:

- Disconnected
- Connected
- Configured
- Ready
- Preview
- Acquiring
- Recording
- Paused
- Stopping
- Error

상태 요구사항:

- 각 상태에서 가능한 버튼과 설정 변경 범위를 제한한다.
- Error 상태에서는 원인, 마지막 성공 단계, 복구 버튼을 제공한다.
- Stopping 상태에서는 중복 Stop 명령을 막고 진행 상황을 표시한다.
- Recording 상태에서는 저장 경로, 파일 크기, 저장 queue를 표시한다.
- Preview 상태에서는 낮은 부하로 trigger, waveform, scan pattern을 확인한다.

### 3.13 Replay and Simulator

장비 없이 개발하고 검증할 수 있어야 한다.

필수 기능:

- 저장된 raw data replay
- 저장된 processed frame replay
- synthetic FMCW signal generator
- fake digitizer mode
- fake EDFA controller mode
- fake UDP receiver/sender
- UI demo mode

목적:

- UI 개발을 장비 없이 진행
- CPU/GPU FFT 결과 비교
- 저장 포맷 검증
- 3D viewer 성능 검증
- 사용자 교육용 demo 제공

### 3.14 Health, Safety, and Diagnostics

장비 운용 중 상태와 위험을 명확히 보여줘야 한다.

필수 상태 표시:

- Digitizer connected/configured/acquiring
- Laser configured/enabled
- EDFA not used/manual/connected/output enabled/alarm
- Scanner connected/running
- MCU connected/running/firmware version
- GPU available/processing
- CPU fallback status
- UDP connected/sending/dropped
- Storage ready/recording/dropped
- Temperature 또는 system resource status
- Trigger period stable/jitter/missed count

안전 요구사항:

- Emergency stop
- Laser/EDFA/scanner/digitizer start order 확인
- Laser enable 상태와 acquisition 상태 분리 표시
- EDFA output 상태와 laser enable 상태 분리 표시
- 장비 연결 해제 감지
- timeout 또는 trigger loss 감지
- Start 전 checklist
- Stop 후 resource cleanup 확인

진단 요구사항:

- 최근 오류 이력
- session summary
- performance timeline
- config diff viewer
- dependency/version report
- export diagnostics package

### 3.15 MCU/Firmware Integration

MCU/STM32 기반 MEMS 제어 firmware는 기존 코드를 `legacy/MEMS_control_v3`에 보존하고, 새 버전에서 필요한 protocol, command, build artifact는 `src/firmware/mcu`와 문서로 분리한다.

MCU UI 요구사항:

- Serial port 선택
- Baud rate, parity, stop bit, timeout 설정
- Connect/disconnect
- Firmware version 조회
- MCU health/status 표시
- MEMS enable/disable
- Frame/pattern load
- Start/stop
- Clear/reset
- Emergency stop 시 MCU 출력 즉시 disable

Protocol 요구사항:

- UART command와 ACK/ERR 응답 형식을 versioning한다.
- command timeout, retry count, duplicate command 처리 정책을 둔다.
- MCU command log를 session log에 포함한다.
- UI command와 firmware command 이름을 1:1로 추적할 수 있어야 한다.
- legacy `uart_cmd.c`, `uart_cmd.h`, `frame_player.c`, `mirrorcle_drv.c`의 command/scan 동작을 새 protocol 문서로 추출한다.

Timing 요구사항:

- MCU MEMS scan timing과 digitizer acquisition frame timing을 같은 session clock 기준으로 기록한다.
- Laser `up_chirp_only` trigger 방식에서는 digitizer가 full-period buffer를 받고, MCU scan 위치는 해당 period/frame metadata와 매칭되어야 한다.
- trigger loss, MCU stop, scan underrun이 발생하면 acquisition/processing/storage metadata에 표시한다.
- replay 모드에서도 MCU scan position 또는 frame index metadata를 재현할 수 있어야 한다.

## 4. Non-Functional Requirements

### 4.1 Performance

- 실시간 수집 중 UI가 멈추지 않아야 한다.
- Acquisition thread, processing thread, UI thread를 분리한다.
- Storage writer thread, UDP sender thread, 3D rendering thread를 acquisition loop와 분리한다.
- GPU FFT와 CPU FFT 모두 동일한 입력/출력 인터페이스를 사용한다.
- UDP 송신 실패가 수집 루프 전체를 멈추지 않도록 한다.
- UI는 최신 상태 snapshot을 읽고, acquisition buffer를 직접 소유하지 않는다.
- 2D/3D 렌더링은 표시용 복사본이나 downsample된 데이터를 사용한다.
- 각 stage의 평균/최대 latency를 측정한다.
- queue overflow 기본 정책은 acquisition stop으로 한다.
- stop 발생 시 원인 queue, 마지막 frame ID, queue 사용량, 복구 절차를 UI와 log에 표시한다.

권장 성능 지표:

- acquisition buffer drop count
- processing queue length
- processing latency average/max
- UI frame rate
- 3D viewer update rate
- raw writer throughput
- UDP send throughput
- CPU/GPU utilization
- memory usage

### 4.2 Reliability

- Start/Stop 반복 시 메모리 누수와 장비 lock 상태가 없어야 한다.
- 장비 연결 실패 시 UI에서 명확한 오류를 보여준다.
- optional 장비인 EDFA가 비활성 profile에서 없을 때는 오류로 처리하지 않는다.
- 저장 중 디스크 속도가 부족하면 drop/skip 정책을 표시한다.
- 설정값이 잘못되면 수집 시작 전에 검증한다.
- long-run test를 통해 일정 시간 이상 연속 측정 시 drop, memory growth, handle leak을 확인한다.
- acquisition 중 예외가 발생해도 장비 stop/abort/cleanup을 수행한다.
- crash 이후 마지막 session metadata와 log를 복구할 수 있어야 한다.
- 실행 파일과 설정 profile의 version compatibility를 확인한다.

### 4.3 Portability

- Windows와 Jetson/Linux 코드를 명확히 분리한다.
- OS별 코드는 `drivers/windows`, `drivers/linux`처럼 분리한다.
- 공통 알고리즘은 OS 독립적으로 유지한다.
- Visual Studio 절대경로 의존성을 제거한다.
- Qt, CUDA, FFTW, AlazarTech SDK는 저장소에 vendor binary로 포함하지 않고 CMake package 또는 cache root로 탐색한다.
- 개인 PC SDK 경로는 `CMakeUserPresets.json`에만 두고 source-controlled CMake 파일에 기록하지 않는다.
- 플랫폼별 준비 절차는 `docs/build_setup.md`를 따른다.

### 4.4 Maintainability

- 구버전 파일은 `legacy`에 보관한다.
- 새 버전은 역할별 모듈로 나눈다.
- 전역 shared state는 점진적으로 `SystemState` 또는 `FrameBus` 구조로 교체한다.
- 설정, 수집, 처리, 시각화, 저장을 독립 모듈로 관리한다.
- UI는 core library를 호출하고, core는 UI framework를 알지 못해야 한다.
- Windows/Jetson 차이는 driver adapter와 build profile에서 흡수한다.
- 설정 schema와 UDP packet schema는 문서와 코드가 함께 versioning되어야 한다.

## 5. Proposed New Source Structure

```text
FMCW_LiDAR/
  docs/
  config/
    calibration/
  data/
    raw/
    processed/
    samples/
  hardware/
  legacy/
    fmcw_lidar_v2_legacy/
    MEMS_control_v3/
    EDFA-Amplifier-V20240219/
  outputs/
  src/
    apps/
      windows/
      jetson/
    core/
    drivers/
      alazar/
      edfa/
    processing/
    storage/
    visualization/
    firmware/
      mcu/
  tests/
```

구조 원칙:

- `legacy/`: 기존에 작성한 원본 코드 보관. 직접 수정하지 않고 비교 기준으로 사용한다.
- `legacy/fmcw_lidar_v2_legacy`: 기존 FMCW LiDAR PC/Jetson 코드.
- `legacy/MEMS_control_v3`: 기존 MCU/STM32 MEMS 제어 firmware 코드.
- `legacy/EDFA-Amplifier-V20240219`: EDFA vendor controller, driver, command 문서.
- `src/`: 새 Windows/Jetson 공통 코어와 새 앱을 개발하는 위치.
- `src/apps/windows`: Windows UI/실행 앱.
- `src/apps/jetson`: Jetson/Linux Qt UI 실행 앱.
- `src/core`: config, state machine, frame bus, session, telemetry 등 공통 로직.
- `src/drivers`: Alazar, MTI, serial, UDP 등 장비/OS 의존 코드.
- `src/drivers/alazar`: Windows/Jetson 공통 AlazarTech adapter와 OS별 SDK wrapper.
- `src/drivers/edfa`: EDFA optional driver, protocol parser, simulator.
- `src/processing`: GPU FFT, CPU FFT, peak, distance/velocity 처리.
- `src/storage`: raw/processed writer.
- `src/visualization`: 2D/3D 시각화 공통 로직.
- `src/firmware/mcu`: 새 MCU firmware 또는 MCU protocol/header를 정리하는 위치.
- `config/`: profile, calibration, platform override.
- `data/`: 입력/검증용 데이터.
- `outputs/`: 실행 결과, 캡처, export, diagnostics package.

## 6. Migration Plan

### Phase 0: Legacy Inventory and Requirement Lock

목표:

- 기존 PC/Jetson LiDAR 코드, MCU firmware, EDFA vendor 자료를 보존하고 새 구조의 기준을 확정한다.

작업:

- legacy 코드 위치 확정: `legacy/fmcw_lidar_v2_legacy`
- MCU legacy 위치 확정: `legacy/MEMS_control_v3`
- EDFA legacy 위치 확정: `legacy/EDFA-Amplifier-V20240219`
- Windows/Jetson 모두 Qt 6 UI로 결정
- Jetson에서도 AlazarTech 보드 직접 사용으로 결정
- CPU FFT는 FFTW로 결정
- raw 저장은 binary + JSON metadata로 결정
- queue overflow 기본 정책은 stop으로 결정
- 단일 navigation과 페이지 내부 Primary/Details field 기준 확정

완료 조건:

- `docs/requirements.md`와 `docs/folder_structure.md`에 결정사항이 반영되어 있다.
- 새 코드는 `src/` 아래에서만 시작한다.

### Phase 1: Build System and Core Skeleton

목표:

- Windows와 Jetson에서 같이 가져갈 공통 core 구조를 먼저 만든다.

작업:

- Qt 6 app skeleton 생성
- Windows app entry: `src/apps/windows`
- Jetson app entry: `src/apps/jetson`
- 공통 core module 생성: state machine, frame metadata, logging
- 공통 driver interface 생성: Alazar, MCU, EDFA, UDP
- 공통 processing interface 생성: CUDA FFT, FFTW FFT
- 공통 storage interface 생성: raw writer, processed writer
- platform adapter 구분: Windows/Jetson SDK, serial, filesystem, timer
- full-period raw frame과 revision metadata 계약 문서화
- 상태 전이 contract test 추가

완료 조건:

- 선택한 플랫폼에서 Qt app shell이 실행된다.
- core contract test가 통과한다.
- log/status shell이 동작한다.
- Windows와 Jetson build profile이 분리되어 있다.
- Qt 6 또는 C++ compiler가 없으면 누락된 필수 dependency를 명확히 보고한다.

### Phase 2: Configuration and System State

목표:

- 모든 장비 설정을 UI와 파일에서 같은 구조로 다룬다.

작업:

- YAML profile schema 작성
- `digitizer`, `laser`, `edfa`, `scan`, `chirp_segmentation`, `processing`, `udp`, `storage`, `ui`, `calibration`, `mcu` 설정 그룹 구현
- 전역 UI mode 없이 field별 Primary/Details presentation 기준 구현
- 설정 validation rule 구현
- pending change와 restart-required 표시
- Start/Stop state machine 구현
- queue overflow 시 acquisition stop 정책 구현

완료 조건:

- 잘못된 설정은 Start 전에 막힌다.
- Start 시점의 config snapshot이 session metadata에 저장된다.
- 모든 설정 페이지가 단일 navigation에서 접근 가능하고 restart-required 상태가 field별로 구분된다.

### Phase 3: Acquisition and Device Drivers

목표:

- 실제 장비와 연결되는 최소 acquisition path를 안정화한다.

작업:

- AlazarTech adapter 구현: `src/drivers/alazar`
- Windows Alazar SDK wrapper 구현
- Jetson Alazar SDK/driver wrapper 구현
- 단일 채널 A 또는 B 수집만 지원
- `up_chirp_only` full-period acquisition 구현
- chirp segmentation boundary 적용
- MCU UART/protocol 정리 및 adapter 구현
- EDFA optional driver 구현: none/manual/controlled
- EDFA 없는 profile에서도 정상 Start 가능하게 처리
- 최소 fake digitizer/EDFA/MCU adapter로 장비 없는 acquisition path 검증

완료 조건:

- fake digitizer에서 single-channel full-period raw acquisition이 가능하다.
- Windows Alazar adapter가 SDK가 설치된 환경에서 빌드되고 단일 채널 raw acquisition acceptance 절차가 정의된다.
- Jetson Alazar SDK 연결/빌드 절차와 hardware acceptance 절차가 정의된다.
- EDFA 미사용 profile이 오류 없이 동작한다.
- MCU/EDFA 연결 상태가 core telemetry snapshot에 제공된다.

### Phase 4: Processing and Storage Pipeline

목표:

- 실시간 processing과 raw 저장이 acquisition을 막지 않도록 분리한다.

작업:

- CUDA/cuFFT backend 정리
- CPU FFTW backend 구현
- GPU FFT와 CPU FFT 결과 비교 테스트 작성
- ADC full-scale 변환, DC removal, window, FFT, independent peak detection, distance/velocity 처리 구현
- scan line accumulator와 B-scan matrix 구현
- UI용 immutable waveform/FFT/peak/B-scan snapshot publisher 구현
- full-period raw buffer에서 up/down segment extraction 적용
- raw binary writer 구현
- JSON metadata sidecar 저장 구현
- processed frame writer 구현
- 저장 실패 시 acquisition stop 정책 구현
- replay mode 구현

완료 조건:

- raw binary + JSON metadata 저장이 동작한다.
- 저장된 raw data replay가 같은 processing pipeline을 사용한다.
- FFTW 결과와 CUDA FFT 결과를 비교할 수 있다.
- acquisition thread가 disk write를 기다리지 않는다.

### Phase 5: Qt UI MVP

목표:

- 사용자가 실제 장비 운용을 시작/정지하고 상태를 볼 수 있는 Qt UI를 만든다.

작업:

- Overview/status bar
- global command bar와 단일 START/STOP
- Digitizer
- Laser / EDFA
- Scan / MCU
- Processing
- Storage / UDP
- System Log
- Live View: Time Domain, FFT, Peak Analysis, Distance/Velocity, B-scan
- on-demand chirp segmentation snapshot
- page view model과 immutable UI snapshot publisher
- Health/Safety checklist
- session summary

완료 조건:

- UI에서 profile 선택, validation, Start, Stop, Save on/off가 가능하다.
- Start 실패 시 사용자가 이해할 수 있는 오류가 표시된다.
- waveform/FFT/peak/B-scan이 acquisition과 독립된 rate로 갱신된다.
- digitizer가 arm된 뒤 MCU scan이 시작되고, Stop 시 MCU trigger가 먼저 차단된다.

### Phase 6: 3D, UDP, and Simulator Expansion

목표:

- point cloud 확인, 외부 송신, 장비 없는 개발 흐름을 완성한다.

작업:

- Qt/OpenGL 기반 경량 3D point cloud viewer 구현
- point decimation/LOD 적용
- UDP sender 구현
- UDP receiver 테스트 도구 작성
- 고속 synthetic FMCW signal generator와 fault injection
- fake digitizer/EDFA/MCU의 timing, timeout, alarm 시나리오 확장
- diagnostics export 구현

완료 조건:

- 3D viewer가 acquisition을 막지 않는다.
- UDP 송신 실패가 acquisition thread를 막지 않는다.
- 장비 없이 UI demo/replay/simulator가 실행된다.

### Phase 7: Hardware Integration and Release Hardening

상세 subphase 순서, audit finding 추적, 완료 조건과 commit/push 기준은 `docs/phase7_execution_plan.md`를 단일 실행 기준으로 사용한다.

목표:

- Windows 실제 장비 pipeline을 완성하고 Jetson에서도 같은 Qt UI와 core pipeline을 사용해 현장 운용 가능한 형태로 만든다.

작업:

- ATS9371 ADC sample alignment와 chirp profile 일관성 수정
- packaged EXE의 simulator 고정 runtime을 Alazar/MCU/EDFA hardware runtime으로 교체
- DMA buffer 단위 acquisition, FFTW batch, CUDA full pipeline 구현
- DMA-block high-speed raw recording과 replay 구현
- laser timing, MCU waveform, scan geometry calibration 정합
- Windows hardware acceptance와 release package 완성
- Jetson Qt UI packaging
- Jetson AlazarTech SDK/driver 설치 절차 문서화
- Jetson CUDA/cuFFT 동작 확인
- Jetson FFTW build/link 확인
- Linux serial/UDP/storage adapter 검증
- 로컬 Qt UI 자동 실행 옵션 구성
- long-run acquisition/storage test
- UI responsiveness test
- config compatibility test
- diagnostics package 검증

완료 조건:

- Windows에서 실제 ATS9371 acquisition, FFTW/CUDA processing, raw recording이 장시간 검증된다.
- audit finding `P7-001`부터 `P7-008`까지 모두 닫힌다.
- Jetson에서 Qt UI가 실행된다.
- Jetson에서 Alazar acquisition path가 검증된다.
- 장시간 운용 시 drop, memory growth, handle leak을 측정할 수 있다.
- Windows와 Jetson에서 같은 profile/schema를 공유한다.

## 7. Confirmed Decisions

현재 결정된 기준은 다음과 같다.

1. Windows UI는 Qt 6로 개발한다.
2. Jetson도 로컬 Qt UI를 띄운다.
3. AlazarTech 보드는 Jetson에서도 직접 사용한다.
4. CPU FFT 라이브러리는 FFTW로 한다.
5. raw 저장은 고속 binary + JSON metadata로 시작한다.
6. 3D point cloud는 실시간성과 가벼움을 우선해 Qt/OpenGL 기반 renderer로 시작한다.
7. 실시간 queue overflow 기본 정책은 stop으로 한다.
8. UI는 전역 Basic/Advanced mode 없이 단일 navigation으로 구성하고, 상세 설정은 각 페이지 내부 `Details` 영역에서 제공한다.
9. v1 hardware acquisition trigger는 up chirp 시작 1회만 사용하며, trigger마다 full up+down period를 저장한 뒤 software에서 두 구간을 분리한다.
10. acquisition과 scanner는 한 개 global START/STOP으로 운용하며 digitizer를 먼저 arm하고 MCU trigger를 마지막에 시작한다.
11. chirp segmentation 설정 graph는 연속 실시간 plot이 아니라 요청 시 고정되는 full-period snapshot이다.
12. FFT spectrum은 Live View FFT 탭만 소유하며 Peak Analysis는 peak index/value와 validity만 표시한다.

## 8. Recommended First Implementation Target

첫 목표는 다음 조합이 가장 현실적이다.

- Windows desktop app
- Qt 6 기반 상업용 장비 소프트웨어 느낌의 UI
- Windows/Jetson Alazar acquisition 공통화
- 기존 CUDA FFT 유지
- CPU FFTW backend 추가
- YAML profile과 config validation 추가
- system state machine 추가
- async raw writer 추가
- raw binary + JSON metadata 저장
- 2D plot 먼저 안정화
- replay mode 추가
- 이후 Qt/OpenGL 3D point cloud viewer 추가

Jetson 버전은 Windows 버전과 같은 core/UI 구조를 쓰되, OS별 driver/network/storage adapter와 AlazarTech SDK 설치 절차를 분리한다.
