# Hardware Acceptance

이 절차는 Phase 3 adapter를 실제 Windows/Jetson 장비와 연결할 때 사용한다. 현재 개발 PC에서는 simulator와 SDK-header compile check까지만 수행했으며, 아래 항목은 실제 보드와 광학 안전 환경에서 확인해야 한다.

## Safety Gate

- EDFA output path에 적합한 광 파워 미터와 안전 interlock을 설치한다.
- 최초 시험은 EDFA `none`으로 수행한다.
- scanner limit와 laser/EDFA key 상태를 확인한다.
- Stop과 emergency off를 먼저 시험한 뒤 광 출력을 활성화한다.
- 로그에 board id, SDK/driver version, profile id, config revision을 남긴다.

## Windows Alazar

Current workstation discovery with ATS-SDK 25.1.0 reports one ATS9371 at System 1 / Board 1: 12-bit, serial 860928, FPGA 35.3, driver 7.13.12. The application keeps this address fixed, allows one of the supported 12-bit AUX trigger-enable models to be selected, and rejects a selection/hardware mismatch.

1. 보드 모델에 맞는 AlazarTech Windows driver와 ATS-SDK를 설치한다.
2. vendor utility에서 board가 정상 인식되고 self-test를 통과하는지 확인한다.
3. x64 Developer Command Prompt에서 SDK root를 지정한다.

```powershell
cmake --preset windows-msvc-release `
  -DCMAKE_PREFIX_PATH=C:\Qt\6.11.0\msvc2022_64 `
  -DALAZAR_SDK_ROOT=C:\AlazarTech\ATS-SDK\25.1.0
cmake --build --preset windows-msvc-release
```

4. configure log의 `AlazarTech ATS-SDK adapter: enabled`를 확인한다.
5. oscilloscope로 up chirp 시작마다 TTL trigger가 한 번만 발생하는지 확인한다.
6. EDFA/MCU를 끈 finite profile로 channel A 1024 frame을 수집한다.
7. channel B로 동일 시험을 반복한다. A+B 동시 수집은 허용하지 않는다.
8. 모든 frame이 `sample_point`, pre/post 합, up/down segment 범위를 만족하는지 확인한다.
9. `frames_received`, DMA overflow, trigger miss, Stop latency를 기록한다.
10. 연속 mode에서 최소 10분 동안 buffer post/wait/repost와 Stop을 반복하고 driver locked-page 오류가 없는지 확인한다.

## Jetson Alazar

Jetson은 Linux라는 이유만으로 모든 Alazar board가 자동 지원되는 것은 아니다. 먼저 정확한 board model과 JetPack kernel에 맞는 arm64 driver가 있는지 AlazarTech에 확인한다. AlazarTech는 일부 board의 Linux resource 페이지에서 arm64 driver를 별도로 제공한다.

1. board별 arm64 driver를 설치하고 PCIe link width/speed와 IOMMU 설정을 확인한다.
2. ATS-SDK header는 일반적으로 `/usr/local/AlazarTech/include`, library는 system library path의 `libATSApi.so`를 사용한다.
3. `ldconfig -p | grep ATSApi`와 vendor sample NPT acquisition을 먼저 통과시킨다.
4. `deploy/jetson/jetson.env`에서 SDK root를 지정하고 표준 Jetson 빌드를 수행한다.

```bash
bash deploy/jetson/build.sh
```

5. configure log의 `AlazarTech ATS-SDK adapter: enabled`와 CTest 통과를 확인한다.
6. Windows와 같은 A/B 단일 채널 finite test, full-period 검증, 10분 연속 test를 수행한다.
7. GPU/3D UI를 동시에 켠 상태의 PCIe throughput과 thermal throttling은 Phase 7에서 별도 측정한다.

공식 설치 및 Linux 연동 기준:

- https://docs.alazartech.com/ats-sdk-user-guide/latest/getting-started.html
- https://www.alazartech.com/en/linux-drivers/ats9462/13/

## MCU

- Windows COM 또는 Jetson `/dev/tty*` port를 profile에 지정한다.
- 1 point, 100 point, 15000 point waveform upload count를 각각 확인한다.
- malformed DATA와 15001번째 point에서 firmware 오류가 UI telemetry에 전달되는지 확인한다.
- START 전에 trigger 출력이 없고 START ACK 이후에만 scan/trigger가 시작되는지 확인한다.
- STOP ACK 이후 출력 enable과 trigger가 내려가는지 oscilloscope로 확인한다.

## EDFA

- `none` profile에서 port가 없어도 Connect와 acquisition이 성공해야 한다.
- `controlled` 시험은 status query와 measured output이 파워 미터 범위와 일치하는지 먼저 확인한다.
- safe minimum setpoint에서 APC mode, setpoint ACK, activation ACK를 순서대로 확인한다.
- 정상 Stop, serial disconnect, application emergency stop에서 soft activation이 0으로 확인돼야 한다.
- key off 또는 비정상 상태에서 activation 거부가 오류 telemetry로 전달되는지 확인한다.

## Pass Criteria

- A 또는 B 한 채널의 full-period frame이 누락 없이 전달된다.
- up/down segment가 record 안에 있고 서로 겹치지 않는다.
- EDFA `none`과 MCU disabled profile이 하드웨어 없이 동작한다.
- optional device의 connected/ready/running/output 상태가 core telemetry에 나타난다.
- DMA overflow, protocol checksum/ACK 오류, Stop 실패가 성공으로 숨겨지지 않는다.
- Stop 또는 emergency stop 후 digitizer DMA, MCU scan, EDFA output이 모두 비활성화된다.
