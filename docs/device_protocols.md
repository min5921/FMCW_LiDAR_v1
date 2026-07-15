# Device Protocols

Phase 3의 장비 adapter는 `IDigitizer`, `IEdfaController`, `IMcuController` 뒤에 구현한다. UI와 processing 코드는 vendor API, COM port, Linux tty를 직접 다루지 않는다.

## AlazarTech AutoDMA

공통 `AlazarDigitizer`가 Windows의 `ATSApi.lib`와 Linux의 `libATSApi.so`를 CMake에서 선택한다. ATS-SDK가 없으면 simulator를 포함한 전체 앱은 계속 빌드되고, 실제 adapter의 Connect만 명확한 오류를 반환한다.

현재 hardware adapter 지원 범위:

- ATS9371, System 1 / Board 1 고정 및 `AlazarGetBoardKind` 검증
- internal clock discrete sample rates: 1 kS/s, 2 kS/s, 5 kS/s, 10 kS/s, 20 kS/s, 50 kS/s, 100 kS/s, 200 kS/s, 500 kS/s, 1 MS/s, 2 MS/s, 5 MS/s, 10 MS/s, 20 MS/s, 50 MS/s, 100 MS/s, 200 MS/s, 500 MS/s, 800 MS/s, 1 GS/s
- 입력 범위 +/-400 mV, 50 ohm, DC coupling
- external TTL trigger (`ETR_TTL`), fixed legacy SDK level argument `150`, rising 또는 falling slope
- channel A 또는 channel B 중 하나만 사용
- `ADMA_NPT | ADMA_EXTERNAL_STARTCAPTURE`
- `digitizer.fifo_only_streaming`에 따라 `ADMA_FIFO_ONLY_STREAMING` 선택
- `AlazarAllocBufferU16`을 사용하는 9..16-bit board

수집 순서:

1. board handle과 channel 정보를 확인한다.
2. clock, 선택한 한 채널, external trigger, trigger delay와 AUX trigger input을 설정한다.
3. full record의 pre/post trigger sample을 설정한다.
4. DMA buffer를 할당하고 `AlazarBeforeAsyncRead`를 호출한다.
5. 모든 buffer를 post한 뒤 capture를 시작한다.
6. post 순서대로 buffer 완료를 기다리고, 각 record를 signed `RawFrame`으로 변환한다.
7. buffer의 모든 record를 소비한 뒤 즉시 다시 post한다.
8. Stop, 오류, 소멸 시 반드시 async read를 abort하고 DMA buffer를 해제한다.

한 trigger는 up chirp 시작에서만 들어오며 한 record에 up/down 전체 주기가 들어온다. `up_segment`와 `down_segment`는 record 내 half-open sample range로 metadata에 복사된다. `fifo_only_streaming`은 legacy 장비 기본값인 `true`지만, on-board memory를 사용하는 보드는 acceptance 과정에서 vendor 권장값을 확인해 `false`로 변경한다.

공식 ATS-SDK의 AutoDMA 흐름과 API 설치 위치는 다음 문서를 기준으로 한다.

- https://docs.alazartech.com/ats-sdk-user-guide/latest/programmers-guide.html
- https://docs.alazartech.com/ats-sdk-user-guide/latest/getting-started.html

## MCU UART

legacy STM32 firmware의 line protocol을 유지한다.

- framing: UTF-8/ASCII text, LF 종료, CR은 무시
- `CLR` -> `ACK:CLR`
- `DATA,a,b,c,d,m` -> waveform point 적재, 정상 시 개별 ACK 없음
- `LOAD_DONE` -> `ACK:LOAD_DONE,count`
- `START` -> `ACK:START`
- `STOP` -> `ACK:STOP`
- 오류 -> `ERR:<code>`
- firmware 최대 point 수: 15000

Marker가 켜진 waveform point의 `m` 값은 firmware의 `m >= 200` 조건을 만족하도록 `255`를 전송한다. 한 MCU upload는 `A-scans/B-scan * B-scans/frame` 크기의 전체 raster frame이며 각 B-scan 시작점에서만 marker를 켠다. MCU TIM6의 100 kHz point rate는 전체 waveform cycle time 계산에 사용하고, 실제 B-scan rate는 Alazar DMA buffer 완료 timestamp에서 측정한다.

`McuSerialController`는 Windows COM과 Jetson tty에서 같은 protocol codec을 사용한다. upload는 buffer clear, DATA 전송, loaded count 확인 순서로 수행한다. ACK가 필요한 profile에서는 count가 다르면 Ready로 전환하지 않는다. emergency stop의 ACK가 오지 않으면 serial port를 닫고 연결 오류 상태를 남긴다.

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

광 출력 setpoint는 현재 schema에서 dBm/mW이므로 serial 자동 설정은 APC mode에서만 허용한다. ACC current와 AGC gain setpoint는 별도 typed field가 추가되기 전까지 자동 출력 설정을 거부한다.

현재 vendor 문서에는 status packet의 남은 byte에 대한 alarm/interlock bit 정의가 없다. 따라서 연결 성공만으로 `interlock_closed`를 true로 추정하지 않으며, 해당 bit 정의를 장비 업체에서 확인하기 전에는 미정(false)으로 표시한다.

EDFA profile mode:

- `none`: serial port를 열지 않고 bypass Ready로 동작
- `manual`: 통신 없이 운용자 제어 상태로 동작
- `controlled`: status 확인 후 mode, 안전 범위 내 setpoint, activation ACK를 순서대로 확인

Start는 controlled EDFA 출력 확인, digitizer arm, MCU scan 시작 순서다. Stop과 오류 처리는 MCU stop, digitizer abort/stop, EDFA output off 순서로 진행한다.
