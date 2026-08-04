# FMCW LiDAR MCU firmware

이 디렉터리는 실제 개발에 사용하는 STM32CubeMX/STM32CubeIDE 프로젝트다.
기존 비교 기준은 `legacy/MEMS_control_v3`에 그대로 두며, 앞으로의 MCU 변경은
이 프로젝트에만 적용한다.

## 프로젝트 열기

1. STM32CubeIDE에서 `File > Import > Existing Projects into Workspace`를 연다.
2. 이 `FMCW_LiDAR_MCU` 디렉터리를 선택한다.
3. CubeMX 설정은 `FMCW_LiDAR_MCU.ioc`에서 변경한다.
4. 코드 생성 후 `Debug` 또는 `Release`를 빌드한다.

기존 launch 파일에는 다른 PC의 절대 경로와 ST-LINK serial이 포함되어 있어 복사하지
않았다. 보드별 Debug Configuration은 각 개발 PC에서 새로 만든다. `Debug/`와
`Release/`는 생성 산출물이므로 Git에 포함하지 않는다.

## Timer와 출력 역할

| 자원 | 핀 | 역할 | 현재 설정 |
|---|---|---|---|
| TIM2 CH1 | PA15 | MEMS mirror drive PWM | 약 18.462 kHz, duty 약 46.15% |
| TIM2 CH3 | PA2 | MEMS mirror drive PWM | 약 18.462 kHz, duty 약 46.15% |
| TIM6 | 내부 IRQ | waveform point 재생 clock | 100 kHz, point period 10 us |
| PA9 | M_Btrig | B-scan 시작 marker | marker point 한 개 동안 High |
| PA11 | Enable | Mirrorcle 출력 enable | idle/stop/error에서 Low |

TIM2 계산은 APB1 timer clock 240 MHz, prescaler 1000, period 13을 기준으로 한다.
TIM6 계산은 timer clock 240 MHz, prescaler 240, period 10을 기준으로 한다.

TIM1 기반 200 kHz 입력/400 kHz 출력 변환은 사용하지 않으므로 CubeMX peripheral,
PE9/PE14 설정, 초기화 및 시작 코드에서 제거했다. Startup/CMSIS에 남는 TIM1 이름은
STM32H750의 전체 interrupt vector와 device definition이므로 정상이다.

## 실행 순서

- 부팅: Enable Low -> UART receive start -> TIM2 PWM start -> DAC reset/reference/bias
- `START`: DAC bias 준비 -> marker Low -> TIM6 counter/flag reset -> Enable High -> TIM6 start
- `STOP`: TIM6 stop -> Enable Low -> marker Low -> DAC bias load -> `ACK:STOP`
- fatal error: Enable과 marker를 즉시 Low로 만든 뒤 interrupt를 정지한다.

UART4는 PA0/PA1, 115200 baud, 8-N-1이다. UART IRQ는 priority 0이고 TIM6 IRQ는
priority 1이다. 수신 ISR은 byte를
512-byte ring에 적재하고 명령 parsing 및 ACK 송신은 main loop에서 처리한다.
START가 TIM6를 활성화한 뒤에도 UART 수신과 ACK가 100 kHz waveform ISR에 막히지
않도록 UART를 우선한다. 정상 재생 중에는 UART traffic이 거의 없으므로 waveform의
고정 marker 위치는 GUI의 B-trigger offset으로 보정한다.

부팅 시 UART4 TX로 다음 진단 메시지를 순서대로 출력한다.

- `BOOT:FMCW_MCU_DIAG_V1`: UART 명령 수신 준비 완료
- `BOOT:TIM2_PWM_READY`: MEMS PWM 시작 완료
- `BOOT:READY`: DAC 초기화까지 완료되어 명령 처리 가능

초기화 실패 시 출력을 비활성화하고 해당 `ERR:BOOT_*`를 1초마다 반복하는 UART 전용
진단 루프에 머문다. SPI TX-ready/EOT 대기는 무한 루프 대신 bounded polling을
사용하므로 DAC 초기화 실패가 UART 전체 무응답으로 이어지지 않는다. 그보다 이른
fatal error는 `ERR:FATAL`을 출력한다.

## Waveform protocol과 메모리

현재 PC/Jetson adapter와 호환되는 line protocol을 유지한다.

```text
CLR
DATA,a,b,c,d,m
LOAD_DONE
START
STOP
```

`m >= 200`은 marker High로 저장되며 PC는 High에 255, Low에 0을 보낸다. 최대
15,000 point이고 현재 frame 하나는 DAC A/B/C/D word와 marker를 저장한다. Debug
build 기준 BSS는 약 303 KB다.

`X/Y/M`만 전송하고 MCU에서 A/B/C/D를 만드는 압축 protocol은 별도 version으로
추가할 수 있다. 다만 먼저 X/Y 단위, center/bias, axis polarity, voltage limit,
calibration table을 확정해야 한다. 그 전까지는 임의의 변환이나 보간을 적용하지 않는다.

## 다음 결정이 필요한 항목

- 현재 TIM6 100 kHz에서 998 point/line이면 marker rate는 약 100.2 Hz다. 목표
  B-scan rate가 200 Hz라면 point rate, 실제 line point 수, trigger 위치를 함께
  확정해야 한다. 이번 변경에서는 legacy의 100 kHz를 임의로 바꾸지 않았다.
- 15,000-point buffer는 998 point/line에서 최대 15 line만 담는다. 더 큰 raster
  frame은 단순 `X/Y/M` point 압축만으로 충분한지 계산한 뒤, compact point,
  chunk streaming 또는 MCU 내부 raster 생성 중 하나를 선택해야 한다.

## 확인된 빌드

- STM32CubeIDE 2.0.0 headless Debug and Release builds
- both builds: 0 errors, 0 warnings
- Debug: text 47,212 bytes, data 100 bytes, BSS 303,020 bytes
- Release: text 27,236 bytes, data 100 bytes, BSS 303,020 bytes
