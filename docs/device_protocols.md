# Device Protocols

Phase 3의 장비 adapter는 `IDigitizer`, `IEdfaController`, `IMcuController` 뒤에 구현한다. UI와 processing 코드는 vendor API, COM port, Linux tty를 직접 다루지 않는다.

## AlazarTech AutoDMA

공통 `AlazarDigitizer`가 Windows의 `ATSApi.lib`와 Linux의 `libATSApi.so`를 CMake에서 선택한다. ATS-SDK가 없으면 simulator를 포함한 전체 앱은 계속 빌드되고, 실제 adapter의 Connect만 명확한 오류를 반환한다.

현재 hardware adapter 지원 범위:

- SDK 25.1.0 기준 12-bit 및 AUX trigger-enable 조건을 만족하는 11개 모델
- ATS9120, ATS9130, ATS9350/51/52/53, ATS9360/62/64, ATS9371/73
- System 1 / Board 1 고정 및 선택 모델과 `AlazarGetBoardKind` 일치 검증
- 선택 모델별 internal-clock discrete sample rate와 input range, 50 ohm, DC coupling
- external trigger range는 모델별 `ETR_TTL` 또는 `ETR_5V`, level code `150`
- positive-slope `AUX_IN_TRIGGER_ENABLE` B-scan gate
- channel A 또는 channel B 중 하나만 사용
- `ADMA_NPT | ADMA_EXTERNAL_STARTCAPTURE`
- 모델의 SDK NPT Scan 설정에 따라 `ADMA_FIFO_ONLY_STREAMING` 선택
- native left-aligned 12-bit sample을 위한 `AlazarAllocBufferU16`

모델별 정확한 rate/range/record/trigger 표는 `docs/alazar_supported_models.md`를 따른다.

수집 순서:

1. board handle과 channel 정보를 확인한다.
2. clock, 선택한 한 채널, external trigger, trigger delay와 AUX trigger input을 설정한다.
3. full record의 pre/post trigger sample을 설정한다.
4. DMA buffer를 할당하고 `AlazarBeforeAsyncRead`를 호출한다.
5. 모든 buffer를 post한 뒤 capture를 시작한다.
6. post 순서대로 buffer 완료를 기다리고, 각 record를 signed `RawFrame`으로 변환한다.
7. buffer의 모든 record를 소비한 뒤 즉시 다시 post한다.
8. Stop, 오류, 소멸 시 반드시 async read를 abort하고 DMA buffer를 해제한다.

한 trigger는 up chirp 시작에서만 들어오며 한 record에 up/down 전체 주기가 들어온다. `up_segment`와 `down_segment`는 record 내 half-open sample range로 metadata에 복사된다. `fifo_only_streaming`은 사용자 설정이 아니라 선택 모델의 SDK `NPT_Scan` 예제 설정을 따른다.

공식 ATS-SDK의 AutoDMA 흐름과 API 설치 위치는 다음 문서를 기준으로 한다.

- https://docs.alazartech.com/ats-sdk-user-guide/latest/programmers-guide.html
- https://docs.alazartech.com/ats-sdk-user-guide/latest/getting-started.html

## MCU UART

활성 firmware는 `src/firmware/mcu/FMCW_LiDAR_MCU/`에 있고 legacy STM32 firmware의 line protocol을 유지한다.

- framing: UTF-8/ASCII text, LF 종료, CR은 무시
- `CLR` -> `ACK:CLR`
- `DATA,a,b,c,d,m` -> waveform point 적재, 정상 시 개별 ACK 없음
- `LOAD_DONE` -> `ACK:LOAD_DONE,count`
- `START` -> `ACK:START`
- `STOP` -> `ACK:STOP,count` (정지 후 RAM에 유지된 waveform point 수)
- 오류 -> `ERR:<code>`
- firmware 최대 point 수: 15000

MCU waveform source는 `legacy_xym_file`과 `generated_raster` 두 mode를 지원한다. 기본 mode인 legacy X/Y/M 파일은 첫 줄의 `sps <rate>`와 이후 `X Y M` 행을 읽는다. X/Y는 legacy converter와 같은 90 V bias, 120 V differential span, 0..200 V DAC mapping으로 A/B/C/D에 변환한다. source SPS가 100 kS/s가 아닐 때만 기존 converter 규칙대로 X/Y linear, M nearest-neighbor 방식으로 100 kS/s에 맞춘다. `M >= 0.5`인 point는 firmware의 `m >= 200` 조건을 만족하도록 `255`를 전송한다. 선택적인 `scan.trigger_shift_samples`는 이 판정 뒤 M bit에만 적용된다. 음수는 marker를 앞당기고 양수는 늦추며, X/Y DAC word는 원래 sample index에 그대로 남는다. 연속 재생 경계에서도 timing이 유지되도록 shift는 전체 waveform을 주기로 순환한다.

활성 기본 파일은 `config/waveforms/mems_xym_100ksps.txt`이며 10,388 points와 12개의 B-trigger rising edge를 가진다. Upload 전에 marker rising edge 수와 UI의 `B-scans / frame`을 비교하며 다르면 전송하지 않는다. `generated_raster` mode만 `A-scans/B-scan * B-scans/frame` 크기로 파형을 만들고 각 B-scan 첫 point에 marker를 켠다. 두 mode 모두 firmware 최대 15,000 points와 TIM6 100 kHz playback rate를 따른다. 실제 B-scan rate는 Alazar DMA buffer 완료 timestamp에서 측정한다.

Host는 원본 M rising edge와 offset 적용 후 출력 M rising edge를 별도로 보관한다. 원본 edge는 A-scan을 X/Y command에 결합하는 논리 line anchor이고, 출력 edge는 실제 PA9 timing이다. 따라서 GUI offset으로 PA9를 보정해도 point-cloud 좌표가 함께 이동하지 않는다. 각 DMA line은 원본 edge부터 `floor(record_index * waveform_rate / laser_sweep_rate)` sample의 X/Y를 사용하며 보간하지 않는다. fast axis와 증가/감소 방향은 line의 실제 command 변화량에서 판정하고, legacy vector waveform에 odd/even 반전을 추가하지 않는다.

Timer/output contract:

- TIM1: 사용하지 않음. 기존 200 kHz 입력을 400 kHz로 변환하던 경로는 제거한다.
- TIM2 CH1/CH3: PA15/PA2의 독립 MEMS mirror drive PWM이며 scan waveform point clock과 분리한다.
- TIM6: 100 kHz waveform playback interrupt이며 각 tick에서 DAC A/B/C/D point 하나를 출력한다.
- PA9 `M_Btrig`: legacy mode에서는 파일의 M high 구간을 그대로 출력하고, generated raster mode에서는 각 B-scan 첫 point에서 10 us High marker를 출력한다.
- PA11 `Enable`: STOP/error 시 ACK보다 먼저 Low가 되어야 한다.

UART4는 115200 baud 8-N-1이다. UART4 IRQ priority 0, TIM6 IRQ priority 1을 사용한다. UART ISR은 byte ring 적재와 정확한 `STOP` line의 fast-path 감지만 수행하고, 일반 command parsing과 blocking ACK 송신은 main loop에서 수행한다. START는 main loop에서 TIM6를 활성화한 뒤 ACK를 보내므로 UART가 TIM6보다 높은 priority여야 ACK timeout과 RX overrun 없이 연결을 유지할 수 있다. 정상 재생 중 UART traffic은 START/STOP 정도로 제한되며, B-trigger의 고정 위치는 M-only GUI offset으로 보정한다.

Legacy firmware는 UART4와 TIM6가 모두 priority 0이고 명령 parsing 및 START ACK까지 UART ISR 안에서 처리했다. 같은 priority의 TIM6가 진행 중인 UART ISR을 선점하지 못했기 때문에 동작했지만, blocking parsing/ACK를 ISR에 두는 구조는 현재 firmware의 receive ring과 fast STOP 설계에는 사용하지 않는다.

`McuSerialController`는 Windows COM과 Jetson tty에서 같은 protocol codec을 사용한다. upload는 buffer clear, DATA 전송, loaded count 확인 순서로 수행한다. ACK가 필요한 profile에서는 count가 다르면 Ready로 전환하지 않는다. 정상 STOP은 firmware buffer를 지우지 않으며 `ACK:STOP,count`로 잔존 point 수를 다시 확인한 뒤 Ready로 돌아가므로 다음 START에는 재업로드가 필요 없다. emergency stop의 ACK가 오지 않으면 serial port를 닫고 연결 오류 상태를 남긴다.

현재 `DATA,a,b,c,d,m`은 PC/Jetson 호환을 위해 유지한다. 향후 `X/Y/M` 압축 protocol은 X/Y 단위, center/bias, polarity, voltage limit 및 calibration table이 확정된 뒤 versioned command로 추가한다.

## EDFA Binary Protocol

legacy vendor command 문서의 9600 baud, 8 data bit, no parity, 1 stop bit 규격을 기본값으로 사용한다.

송신 packet:

```text
EF EF LEN ADDR DATA... SUM
```

수신 packet:

```text
ED FA LEN ADDR DATA... SUM
```

`LEN = DATA byte count + 2`이며 `SUM`은 앞 byte 전체 합의 하위 8-bit다. header, length, checksum이 맞지 않는 응답은 상태에 반영하지 않는다.

현재 사용하는 address:

| 기능 | 송신 address | 응답 address |
|---|---:|---:|
| device status query | `0x00` | `0x00` |
| target power setting | `0x04` | `0x03` |
| APC/ACC/AGC mode setting | `0x06` | `0x05` |
| soft activation on/off | `0x26` | `0x25` |

광 출력 setpoint는 현재 schema에서 dBm/mW이므로 serial 자동 설정은 APC mode에서만 허용한다. ACC current와 AGC gain setpoint는 별도 typed field가 추가되기 전까지 자동 출력 설정을 거부한다. 현재 장비 한계에 맞춘 UI 범위는 0.0~30.0 dBm이다.

현재 vendor 문서에는 status packet의 남은 byte에 대한 alarm/interlock bit 정의가 없다. 따라서 연결 성공만으로 `interlock_closed`를 true로 추정하지 않으며, 해당 bit 정의를 장비 업체에서 확인하기 전에는 미정(false)으로 표시한다.

EDFA profile mode:

- `none`: serial port를 열지 않고 bypass Ready로 동작
- `manual`: 통신 없이 운용자 제어 상태로 동작
- `controlled`: 연결 시 status/mode/target/activation을 읽고, 출력 활성화 시 APC mode, 안전 범위 내 setpoint, activation ACK를 순서대로 확인

COM/tty 포트는 플랫폼에서 검색한 목록을 UI combo box로 제공한다. Digitizer source 선택과 optional serial adapter 선택은 독립적이므로 Simulator/Replay에서도 controlled EDFA를 실제 장비에 연결할 수 있다. Start는 controlled EDFA 출력 확인, digitizer arm, MCU scan 시작 순서다. Stop과 오류 처리뿐 아니라 일반 Disconnect도 serial port를 닫기 전에 EDFA output off를 시도한다.
