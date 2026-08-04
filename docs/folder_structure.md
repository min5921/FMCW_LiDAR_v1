# Folder Structure

현재 프로젝트는 기존 코드를 보존하면서 새 구조를 단순하게 보기 위해 아래처럼 정리한다.

```text
FMCW_LiDAR/
  docs/
  config/
    calibration/
    profiles/
    waveforms/
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

## Where Things Go

### `docs/`

요구사항, 설계 문서, 사용법, 통신 규격, 데이터 포맷 문서를 둔다.

예:

- `requirements.md`
- `system_architecture.md`
- `data_contract.md`
- `build_setup.md`
- `udp_protocol.md`
- `raw_data_format.md`

### `config/`

실행 설정과 calibration profile을 둔다.

예:

- Windows profile
- Jetson profile
- `profiles/`: 사용자/측정 profile
- `calibration/`
- `waveforms/`: Windows와 Jetson package에 포함되는 활성 MCU X/Y/M waveform

현재 계층은 `default.yaml` -> `windows.yaml` 또는 `jetson.yaml` -> `profiles/*.yaml` -> `calibration/*.yaml` 순서로 적용한다. 상세 규칙은 `configuration.md`에 있다.

legacy converter에 보관된 원본을 실제 GUI에서 사용할 때는 활성 사본을 `config/waveforms/`에 둔다. 현재 기본 파일은 `mems_xym_100ksps.txt`이며 GUI의 Scan / MCU 페이지에서 다른 파일을 선택할 수도 있다.

### `data/`

입력 데이터와 검증용 데이터를 둔다.

- `raw/`: 측정 원본 raw ADC data
- `processed/`: 처리된 결과 데이터
- `samples/`: 장비 없이 테스트할 작은 샘플 데이터

### `hardware/`

회로, 광학계, 기구, 부품 데이터시트처럼 하드웨어 관련 자료를 둔다.

처음부터 세부 폴더를 많이 만들지 않고, 실제 파일이 생기면 그때 나눈다.

### `legacy/`

기존에 작성한 원본 코드를 보관한다.

현재 기존 코드는 여기에 있다.

```text
legacy/fmcw_lidar_v2_legacy/
legacy/MEMS_control_v3/
legacy/EDFA-Amplifier-V20240219/
```

원칙:

- 원본 비교 기준으로 둔다.
- 직접 수정하지 않는다.
- 새 구조로 옮길 때는 `src/`로 복사하거나 새로 정리한다.
- MCU/STM32 관련 기존 코드는 `legacy/MEMS_control_v3/`에서 참조하고, 새 firmware/protocol 정리는 `src/firmware/mcu/`에 둔다.
- 활성 CubeMX/CubeIDE 프로젝트는 `src/firmware/mcu/FMCW_LiDAR_MCU/`이며 legacy와 별도 프로젝트 이름을 사용한다.
- EDFA vendor controller, FTDI driver, command PDF는 `legacy/EDFA-Amplifier-V20240219/`에서 참조하고, 새 EDFA 제어 코드는 `src/drivers/edfa/`에 둔다.

### `outputs/`

실행 중 생성되는 결과물을 둔다.

예:

- screenshots
- exported point cloud
- diagnostics package
- session summary
- plot image

### `src/`

새로 정리할 실제 개발 코드를 둔다.

- `apps/windows/`: Windows 실행 앱과 상업용 UI
- `apps/jetson/`: Jetson/Linux Qt UI 실행 앱
- `core/`: 공통 state machine, config, frame bus, session, telemetry
- `drivers/`: Alazar, MTI, serial, UDP, OS별 adapter
- `drivers/alazar/`: Windows/Jetson 공통 AlazarTech adapter와 OS별 SDK wrapper
- `drivers/edfa/`: EDFA optional driver, output on/off, optical output setting, simulator
- `drivers/mcu/`: STM32 waveform command/ACK protocol과 controller
- `drivers/serial/`: Win32 COM과 Jetson/Linux tty 공통 transport
- `drivers/simulator/`: 장비 없는 full-period acquisition과 optional device simulator
- `network/`: UDP point packet codec and asynchronous sender service
- `processing/`: GPU FFT, CPU FFT, peak detection, distance/velocity
- `processing/cpu/`: active FFTW implementation
- `processing/cuda/`: active CUDA/cuFFT implementation compiled from `.cu`
- `storage/`: raw writer, processed writer
- `visualization/`: 2D plot, heatmap, 3D point cloud 공통 로직
- `firmware/mcu/FMCW_LiDAR_MCU/`: 활성 CubeMX/CubeIDE firmware, UART command, MEMS 제어 protocol

### `tests/`

FFT 비교, 설정 검증, 저장 포맷, replay, UDP 수신기, MCU protocol, EDFA optional-device 테스트를 둔다.

## Current Rule

당분간 새 코드는 `src/` 아래에만 만든다.

기존 코드는 `legacy/`에서 바로 수정하지 말고, 필요한 파일을 `src/` 아래 역할에 맞게 옮겨서 정리한다.

`legacy/` paths must not appear in active CMake source lists, includes, or runtime calls.
