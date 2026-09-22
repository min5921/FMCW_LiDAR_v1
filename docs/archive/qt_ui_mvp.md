# Qt UI MVP

## 1. Scope

Phase 5 Qt UI는 Windows와 Jetson에서 같은 화면과 core runtime contract를 사용한다. 현재 즉시 실행 가능한 기본 runtime은 simulator이며 다음 경계가 적용된다.

운용자는 실행 파일을 직접 실행하며 별도 command-line theme option을 사용하지 않는다. Windows와 Jetson 모두 dark theme가 기본이다.

- 실제 동작: simulator acquisition, FFTW/CUDA processing, independent peak detection, distance/velocity, B-scan, 3D point cloud, raw/processed binary recording, UDP point-frame transmission
- 설정 가능: digitizer, laser, optional EDFA, optional MCU, chirp segmentation, processing, storage, UDP endpoint/packet/queue policy
- Phase 6: selected A-scan display, UDP sender, 3D point cloud
- Phase 7: 실제 Alazar, MCU, EDFA, Jetson target 및 NVMe 장시간 검증

3D는 Live View의 실제 동작 tab으로 제공한다. UDP 상태는 `OFF`, `READY`, `TX`로 구분해 설정 상태와 실제 송신 상태를 혼동하지 않게 한다.

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

Phase 7.2 replaces timer-driven acquisition polling with `ContinuousAcquisitionWorker`. The worker blocks on one complete DMA buffer and publishes one immutable `RawFrameBatch`. Current contiguous ownership retains an ATS DMA lease until input transfer releases it, then reposts that buffer independently of final FFT/result completion. The Qt timer only publishes telemetry and the latest UI snapshots. The Digitizer page owns the Simulator, supported AlazarTech ATS, and Raw Replay source selector and replay file controls.

| Page | Runtime ownership |
|---|---|
| Overview | readiness, revision, queue, frame, latency, recording status |
| Live View | Time Domain, FFT, Peak Analysis, Distance/Velocity, B-scan, freeze/range/cursor/save |
| Digitizer | supported ATS model at System 1 / Board 1, A 또는 B channel, model-specific sampling rate/range, DMA, detailed trigger |
| Laser / EDFA | laser specification, optional EDFA mode/setpoint/output safety |
| Scan / MCU | frame geometry, records-per-buffer A-scans/B-scan, operator B-scans/frame, measured DMA rate/frame time, full-frame MCU readiness |
| Processing | FFT/window/DC, independent peak threshold/search, frozen segmentation |
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

`--page`는 0부터 7, `--live-tab`은 0부터 5 범위를 사용한다. demo screenshot은 complete raster B-scan frame이 완성될 시간을 포함해 5.2초 후 저장하고 자동 종료한다.

## 5. Phase 6 Extension

Live View now includes a functional `3D Point Cloud` tab, and Storage / UDP starts a real asynchronous sender with global START. Time Domain and FFT use the selected A-scan record from each DMA buffer; all aggregate processing, UDP, and storage paths remain unfiltered.

## 6. Operator Flow

1. profile을 load하거나 화면에서 설정한다.
2. 상단 validation indicator가 error가 아닌지 확인한다.
3. `Connect`로 simulator runtime readiness를 확인한다.
4. MCU를 사용하는 profile은 Scan / MCU에서 waveform을 upload한다.
5. controlled EDFA는 Laser / EDFA에서 setpoint와 output safety state를 확인한다.
6. global `START`로 전체 session을 시작한다.
7. Live View에서 측정 결과를 확인하고 필요하면 display만 freeze한다.
8. global `STOP`으로 writer metadata finalize까지 완료한다.

잘못된 설정과 적용되지 않은 설정은 START를 막는다. processing/storage queue overflow와 writer failure는 session stop으로 전달된다.
