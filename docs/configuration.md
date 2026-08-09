# Configuration System

Phase 2의 설정 시스템은 UI와 profile 파일이 동일한 `SystemConfig` 타입을 사용하도록 구성한다. 실제 장비 연결 전에도 설정을 읽고 검증할 수 있으며, Start가 승인된 시점의 전체 설정을 JSON snapshot으로 고정한다.

## Profile Layers

설정은 아래 순서로 병합하며 뒤의 값이 앞의 값을 덮어쓴다.

1. 코드에 포함된 안전 기본값
2. `config/default.yaml`
3. 플랫폼 설정: `config/windows.yaml` 또는 `config/jetson.yaml`
4. 사용자 profile: `config/profiles/*.yaml`
5. calibration: `config/calibration/*.yaml`

일부 값만 가진 partial profile도 사용할 수 있다. 알 수 없는 key, 잘못된 scalar type, 범위를 벗어난 정수는 profile load 단계에서 거부한다.

## YAML Scope

실시간 장비 프로그램에서 플랫폼별 파싱 차이를 피하기 위해 다음의 엄격한 YAML 하위 규격만 지원한다.

- 2-space indentation을 사용하는 중첩 map
- string, integer, finite number, boolean scalar
- `#` comment
- 콜론이나 빈 문자열이 포함된 string은 따옴표 사용
- sequence, anchor, alias, tag, null, multiline scalar는 지원하지 않음

예:

```yaml
digitizer:
  board_profile: ats9371
  channel: a
  sample_rate_hz: 1000000000
edfa:
  mode: none
  port: ""
```

## Groups And Units

| Group | 주요 내용 | 기준 단위 |
|---|---|---|
| `digitizer` | Alazar board, A/B 단일 채널, sampling, DMA, trigger | Hz, sample, ms, V |
| `laser` | distance conversion bandwidth and sweep rate | Hz |
| `edfa` | optional/manual/controlled, output on/off 준비, setpoint, serial | dBm 또는 mW, ms |
| `scan` | MEMS angle, pixel/line, timing | deg, ms, Hz |
| `chirp_segmentation` | up-trigger 기준 전체 주기와 up/down 절단 범위 | sample |
| `processing` | FFTW/CUDA, window/peak, queue | dB, bin, frame |
| `udp` | IPv4 endpoint, packet version, backpressure | port, point |
| `storage` | raw/processed toggle, split/flush, queue | GB, frame |
| `ui` | 2D/3D refresh rate, overlay, color map, last profile | Hz |
| `calibration` | distance/velocity/angle correction and velocity wavelength | m, m/s, nm, deg |
| `mcu` | optional UART, waveform source/file, ACK, retry | baud, ms |

`chirp_segmentation.mode`는 `up_chirp_only`만 허용한다. Digitizer는 up chirp trigger에서 전체 up/down 주기를 한 번에 받고, `up_segment`와 `down_segment`의 half-open range `[start, end)`를 후단 처리에 전달한다.

Laser 설정은 `sweep_bandwidth_hz`와 full triangular waveform의 `sweep_rate_hz` 두 값만 사용한다. 두 값은 거리 변환식 `c * (f_up + f_down) / (8 * bandwidth * sweep_rate)`에만 들어가며 sample point, chirp segmentation, trigger timing을 결정하지 않는다. 양수가 아닌 값은 거리 계산이 불가능하므로 Error지만, bandwidth와 sweep rate 사이의 추정 관계나 timing 일치 Warning은 만들지 않는다.

Full-period sample segmentation은 Laser 설정과 독립적이다. 일반 development simulator와 replay는 UI 확인을 위한 제한된 pacing을 사용한다. `runtime.simulator_realtime_dma: true`인 ATS qualification simulator만 실제 trigger 부하를 재현하기 위해 `records_per_buffer / laser.sweep_rate_hz`를 DMA completion 주기로 사용한다. Digitizer simulator는 시작 시 96 ADC-count RMS Gaussian-like noise가 포함된 재현 가능한 signal template을 미리 생성하고 DMA slot별로 순환하여, runtime 난수 생성 비용 없이 Time Domain과 FFT가 실제 입력처럼 변하도록 한다. 실제 장비에서는 trigger timestamp와 DMA completion timestamp를 telemetry로 측정한다.

`digitizer.board_profile`은 선택 가능한 sampling rate, input range, record alignment와 trigger range를 제한한다. 지원 범위는 SDK 25.1.0의 12-bit 및 `AUX_IN_TRIGGER_ENABLE` NPT Scan 조건을 모두 만족하는 11개 모델이며 상세 목록은 `docs/alazar_supported_models.md`에 있다. System/Board ID는 `1 / 1`, DC coupling과 `50 ohm`은 고정한다. `runtime.acquisition_source: alazar`에서는 SDK의 `AlazarGetBoardKind`로 모델을 자동 감지하고 `Board model`에는 읽기 전용 결과만 표시한다. 감지된 capability에 맞춰 sampling rate, input range, record/pre-trigger/trigger-delay alignment를 즉시 다시 채우므로 사용자가 모델을 선택하지 않는다. Simulator와 Replay는 저장된 `board_profile`을 장비 capability emulation과 raw 해석에 사용한다.

Trigger는 `TRIG IN`, external, DC coupling contract를 사용한다. UI는 rising/falling edge, trigger delay, pre/post-trigger samples만 표시하며 analog `% FS` threshold는 제공하지 않는다. ATS SDK trigger level 인자는 내부 code `150`을 사용하고 외부 입력 range는 모델에 따라 `ETR_TTL` 또는 `ETR_5V`로 설정한다. AUX IN은 positive-slope `AUX_IN_TRIGGER_ENABLE` B-scan gate로 고정한다. Delay 기본값은 `0 samples`, trigger timeout은 `0 ticks`이며 record/pre-trigger/delay alignment는 선택 모델의 capability를 따른다.

Scan 계산에서 A-scans/B-scan은 별도 입력값이 아니라 `digitizer.records_per_buffer`와 동일하다. B-scans/frame은 사용자가 지정하며, 한 프레임의 position 수는 두 값의 곱이다. B-scan rate와 period는 Alazar DMA buffer 완료 timestamp에서 실측하고, frame time은 실측 period와 B-scans/frame의 곱으로 계산한다. MCU의 100 kHz point rate는 전체 waveform cycle time과 A-scan-to-command mapping에 사용한다. `mcu.waveform_source: legacy_xym_file`에서는 `mcu.waveform_file`의 실제 point 수가 cycle 길이를 정하며 원본 M rising edge 수는 B-scans/frame과 같아야 한다. line의 dominant command delta는 fast axis 판정에만 사용하고 증가/감소 방향은 자동 판정하지 않는다. 사용자가 `scan.bidirectional`을 끄면 모든 line의 `x_index`가 acquisition order를 유지하고, 켜면 짝수 line은 정방향, 홀수 line은 `x_index`에서 한 번만 역배치한다. 이 반전은 A-scan 내부 time sample이나 FFT 입력 순서를 바꾸지 않는다. azimuth는 정렬된 `x_index`와 `scan.x_start_deg`/`x_end_deg`로 계산한다. Vector line은 위에서 아래로 수집되므로 elevation은 첫 `y_index`를 `scan.y_end_deg`, 마지막을 `scan.y_start_deg`로 계산한다. 원본 X/Y command와 trajectory sample index는 저장 및 진단용 provenance로 유지한다. `generated_raster`도 같은 `scan.bidirectional` 홀짝 및 위에서 아래 elevation 계약을 사용하며 point 수는 `records_per_buffer * y_line_count`로 계산한다. `scan.trigger_shift_samples`는 원본 X/Y와 원본 M을 provenance로 보존하고 업로드 직전에 출력 M/B-trigger bit만 반복 waveform 안에서 순환 이동한다. digitizer가 실제로 보는 emitted M edge가 acquired line과 좌표의 anchor다. `0`은 원본 timing, 음수는 trigger advance, 양수는 trigger delay이며 100 kHz에서 1 sample은 10 us이다. 변경 후에는 `Apply Setup`과 waveform 재업로드가 필요하다.

## Single UI And Field Presentation

### Alazar Record Length

`digitizer.sample_point`는 사용자가 직접 정하는 Alazar record 길이이자 캡처된 full-period 길이의 단일 기준이다. 모든 지원 모델의 최소 길이는 256 samples이며 resolution, pre-trigger alignment, NPT pre-trigger 최대값과 trigger-delay alignment는 선택 모델에 따라 달라진다. UI는 유효한 record 값만 확정하고 sample rate로 계산한 실제 record 시간을 즉시 표시한다. Segmentation Snapshot은 별도 period length를 입력받지 않고 Digitizer의 `sample_point`를 그대로 사용하며, UP/DOWN segment가 record 밖으로 나가는 경우에는 Error를 유지한다. YAML의 `chirp_period_samples`는 기존 profile 호환을 위한 파생 mirror이며 저장 시 항상 `sample_point`와 같은 값으로 기록된다.

### Runtime Acquisition Source

The `runtime` profile group selects `simulator`, `alazar`, or `replay` for the digitizer only. Source changes are restart-required settings and are available directly on the Digitizer page. `alazar` auto-detects the supported ATS model at System 1 / Board 1, `simulator` creates the deterministic DMA source, and `replay` reads raw data through the same acquisition and processing pipeline. Selecting a replay raw part loads the adjacent `.setup.yaml`, or the older JSON `config_snapshot` fallback, so the saved board profile and measurement setup replace pending controls without manual board selection. Optional MCU and EDFA adapters are selected independently during live acquisition; replay restore disables both physical outputs as well as UDP and recursive recording. Replay requires `runtime.replay_file`, while `runtime.replay_loop` controls end-of-stream looping. `runtime.simulator_realtime_dma` enables the bounded, absolute-cadence DMA ring used by `config/profiles/ats9371_200hz_simulator.yaml`; a full ring latches overflow instead of slowing or silently dropping input. Normal operation uses the GUI and saved profiles rather than command-line source options.

Laser / EDFA와 Scan / MCU의 `Serial port`는 Windows의 현재 COM 장치 또는 Jetson/Linux의 지원 tty 장치를 자동 검색하는 combo box다. Jetson에서는 MCU용 `/dev/ttyTHS0`을 `Jetson 40-pin UART (pins 8/10)`으로, EDFA용 `/dev/ttyUSB*`를 `USB UART`로 구분해 표시하고 각 장치 역할에 맞는 포트를 목록 앞에 배치한다. 새 장치를 연결한 뒤 refresh 버튼으로 목록을 갱신하고 선택한 뒤 `Apply Setup`과 `Connect`를 수행한다. MCU와 controlled EDFA가 같은 포트를 선택하면 검증 오류로 Apply와 Connect를 차단한다. Windows COM은 한 프로세스가 독점하므로 vendor controller나 다른 serial tool이 같은 포트를 열고 있으면 이를 닫은 뒤 연결해야 한다. Controlled mode는 APC만 지원하며 출력 setpoint 범위는 0.0~30.0 dBm이다. 연결 시 실제 mode, target, soft activation, input/output power, pump current를 읽어 표시한다. `Enable Output`은 APC mode와 UI setpoint를 장비 응답으로 확인한 뒤 activation을 요청하며, `Disconnect`, 전역 `STOP`, `E-STOP`은 serial port를 닫기 전에 output OFF를 시도한다.

### Profile Load And Apply

상단 profile 표시는 저장하거나 불러온 YAML 파일의 base name을 사용한다. `Load`는 새 설정을 controls에 staging하고 `APPLY REQUIRED` 상태로 전환하지만 현재 적용된 runtime config를 즉시 덮어쓰지 않는다. 이후 `Apply Setup` 또는 `Connect`가 성공해야 applied config와 revision이 갱신된다. 따라서 저장한 profile을 다시 불러왔을 때 기본 `ATS9371 200 Hz Simulator` 이름이나 이전 runtime 설정이 남지 않는다.

전역 Basic/Advanced mode는 schema version 2부터 사용하지 않는다. 모든 설정 페이지는 항상 접근 가능하며, field policy의 `primary`는 페이지에 바로 표시하고 `detailed`는 같은 페이지의 `Details` 영역에 표시한다. 이 구분은 접근 권한이나 별도 운용 mode가 아니다.

Processing group은 `peak_threshold_db`, `peak_search_start_bin`, `peak_search_end_bin`만 runtime peak 설정으로 저장한다. 각 A-scan은 이전 결과를 추적하지 않고 search range 안의 최대 정수 bin을 독립적으로 검출한다. Strict threshold는 이 center bin에 먼저 적용하며, 통과한 peak는 좌우 dB 값을 이용한 3-point quadratic refinement로 fractional `peak_bin`을 계산한다. Search boundary나 유효하지 않은 curvature에서는 정수 bin으로 fallback하고, threshold를 초과하지 못하면 실수형 peak 및 측정 필드는 `NaN`이다.

실수 입력 FFT의 내부 R2C 결과는 Nyquist bin까지 보존한다. 단, 측정용 peak search와 Live FFT 표시는 `[0, FFT length / 2 - 1]`만 사용하므로 FFT length 2048에서는 inclusive 범위가 `0..1023`이고 bin 1024는 표시 및 검출에서 제외된다.

Live View의 `Selected A-scan`은 profile 설정이 아니라 표시 상태다. 0부터 `records_per_buffer - 1` 사이의 record index를 acquisition 중에도 바꿀 수 있으며 digitizer 재설정이나 processing 결과에는 영향을 주지 않는다.

UDP v2는 little-endian `FMCW` point packet과 ROS/RViz `X forward, Y left, Z up` 좌표를 사용한다. 같은 wire layout에 legacy axis 의미를 사용한 v1은 송신과 수신에서 거부한다. `packet_point_count`는 UDP 최대 payload를 넘지 않도록 1..3273으로 제한하며, `queue_capacity`와 `backpressure_policy`는 각각 sender queue 크기와 `latest_frame`, `preserve_frames`, `stop_sending` 동작을 정한다.

### Schema Version 5

- full profile의 `profile.schema_version`은 `5`다.
- `digitizer.trigger_level_percent`를 제거하고 External TTL 및 SDK level code `150`을 고정한다.
- Laser group은 `sweep_bandwidth_hz`, `sweep_rate_hz`만 유지한다.
- velocity wavelength는 `calibration.velocity_wavelength_nm`으로 이동한다.
- 더 이상 사용하지 않는 `scan.line_time_ms`를 제거한다.
- MCU `generated_raster` waveform point 수는 `records_per_buffer * y_line_count`로 계산하고, `legacy_xym_file`은 파일의 실제 point 수를 사용한다.
- B-scan timing은 설정 파일 값이 아니라 runtime DMA telemetry로 기록한다.
- 더 이상 지원하지 않는 `ui.mode` key를 제거한다.
- `processing.normalize`, `processing.peak_tracking_*`, `processing.peak_lost_policy` key를 제거한다.
- digitizer group에 `board_profile`을 추가한다.
- schema v3 full profile은 frame별 amplitude normalization이나 peak continuity 설정을 기록하지 않는다.

각 field에는 변경 정책이 함께 등록된다.

- `runtime`: acquisition 중 다음 frame boundary에서 반영. 현재 DC removal, peak threshold/search range가 해당한다.
- `preview_only`: Preview에서는 즉시 반영하고 acquisition 중에는 pending
- `restart_required`: 운전 중에는 변경 control을 잠근다. STOP 후 `Apply Setup`이 disconnect/configure/reconnect를 수행하며 자동 START하지 않는다.

active config가 실제 장비와 처리 pipeline의 유일한 입력이다. pending config는 active config를 바꾸지 않으며 `Ready` 같은 idle state에서 명시적으로 적용한다. 즉시 적용 또는 pending 적용 때마다 config revision이 증가한다.

## Validation And Stop Policy

Start 전에 record 길이, pre/post trigger 합, full-period 범위, up/down segment와 guard, FFT bin, scan 크기, serial/UDP endpoint, EDFA 안전 출력, storage와 UI rate를 검증한다. Error가 하나라도 있으면 Start를 거부하고 상태는 `Ready`에 남는다. Warning은 운용자에게 표시하되 Start를 차단하지 않는다.

Start 승인 결과에는 config revision과 JSON snapshot이 포함된다. Phase 4의 session writer가 이 값을 raw metadata와 함께 저장한다.

Processing과 storage queue는 bounded queue와 `stop_acquisition` 정책을 사용한다. UDP queue는 acquisition을 막지 않고 선택한 backpressure 정책을 적용하며 drop/send-error telemetry를 UI와 log에 남긴다.
