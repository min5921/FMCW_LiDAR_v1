# Qt UI MVP

## 1. Scope

Phase 5 Qt UI는 Windows와 Jetson에서 같은 화면과 core runtime contract를 사용한다. 현재 즉시 실행 가능한 기본 runtime은 simulator이며 다음 경계가 적용된다.

- 실제 동작: simulator acquisition, FFTW/CUDA processing, peak tracking, distance/velocity, B-scan, raw/processed binary recording
- 설정 가능: digitizer, laser, optional EDFA, optional MCU, chirp segmentation, processing, storage, UDP endpoint
- Phase 6: UDP sender, 3D point cloud, simulator fault injection
- Phase 7: 실제 Alazar, MCU, EDFA, Jetson target 및 NVMe 장시간 검증

3D는 빈 tab이나 disabled control로 노출하지 않는다. UDP는 Phase 5에서 `CONFIG ONLY`로 표시해 실제 송신 중으로 오인하지 않게 한다.

## 2. Runtime Architecture

`MainWindow`는 navigation, command bar, 입력 control, plot binding만 소유한다. `ApplicationController`는 UI 명령을 Qt queued invocation으로 `RuntimeWorker`에 전달한다.

`RuntimeWorker`는 전용 `QThread`에서 다음 순서를 관리한다.

1. profile validation 및 device/processing configure
2. optional storage writer open
3. processing worker start
4. controlled EDFA output 및 warm-up
5. digitizer arm
6. optional MCU trigger/scan start
7. full-period frame enqueue

STOP은 MCU trigger, digitizer abort/stop, processing drain, storage finalize, controlled EDFA off 순서로 수행한다. UI plot은 `ProcessingSnapshotStore`가 publish한 최신 immutable snapshot만 받는다.

## 3. Page Ownership

| Page | Runtime ownership |
|---|---|
| Overview | readiness, revision, queue, frame, latency, recording status |
| Live View | Time Domain, FFT, Peak Analysis, Distance/Velocity, B-scan, freeze/range/cursor/save |
| Digitizer | A 또는 B channel, sampling, sample point, DMA, trigger |
| Laser / EDFA | laser specification, optional EDFA mode/setpoint/output safety |
| Scan / MCU | geometry, optional MCU port, waveform upload/readiness |
| Processing | FFT/window/DC, peak threshold/search/tracking, frozen segmentation |
| Storage / UDP | raw/processed recording and UDP profile configuration |
| System Log | command, runtime, warning, error log filtering |

Overview에는 plot을 두지 않으며 Processing에는 FFT spectrum을 중복 표시하지 않는다. scanner 전용 Start/Stop도 두지 않는다.

## 4. Run

Windows Debug executable:

```powershell
build\preset-windows-msvc-debug\src\fmcw_lidar_windows.exe
```

Qt DLL이 실행 폴더에 없는 개발 환경에서는 다음 명령으로 배포한다.

```powershell
C:\Qt\6.11.0\msvc2022_64\bin\windeployqt.exe --debug --no-translations build\preset-windows-msvc-debug\src\fmcw_lidar_windows.exe
```

자동 검증 예:

```powershell
build\preset-windows-msvc-debug\src\fmcw_lidar_windows.exe --smoke-test
build\preset-windows-msvc-debug\src\fmcw_lidar_windows.exe --demo-run --live-tab=4 --screenshot=outputs\phase5_bscan.png
build\preset-windows-msvc-debug\src\fmcw_lidar_windows.exe --demo-run --page=5 --capture-segmentation --screenshot=outputs\phase5_segmentation.png
```

`--page`는 0부터 7, `--live-tab`은 0부터 4 범위를 사용한다. demo screenshot은 B-scan line이 완성될 시간을 포함해 5.2초 후 저장하고 자동 종료한다.

## 5. Operator Flow

1. profile을 load하거나 화면에서 설정한다.
2. 상단 validation indicator가 error가 아닌지 확인한다.
3. `Connect`로 simulator runtime readiness를 확인한다.
4. MCU를 사용하는 profile은 Scan / MCU에서 waveform을 upload한다.
5. controlled EDFA는 Laser / EDFA에서 setpoint와 output safety state를 확인한다.
6. global `START`로 전체 session을 시작한다.
7. Live View에서 측정 결과를 확인하고 필요하면 display만 freeze한다.
8. global `STOP`으로 writer metadata finalize까지 완료한다.

잘못된 설정은 START를 막는다. processing/storage queue overflow, writer failure, peak lost `stop_acquisition`은 session stop으로 전달된다.
