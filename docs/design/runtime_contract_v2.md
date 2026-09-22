# v2 Runtime 기준과 읽는 순서

동작 설명 기준일: 2026-09-07, 소스 경로 갱신: 2026-09-22. Windows와 Jetson은 같은 `src/application`, `src/ui` 및 host pipeline을 사용한다. 이 문서는 현재 실행 흐름의 안내이며, 모든 함수의 정확성을 보증하는 감사 보고서가 아니다.

## 1. 읽는 순서

| 순서 | 파일/영역 | 먼저 확인할 내용 |
| --- | --- | --- |
| 1 | `src/core/config/config_types.h`, `src/core/types/frame_types.h` | 설정 단위와 A-scan/DMA/raster 구별 |
| 2 | `src/application/application_controller.cpp` | 실제 `RuntimeWorker`의 configure/connect/start/stop과 callback 연결 |
| 3 | `src/core/acquisition/acquisition_session.cpp`, `src/core/acquisition/continuous_acquisition_worker.cpp` | 장치 순서, 배치 생성, 처리/저장 전달 |
| 4 | `src/processing/processing_service.cpp` | 큐, revision 적용 경계, 종료 시 drain/discard |
| 5 | `signal_processor.cpp`, `processing/cpu`, `processing/cuda` | 동일 처리 규약의 FFTW/CUDA 구현 |
| 6 | `processing_snapshots.cpp` | selected A-scan과 완성 raster publication |
| 7 | `storage/async_storage_service.cpp`, `processing_history.cpp` | 저장 큐, 완료 판정, 당시 처리 설정 보존 |
| 8 | `drivers/replay`, `storage/async_point_cloud_replay.cpp` | raw 재처리와 point-cloud 직접 표시의 차이 |
| 9 | `network/udp_sender_service.cpp`, `ui/main_window.cpp` | 완성 frame 송신, GUI 표시와 로그 |

`ConfigManager`와 `OperationController`는 초기 Phase 2 prototype으로 보존하되 제품 core 링크 대상에서 제외했다. 해당 테스트는 과거 계약 테스트이지 실제 GUI 검증이 아니다. 실제 설정/상태 정책 테스트는 `tests/runtime_orchestration_test.cpp`이며 GUI가 쓰는 동일 `ApplicationController`를 fake adapter와 연결한다.

## 2. 소유권과 스레드

```text
GUI -> ApplicationController -> runtime QThread / RuntimeWorker
                                -> AcquisitionSession (장치 orchestration)
Acquisition worker -> immutable RawFrameBatch
                    -> raw storage queue -> raw writer
                    -> processing queue -> FFTW/CUDA -> snapshot store
                                                       -> selected A-scan display
                                                       -> complete PointCloudSnapshot
                                                          -> GUI / point writer / UDP
Point-cloud file worker -> one-frame prefetch -> GUI display / detection
EDFA monitor -> serialized UART -> short cached-state publication
```

- DMA 메모리는 처리/저장 소비자가 마지막 shared ownership을 해제할 때 반환된다. GUI는 acquisition buffer를 소유하지 않는다.
- UDP와 point-cloud writer는 같은 완성 snapshot을 받는다. 앱의 per-A-scan UDP callback은 제거됐다. UDP packet 생성/유효점 필터링은 송신 worker가 수행한다.
- EDFA `status()`는 UART를 기다리지 않는다. 실패한 telemetry를 과거 정상 수치와 혼동하지 않도록 validity와 마지막 정상 시각을 함께 읽는다.
- 대형 PCD/text 해석은 UI가 아닌 worker에서 진행한다. 세대 번호가 바뀐 결과는 버린다. OS 파일 I/O 한 번 자체를 강제 취소하는 것은 아니므로 네트워크 드라이브의 blocking read는 별도 한계다.

## 3. 설정 적용 계약

| 요청 | 실제 적용/초기화 | 기준 코드/시험 |
| --- | --- | --- |
| 전체 Apply Setup | 수집 중 거부. 정지 상태에서 값/backend 검증 후 적용 및 필요 시 재연결 | `RuntimeWorker::configureRuntime`, runtime test |
| 잘못된 설정 Apply | 후보 검증 실패이면 기존 적용 설정/연결을 유지 | runtime test |
| Apply Processing | backend/queue/overflow 변경은 정지 필요. 허용 처리 변경은 다음 raster 경계, CUDA in-flight 완료 뒤 적용 | `ProcessingService::updateRuntimeConfig` |
| Raw 이력 재생 중 Processing 변경 | 거부. 정지 후 Recorded processing을 끄고 명시적으로 덮어쓰기 | runtime/controller |
| Scanner와 무관한 Apply | `mcuWaveformContractEquivalent`가 참이면 waveform loaded revision 유지 | runtime test |
| Waveform upload 중 Start | 즉시 거부하며 upload 뒤 자동 Start로 대기시키지 않음 | runtime test |
| Stop/E-stop/Disconnect | queued 명령 전에 취소 세대 갱신. upload/arm이 경계에서 중단 | runtime test + serial tests |

전체 Apply를 하드웨어 관점의 원자적 rollback으로 간주하면 안 된다. 사전 값 검증은 연결을 보존하지만 실제 장치 재연결 실패 시에는 오류 상태가 되며 다시 Connect가 필요하다.

`ui.segment_overlay`, `ui.color_map`은 옛 profile 호환 키다. 현재 widget 표시 선택의 권위 있는 설정으로 취급하지 않는다. 자동 생성된 정책표를 현재 GUI 동작표로 사용하지 않는다.

## 4. 자료형·단위·좌표계

| 이름 | 의미/단위 |
| --- | --- |
| `RawFrame`, `ProcessedFrame` | full-period record 1개, 즉 A-scan 1개. 화면의 완성 frame이 아님 |
| `RawFrameBatch` | DMA buffer 1개. 현재 계약에서 B-scan line 1개 |
| `records_per_buffer` | B-scan당 A-scan 수의 원본 설정. `a_scan_count`, `scan.x_pixel_count`는 동기화되는 표현 |
| `scan.y_line_count` / `b_scan_count` | 완성 raster의 B-scan line 수. 운용자 설정 |
| `PointCloudSnapshot` | 완성 raster. UDP/기록은 `complete=true`만 수락 |
| `PointXYZI` | 역사적 이름. 실제 결과는 XYZIV와 validity/provenance를 포함 |
| `x_angle_deg`, `y_angle_deg` | azimuth/elevation. MCU 파일의 X열/Y열이 아님 |
| `sample_point` | ADC record 길이. 보드 모델 제약을 검증하며 sweep rate로 강제 산출하지 않음 |
| `laser.sweep_rate_hz` | chirp/거리 계산과 scan 시각 매핑에 사용하는 레이저 설정. 실제 B-trigger 측정값이 아님 |

공간 좌표는 `+X forward, +Y left, +Z up`, 거리 m, 각도 degree, 속도 m/s다. elevation은 line 0에서 `y_end_deg`, 마지막 line에서 `y_start_deg`로 내려간다. Bidirectional ON이면 홀수 line의 spatial index만 한 번 반전한다. ADC sample 순서나 FFT 입력을 뒤집지 않는다.

Peak threshold를 넘지 못한 값은 invalid/NaN이며 이전 거리로 대체하지 않는다. 선택적 3-point quadratic peak는 기존 승인 기능을 유지했다. 이번 R1~R10에서 FFT/거리/좌표 수식 자체를 재설계하지 않았다.

## 5. 시간 지표

- **5 ms / 200 Hz reference**: 고정 qualification workload 기준. 사용자 장비의 현재 deadline이라고 부르지 않는다.
- **Measured DMA interval**: 실제 수신한 DMA 완료 시각 차이. 100 Hz 실측이면 약 10 ms다.
- **Batch processing ms**: 한 DMA 묶음의 처리 시간. FFT 커널 시간만이 아니며 측정 범위는 service/backend 통계를 따른다.
- **Frame FPS**: 완성 raster 생성률. B-scan rate와 혼동하지 않는다.
- **Display/paint Hz**: GUI 갱신률. 모든 200 Hz 입력을 화면에 한 번씩 그린다는 의미가 아니다.
- **Queue occupancy**: 대기 DMA batch 또는 완성 point frame 개수. 일시 지연 흡수량이며 지속 처리량 부족을 해결하지 않는다.

## 6. 기록과 종료

초기 설정은 `.setup.yaml`, 당시 처리 변경은 `.processing/<first_source_frame_id>_<revision>.yaml`에 보존한다. 변경 요청 시각이 아니라 processing worker의 실제 적용 경계를 기록한다. 재생은 원본 record ID로 이벤트를 고른 뒤 backend는 현재 장비의 것을 유지한다. 이력 폴더 없이 raw 파일만 옮기면 초기 설정 재생으로 동작하므로 session 파일들을 함께 이동해야 한다.

Raw binary/UDP/point-cloud wire version은 이번 작업에서 바꾸지 않았다. `StopResult`는 정리 완료와 측정/저장 성공을 구분한다. 원인 오류를 정리 단계의 정상 종료 로그로 덮지 않는다. 저장 overflow/write/finalize 실패에는 `completed=false`를 남기고 부분 데이터는 삭제하지 않는다.

일반 Stop은 queued processing을 버릴 수 있고 불완전 마지막 raster는 저장하지 않는다. 자연 EOF에서는 processing을 drain한다. `completed=true`는 정상 종료/수락한 저장 데이터 최종화 성공을 뜻하며, trigger 시작부터 모든 sample의 무손실 보증은 아니다. DMA 누락/queue 카운터와 함께 판단한다.

## 7. 배포·검증

`BUILD_FEATURES.txt`는 실제 활성 backend와 Qt/CUDA/compiler/source 정보를 표시한다. `BUILD_SOURCES.sha256`는 host source 파일 해시 목록이다. dirty source는 commit만으로 식별할 수 없으므로 해시를 같이 보관한다. Jetson source ZIP은 별도 `SOURCE_MANIFEST.sha256`로 전체 복사본을 식별한다.

Windows 일반/CUDA-only/CenterPoint 구성을 따로 검사한다. Windows CUDA-only 통과는 Jetson 빌드나 실기 성능 합격이 아니다. Jetson에서는 `deploy/jetson/build.sh`, CTest, packaged 실행, 이후 장시간 DMA/serial 시험을 수행한다. 600초 및 대용량 raw acceptance는 일반 CTest에 자동 실행되지 않는다.
