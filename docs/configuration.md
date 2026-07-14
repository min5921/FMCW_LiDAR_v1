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
| `laser` | wavelength, sweep, chirp, scale correction | nm, Hz, us, mW |
| `edfa` | optional/manual/controlled, output on/off 준비, setpoint, serial | dBm 또는 mW, ms |
| `scan` | MEMS angle, pixel/line, timing | deg, ms, Hz |
| `chirp_segmentation` | up-trigger 기준 전체 주기와 up/down 절단 범위 | sample |
| `processing` | FFTW/CUDA, window/peak, queue | dB, bin, frame |
| `udp` | IPv4 endpoint, packet version, backpressure | port, point |
| `storage` | raw/processed toggle, split/flush, queue | GB, frame |
| `ui` | 2D/3D refresh rate, overlay, color map, last profile | Hz |
| `calibration` | distance/velocity/angle correction | m, m/s, deg |
| `mcu` | optional UART, ACK, retry | baud, ms |

`chirp_segmentation.mode`는 `up_chirp_only`만 허용한다. Digitizer는 up chirp trigger에서 전체 up/down 주기를 한 번에 받고, `up_segment`와 `down_segment`의 half-open range `[start, end)`를 후단 처리에 전달한다.

`digitizer.board_profile`은 선택 가능한 sampling rate, input range, impedance를 제한한다. SDK 25.1.0의 `AlazarSysInfo`로 확인한 장치는 `ATS9371`, System 1 / Board 1, 12-bit, FPGA 35.3이다. System/Board ID는 1로 고정한다. 내부 clock sampling rate는 SDK 보드 표의 1 kS/s부터 1 GS/s까지 20개 discrete 값만 ComboBox로 제공한다. 현재 입력 경로는 legacy와 실제 설정에 맞춰 `+/-400 mV`, DC coupling, `50 ohm`으로 제한한다.

Trigger는 `TRIG IN`, External TTL, DC coupling을 고정 contract로 사용한다. UI는 rising/falling edge, signed full-scale threshold, SDK threshold code, trigger delay, pre/post-trigger samples를 표시한다. 기본값 `+17.3 % FS`는 legacy SDK code `150`에 해당하며, delay는 `400 samples`, trigger timeout은 `0 ticks`로 hardware trigger를 계속 기다린다. ATS9371 record와 pre-trigger alignment는 128 samples, single-channel trigger delay alignment는 16 samples이다.

Scan 계산에서 A-scans/B-scan은 별도 입력값이 아니라 `digitizer.records_per_buffer`와 동일하다. B-scans/frame은 사용자가 지정하며, 한 프레임의 position 수는 두 값의 곱이다. B-scan rate와 period는 Alazar DMA buffer 완료 timestamp에서 실측하고, frame time은 실측 period와 B-scans/frame의 곱으로 계산한다. MCU의 100 kHz point rate는 전체 프레임 파형 cycle time 계산에만 사용한다.

## Single UI And Field Presentation

전역 Basic/Advanced mode는 schema version 2부터 사용하지 않는다. 모든 설정 페이지는 항상 접근 가능하며, field policy의 `primary`는 페이지에 바로 표시하고 `detailed`는 같은 페이지의 `Details` 영역에 표시한다. 이 구분은 접근 권한이나 별도 운용 mode가 아니다.

Processing group은 `peak_threshold_db`, `peak_search_start_bin`, `peak_search_end_bin`만 runtime peak 설정으로 저장한다. 각 A-scan은 이전 결과를 추적하지 않고 search range 안의 threshold 이상 최대 peak를 독립적으로 검출한다.

Live View의 `Selected A-scan`은 profile 설정이 아니라 표시 상태다. 0부터 `records_per_buffer - 1` 사이의 record index를 acquisition 중에도 바꿀 수 있으며 digitizer 재설정이나 processing 결과에는 영향을 주지 않는다.

UDP v1은 little-endian `FMCW` point packet을 사용한다. `packet_point_count`는 UDP 최대 payload를 넘지 않도록 1..3273으로 제한하며, `queue_capacity`와 `backpressure_policy`는 각각 sender queue 크기와 `latest_frame`, `preserve_frames`, `stop_sending` 동작을 정한다.

### Schema Version 4 Migration

- full profile의 `profile.schema_version`을 `4`로 변경한다.
- 더 이상 사용하지 않는 `scan.line_time_ms`를 제거한다.
- MCU waveform point 수는 `records_per_buffer * y_line_count`로 계산한다.
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
