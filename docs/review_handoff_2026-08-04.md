# FMCW LiDAR 작업 검토 인수인계

작성일: 2026-08-04 (Asia/Seoul)
대상 저장소: `FMCW_LiDAR`
기준 브랜치/커밋: `main` / `782b1db`
목적: 다른 대화나 작업 세션에서 현재 검토 결과를 그대로 이어서 판단하고 수정할 수 있도록 근거와 우선순위를 남긴다.

## 새 대화에서 먼저 할 일

1. 이 문서를 읽고 `git status --short`로 현재 작업 트리가 이후 변경되지 않았는지 확인한다.
2. 기존 변경은 사용자 작업물이므로 덮어쓰거나 되돌리지 않는다.
3. 아래의 "확정 오류"부터 재현한 뒤 수정 범위를 정한다.
4. 장시간·실물 하드웨어 검증은 실행 환경과 장비가 준비됐는지 확인한 후 진행한다.

이번 검토에서는 제품 소스를 수정하지 않았다. 빌드, 테스트, 정적 검토만 수행했다.

## 한 줄 결론

Windows/STM32 핵심 회귀는 통과했지만 ROS 공식 빌드가 깨져 있고, FFTW 실시간 기준이 불안정하며, 데이터 호환성과 raw 파일 안전성 문제가 있으므로 전체 제품 검증 완료 상태가 아니다.

## 작업 트리 상태

검토 시점 기준:

- tracked 수정 파일 27개
- `Ros_project/` 아래 신규 파일 12개가 untracked 상태
- 최신 좌표계, replay setup 복원, waveform 보관, ROS receiver 관련 변경은 아직 커밋·푸시되지 않음
- `legacy/MEMS_control_v3/.settings/language.settings.xml`은 로컬 환경 hash 변경으로 보이며 최종 커밋에서 제외 여부를 확인해야 함

## 통과가 확인된 항목

### Windows 애플리케이션

- MSVC Release configure 및 전체 빌드 성공
- Qt 6.11, ATS SDK, FFTW, CUDA/cuFFT 활성화 상태로 빌드 성공
- `ctest --preset windows-msvc-release`: 9/9 통과
- Windows 패키지 `FMCW_LiDAR.exe --smoke-test`: exit code 0
- 빌드 EXE와 패키지 EXE SHA-256 일치
  - `EB22DA780FA1A83D43D64677C10324E94186357FB575C735777B22C6C0E0018A`
- 테스트 로그:
  - `build/preset-windows-msvc-release/Testing/Temporary/LastTest.log`

### 처리 및 저장

- CPU 성능 probe: 평균 약 1.66 ms, 최대 약 4.48 ms
- CUDA 성능 probe: 평균 약 0.47 ms, 최대 약 0.83 ms
- 32-block 저장 probe: 약 0.319 GB, 약 4.81 GB/s, `HARD_PASS`

### STM32

- STM32CubeIDE Debug/Release 증분 빌드 성공
- Release 빌드 경고 없음
- 크기: text 27,236 / data 100 / BSS 303,020 bytes

### ROS에서 확인된 범위

- 기존 UDP protocol 전용 smoke 실행 성공
- ROS 패키지를 독립적으로 우회 구성하면 protocol library와 receiver/sender node는 GCC 9.4 Release로 컴파일 성공
- 단, 공식 catkin workspace와 ROS 테스트는 아래 오류 때문에 통과하지 못함

## 확정 오류

### P0-1. ROS 공식 catkin workspace 구성 실패

재현 명령:

```bash
cd Ros_project
source /opt/ros/noetic/setup.bash
catkin_make -DCMAKE_BUILD_TYPE=Release
```

오류:

```text
Unknown CMake command "catkin_workspace"
```

원인 후보:

- `Ros_project/src/CMakeLists.txt:6`에서 표준 catkin toplevel 초기화 없이 `catkin_workspace()`를 직접 호출함
- 표준 `/opt/ros/noetic/share/catkin/cmake/toplevel.cmake` 구조로 수정하거나 정상적인 catkin workspace 초기화가 필요함

### P0-2. ROS gtest 링크 실패

오류:

```text
undefined reference to `main'
```

근거:

- `Ros_project/src/fmcw_lidar_rviz/CMakeLists.txt:47-55`
- `test/udp_point_protocol_test.cpp`에는 `TEST` 6개가 있지만 `main()`이 없음
- 현재 테스트 링크에 `gtest_main`이 포함되지 않음

수정 후보:

- `${GTEST_MAIN_LIBRARIES}` 또는 `gtest_main`을 링크하거나 테스트에 명시적 `main()` 추가

### P0-3. UDP 좌표 의미 변경에도 프로토콜 버전이 v1로 유지됨

근거:

- 프로토콜 버전: `src/network/udp_point_protocol.h:12`
- CPU 좌표 변환: `src/processing/signal_processor.cpp:195-203`
- CUDA 좌표 변환: `src/processing/cuda/cuda_signal_pipeline.cu:298-305`

영향:

- 좌표가 ROS/RViz 기준 `+X forward, +Y left, +Z up`으로 바뀌었지만 header는 계속 v1임
- 구형 수신기는 새 데이터를 유효한 v1 패킷으로 받아 X/Y를 바꾸고 Z 방향을 잘못 해석할 수 있음

필요 결정:

- UDP v2로 올리거나 명시적인 legacy/new coordinate compatibility 정책을 도입

### P0-4. 구형 raw replay 좌표 호환성 문제

근거:

- raw format 버전은 기존 v1/v2 유지: `src/core/frame_types.h:15-16`
- 구형 raw v2에는 `x_index`, `y_index`, `x_angle_deg`, `y_angle_deg`만 저장됨
- fast-axis/source-command 및 coordinate-frame 메타데이터가 없음

영향:

- 새 replay가 과거의 X/Y angle을 새 azimuth/elevation 의미로 해석함
- 특히 과거 Y-fast 데이터는 잘못된 형상으로 복원될 가능성이 큼
- 구형 파일에는 새 `coordinate_frame` sidecar가 없고 loader에도 migration/check가 없음

필요 결정:

- 구형 raw 파일 식별·변환 규칙, 버전 갱신 또는 명시적 사용자 선택 정책 마련

### P0-5. 손상된 raw 파일이 초대형 메모리 할당을 유발할 수 있음

근거:

- `src/storage/binary_storage.cpp:1078-1098`
- `record_count`와 `record_length`의 개별 상한은 있지만 곱한 크기로 vector를 resize한 뒤 남은 파일 길이를 확인함
- 최대 조합은 현실적으로 할당 불가능한 크기가 될 수 있음

영향:

- 손상되거나 조작된 `.bin`을 열면 수십 TB 이상 할당을 시도하여 GUI가 종료될 수 있음

필요 수정:

- 파일 잔여 길이, 합리적 최대 sample/frame 크기 및 allocation cap을 resize 전에 검증
- `std::bad_alloc` 처리와 오류 메시지 추가

## 높은 우선순위 위험

### P1-1. FFTW 실시간 5 ms 기준 불안정

2초 probe를 4회 반복한 결과 FFTW가 2회 `HARD_FAIL`:

- 최대 5.56 ms, deadline miss 1회
- 최대 7.53 ms, deadline miss 4회

CUDA는 반복 실행에서 통과했다.

근거:

- hard 기준: `tests/phase7_realtime_qualification_test.cpp:152-156`
- 일반 probe 반환 정책: `tests/phase7_realtime_qualification_test.cpp:221-227`
- CTest 등록: `tests/CMakeLists.txt:130-136`

주의:

- 일반 2초 probe는 `FMCW_QUALIFICATION_ENFORCE_HARD_PASS=0`이므로 hard fail이어도 기능 결과가 정상이면 CTest는 성공함
- 따라서 현재의 9/9 통과는 5 ms acceptance 통과를 의미하지 않음
- 600초/backend strict acceptance가 별도로 필요함

### P1-2. 업로드 waveform과 기록 waveform이 달라질 수 있음

근거:

- waveform은 먼저 읽어서 장비에 업로드됨: `src/apps/common/application_controller.cpp:450`, `486-493`
- 기록 시작 시 원본 경로를 다시 읽어 archive함: `src/apps/common/application_controller.cpp:159-175`

영향:

- 업로드 후 원본 파일이 수정되면 장비가 사용한 데이터와 recording에 보관된 waveform이 달라져 재현성이 깨짐

### P1-3. Point Cloud 좌표축의 물리 원점이 잘못됨

근거:

- point는 bounding-box center/extent로 정규화됨: `src/apps/common/point_cloud_widget.cpp:169-173`, `268-272`
- 축은 같은 변환 없이 `project(0,0,0)`에서 그림: `src/apps/common/point_cloud_widget.cpp:151-165`

영향:

- 표시되는 축 원점은 센서 원점이 아니라 bounding-box 중심이 됨

## 중간 우선순위 문제

완료된 Replay 항목(2026-08-08):

- 손상된 `.setup.yaml`은 실패 사유를 경고로 남기고 `.raw.json`의 `config_snapshot`으로 fallback한다.
- 정규화된 RAW 경로가 실제로 바뀐 경우에만 setup을 다시 적용한다. 같은 경로에서 포커스만 이동해도 사용자의 미적용 UI 수정값은 보존된다.
- 기록된 digitizer 모델이 현재 capability 목록에 없으면 ATS9371로 조용히 대체하지 않고 복원을 거부하여 현재 설정을 유지한다.
- 성공 로그는 실제 검증된 capability의 표시 이름과 profile ID, 실제 복원 소스를 출력한다.
- 구현과 회귀 시험: `src/apps/common/replay_setup_loader.cpp`, `tests/replay_setup_loader_test.cpp`

잔여 항목:

- fast-axis 판정이 endpoint 차이만 사용하므로 왕복/비단조 trajectory에서 `Unknown` 또는 잘못된 축으로 판정될 수 있음
  - `src/core/scan_trajectory.cpp:53-68`, `94-110`
- ROS assembler가 중복 fragment에도 `updated_at`을 갱신하여 불완전 assembly를 오래 유지할 수 있음
  - `Ros_project/src/fmcw_lidar_rviz/src/udp_point_protocol.cpp:198-201`, `233-243`
- package에는 `qoffscreen.dll`이 없어 `QT_QPA_PLATFORM=offscreen` packaged smoke가 timeout됨
  - 일반 Windows 실행 및 일반 package smoke는 통과함

## 구현 또는 검증이 부족한 요구사항

### 구현 근거가 없는 항목

- processed-frame replay reader
  - 요구사항: `docs/requirements.md:724-737`
  - 현재는 processed writer와 raw replay reader만 확인됨
- 대규모 Point Cloud decimation/LOD
- GPU vertex buffer 기반 Point Cloud rendering
  - 관련 요구사항: `docs/requirements.md:512`, `523`, `1097-1103`
  - 현재 widget은 모든 점을 `QPainter::drawEllipse`로 그림
- simulator fault injection
- diagnostics export
  - 관련 요구사항: `docs/gui_runtime_requirements.md:225-229`

### 아직 실행하지 못한 acceptance

- FFTW와 CUDA 각각 600초 실시간 acceptance
- 약 1.2 TB NVMe 10분 연속 저장 acceptance
- 실제 ATS 10분 DMA/locked-page/handle/overflow 검증
- command-to-angle 물리 교정
- PA9-Alazar 실제 타이밍 검증
- EDFA 실제 광출력 및 안전 검증
- Jetson native ATS/CUDA/NVMe/thermal/장시간 운전
- ROS sender → receiver → RViz 실제 end-to-end 연동

## 단계별 판단

| 단계 | 현재 판단 |
|---|---|
| Phase 0-2 | PASS |
| Phase 3 | 소프트웨어 PASS, 실제 장비 검증 일부 남음 |
| Phase 4-6 | 핵심 구현/회귀 PASS, 전체 요구사항과 UI 최종 회귀 일부 남음 |
| Phase 7.1 | PASS |
| Phase 7.2 | 실제 ATS 장시간 acceptance 미완료 |
| Phase 7.3 A/B/C | 소프트웨어 범위 PASS |
| Phase 7.3D | 600초/backend strict acceptance 미완료, FFTW 단기 불안정 |
| Phase 7.4 | 1.2 TB NVMe acceptance 미완료 |
| Phase 7.5-7.7 | 실제 하드웨어, EDFA, Jetson 검증 미완료 |

## 문서 정합성 문제

- `docs/phase7_execution_plan.md`의 상태가 구간에 따라 `blocked`와 `in_progress`로 혼재
- P7 작업 번호가 어떤 문서에는 P7-001~008, 실행 계획에는 P7-009까지 존재
- STM32 IRQ 우선순위 설명이 실행 계획과 최신 요구사항/상태 문서에서 충돌
- generated raster와 legacy vector waveform 계약 설명이 문서 간 충돌
- `docs/hardware_acceptance.md`의 검증 범위 설명이 이후 실제 ATS/EDFA probe 기록을 반영하지 않음
- 검토일이 2026-07-15로 남아 있어 2026-08-04 변경이 반영되지 않은 문서가 있음

## 권장 처리 순서

1. ROS catkin workspace와 gtest를 수정하고 실제 `catkin_make`/`run_tests` 통과 확인
2. UDP coordinate 변경의 버전 정책과 구형 raw migration 정책 확정
3. RawReplayReader의 사전 크기 검증과 allocation cap 추가
4. FFTW spike 원인을 분석하고 hard fail을 CI/CTest 실패로 연결
5. waveform archive, Point Cloud 축, replay fallback/덮어쓰기 문제 수정
6. processed replay, LOD/VBO, fault injection, diagnostics export의 범위 확정 및 구현
7. 실제 장비와 Jetson 장시간 acceptance 실행
8. 문서의 상태·프로토콜·IRQ·waveform 계약을 하나의 기준으로 정리
9. 불필요한 IDE 환경 파일을 제외하고 변경사항을 의도적으로 커밋

## 새 대화에 전달할 시작 문구

아래 문장을 새 대화에서 사용할 수 있다.

> `docs/review_handoff_2026-08-04.md`를 먼저 읽고 현재 `git status`와 대조해줘. 기존 사용자 변경은 보존하고, 문서에 기록된 P0 문제부터 재현해서 수정 계획 또는 구현을 진행해줘. CTest 9/9는 실시간 hard acceptance 통과가 아니라는 점과 ROS 공식 catkin 빌드가 현재 실패한다는 점을 반드시 반영해줘.

## P0 처리 결과 (2026-08-04)

기존 진단과 작업 트리는 보존한 채 아래 P0 항목을 수정했다.

- P0-1: `Ros_project/src/CMakeLists.txt`를 표준 catkin top-level 초기화 구조로 교체했다.
- P0-2: ROS UDP gtest에 명시적 `main()`을 추가하고 정상 링크 및 실행을 확인했다.
- P0-3: XYZ 의미가 ROS/RViz `+X forward, +Y left, +Z up`으로 바뀐 계약을 UDP v2로 고정했다. Windows 송수신기와 ROS 송수신기는 v1을 거부한다.
- P0-4: 신규 DMA raw를 v3로 올려 trajectory sample, source X/Y command, fast axis/direction, coordinate source, calibration state를 저장한다. v1/v2는 adjacent sidecar가 `ros_x_forward_y_left_z_up`을 명시한 경우에만 replay한다.
- P0-5: replay가 allocation 전에 record/sample 산술, 정확한 metadata 크기, 남은 파일 길이, 100,000-record 한도와 256 MiB batch payload 한도를 검증한다. `bad_alloc`과 `length_error`도 replay 오류로 변환한다.

검증 결과:

- Windows MSVC Release 전체 빌드 성공
- Windows CTest 9/9 성공
- 패키지 `FMCW_LiDAR.exe --smoke-test` 성공
- Release/package EXE SHA-256: `8044310D4B60EDBA72920D7840C71FB2BA7C25EF957BDDCDC9BBA093DB38ADB3`
- Ubuntu 20.04 / ROS Noetic `catkin_make ... run_tests` 성공
- `catkin_test_results`: 12 tests, 0 errors, 0 failures, 0 skipped
- Jetson shell script `bash -n` 성공, 266-file source manifest 전부 일치

이 결과는 P0 소프트웨어 회귀 검증이다. CTest의 짧은 real-time probe는 600초 FFTW/CUDA strict 5 ms acceptance나 실제 ATS/Jetson hardware acceptance를 대체하지 않는다.

## P1 처리 결과 (2026-08-07)

### P1-1. 998-record 실시간 처리

FFTW 경로를 64-record 단위로 16번 나누던 구조에서 하나의 998-record DMA 배치로 변경했다. 외부 계약은 정확히 1,996개 UP/DOWN FFT이며, 내부 FFTW lane은 cache-friendly한 8-transform plan을 유지한다. 완전한 lane 묶음은 FFTW new-array API로 입력과 최종 spectrum workspace에 직접 실행하고 마지막 부분 묶음만 임시 버퍼를 사용한다.

Windows에서는 processing worker 시작 시 OpenMP 팀 준비 완료를 동기화한다. Acquisition 실행 중에만 프로세스를 `HIGH_PRIORITY_CLASS`로 올리고 worker 종료 시 원래 priority class로 복원한다. `REALTIME_PRIORITY_CLASS`는 사용하지 않는다.

검증 결과:

- FFTW short strict: 20/20 통과, 평균의 평균 1.880 ms, 관측 최대 4.924 ms
- CUDA short strict: 10/10 통과, 평균의 평균 0.918 ms, 관측 최대 1.624 ms
- FFTW 600초 strict: `HARD_PASS`
  - 120,241 batches, 120,000,518 records, valid XYZIV 120,000,518
  - queue high-water 1/32, DMA drop 0, reject 0
  - 평균 1.866 ms, p99 2.455 ms, 최대 3.656 ms, deadline miss 0
- CUDA 600초 strict: `HARD_FAIL`
  - 120,240 batches, 119,999,520 records, valid XYZIV 119,999,520
  - queue high-water 1/32, DMA drop 0, reject 0
  - 평균 1.502 ms, p99 2.536 ms, 최대 7.148 ms, deadline miss 3
  - CUDA compute 최대는 2.791 ms로 5 ms 이내였다. 실패 지점은 Windows simulator의 acquisition wakeup/ownership 최대 5.17/5.33 ms이므로 GPU 신호처리량 문제가 아니라 host end-to-end scheduling 지연이다.

따라서 Windows simulator 기준 FFTW 600초 hard gate는 완료됐고 CUDA 신호처리 compute도 기준을 만족하지만, CUDA end-to-end 600초 hard gate는 아직 완료 상태가 아니다. 기준을 완화하거나 초과 batch를 제외하지 않았다. 실제 ATS와 Jetson native 환경에서 동일 gate를 다시 실행해야 한다.

### P1-2. 업로드 waveform 보관

Legacy X/Y/M 파일을 업로드와 기록 시작 시 각각 읽던 흐름을 제거했다. 업로드 전에 원본 바이트를 한 번만 읽고 그 메모리 snapshot으로 MCU command sequence를 생성한다. 업로드가 성공한 경우에만 해당 바이트와 확장자를 보관하며, raw session 시작 시 원본 경로가 아니라 그 exact snapshot을 archive한다. In-memory snapshot과 path 기반 변환이 trigger shift를 포함해 동일한 command sequence를 만드는 acquisition 회귀 시험을 추가했다.

### P1-3. Point Cloud 물리 원점

Point Cloud의 XYZIV 데이터와 좌표 계산은 변경하지 않았다. 센서 원점, XYZ 축 끝점과 Z=0 grid가 점과 동일한 world-to-view 변환을 사용하도록 수정했다. 화면의 축 원점은 더 이상 point bounding-box 중심이 아니며 `+X forward, +Y left, +Z up`의 실제 센서 원점이다.

최종 Windows 검증:

- MSVC Release 전체 빌드 성공
- CTest 9/9 성공
- 패키지 `FMCW_LiDAR.exe --smoke-test` exit code 0
- Package EXE SHA-256: `3C997B001E4676E5944B04C0C5050EF3E1D725622876B81E014768FA2FF6764F`
- Jetson source bundle 재생성, 266-file manifest mismatch 0
- Jetson source ZIP SHA-256: `DCED3B324E90FD938F84F576825C0F8C33BFB7E0B7B35BAB5C7CE0AD148C94EE`
- `deploy/jetson/*.sh` Bash syntax check 통과

## P2 Replay 복원 안정화 결과 (2026-08-08)

- Replay setup 로딩을 Qt UI 이벤트에서 `replay_setup_loader`로 분리했다.
- YAML이 없거나 손상된 경우 JSON sidecar snapshot을 사용하며, fallback 이유는 System Log에 `WARNING`으로 남긴다.
- 같은 RAW 파일의 `editingFinished`는 setup을 다시 적용하지 않는다. 이로써 사용자가 Replay setup 복원 후 수정하고 아직 Apply하지 않은 UI 값이 보존된다.
- 미지원 recorded board profile은 첫 capability로 대체하지 않고 명시적인 오류로 처리한다. 현재 UI setup은 유지된다.
- 정상 복원 로그는 실제 setup source와 검증된 board display name/profile ID를 표시한다.

검증 결과:

- Windows MSVC Release 전체 빌드 성공
- Replay 복원 회귀 시험 성공: 손상 YAML의 JSON fallback, 전체 snapshot 동일성, 미지원 board 거부, 동등 경로 재적용 방지
- Windows CTest 10/10 성공
- 패키지 `FMCW_LiDAR.exe --smoke-test` exit code 0
- Package EXE SHA-256: `C495837A91C2EBCDFE68A2B259E26E233ACF7A9C408DB60E6E95F7DE8024F910`
- Jetson source bundle 재생성: manifest 269개, mismatch 0
- Jetson build/check/package/run Bash 문법 검사 통과
- Jetson source ZIP SHA-256: `6789FAC350F4BCD341FD3A67E01597711FF006526649E9450E81A47A1B96DA8C`

## Vector Bidirectional 사용자 제어 결과 (2026-08-08)

- Legacy X/Y/M waveform에서도 Scan / MCU의 `Bidirectional vector scan` 체크박스를 사용자가 직접 켜고 끌 수 있다.
- OFF/BYPASS는 실제 X/Y command가 감소하는 홀수 line도 acquisition order 그대로 `x_index = record_index`로 배치한다.
- ON은 짝수 B-scan을 정방향으로 유지하고 홀수 B-scan만 `x_index = record_count - 1 - record_index`로 한 번 뒤집는다.
- X/Y command delta는 fast axis 판정에만 사용하며 line 증가/감소 방향을 자동 결정하지 않는다.
- A-scan 내부 time sample과 FFT 입력은 뒤집지 않는다. FFT, peak search, distance/velocity 계산 후 parity가 적용된 공간 index로 XYZIV, B-scan, Point Cloud를 배치한다.

검증 결과:

- 실제 ATS9371 자동 감지 환경의 acquisition/device 회귀 시험 성공
- Windows MSVC Release 전체 빌드 성공, CTest 10/10 성공
- Windows package smoke test exit code 0
- Package EXE SHA-256: `A8271E09DC411F0F5978E9007D67B1A2C7586BA8C59F2E2C041A479C01898F92`
- Jetson source bundle: manifest 269개, mismatch 0, Bash 문법 검사 통과
- Jetson source ZIP SHA-256: `CE5DB421B9F5C6C9A90F1780B1662B3AFCC3885F9AB47FE2531276FE3DF2926C`
