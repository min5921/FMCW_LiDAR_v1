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

## Single UI And Field Presentation

전역 Basic/Advanced mode는 schema version 2에서 제거했다. 모든 설정 페이지는 항상 접근 가능하며, field policy의 `primary`는 페이지에 바로 표시하고 `detailed`는 같은 페이지의 `Details` 영역에 표시한다. 이 구분은 접근 권한이나 별도 운용 mode가 아니다.

Processing group에는 peak threshold/search range와 함께 `peak_tracking_enabled`, `peak_tracking_max_delta_bins`, `peak_reacquire_width_bins`, `peak_lost_policy`를 저장한다. 이 값은 실행 중 변경 가능한 runtime 설정이다.

### Schema Version 2 Migration

- full profile의 `profile.schema_version`을 `2`로 변경한다.
- 더 이상 지원하지 않는 `ui.mode` key를 제거한다.
- partial profile은 새 peak tracking key를 생략하면 built-in default를 상속한다.
- export한 full profile에는 네 개 peak tracking key를 기록한다.

각 field에는 변경 정책이 함께 등록된다.

- `runtime`: acquisition 중에도 즉시 active config에 반영
- `preview_only`: Preview에서는 즉시 반영하고 acquisition 중에는 pending
- `restart_required`: 운전 중에는 pending으로 보관하고 Stop 이후 적용

active config가 실제 장비와 처리 pipeline의 유일한 입력이다. pending config는 active config를 바꾸지 않으며 `Ready` 같은 idle state에서 명시적으로 적용한다. 즉시 적용 또는 pending 적용 때마다 config revision이 증가한다.

## Validation And Stop Policy

Start 전에 record 길이, pre/post trigger 합, full-period 범위, up/down segment와 guard, FFT bin, scan 크기, serial/UDP endpoint, EDFA 안전 출력, storage와 UI rate를 검증한다. Error가 하나라도 있으면 Start를 거부하고 상태는 `Ready`에 남는다. Warning은 운용자에게 표시하되 Start를 차단하지 않는다.

Start 승인 결과에는 config revision과 JSON snapshot이 포함된다. Phase 4의 session writer가 이 값을 raw metadata와 함께 저장한다.

Processing, storage, UDP queue는 bounded queue를 사용하며 기본 overflow 정책은 `stop_acquisition`이다. Overflow가 발생하면 `Stopping`으로 전환하고 source, queue size/capacity, 마지막 frame id를 보존한 뒤 `Error`에서 운용자 확인을 기다린다.
