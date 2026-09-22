# FMCW LiDAR v2: R1~R10 구현 기록

날짜: 2026-09-07. 기준 commit: `f5e63ef` (`FMCW LiDAR v2`). 이 문서는 해당 기준 이후 변경 기록이며 전체 repository 함수 감사 또는 실제 장비 성능 인증은 아니다. 기존 legacy/MCU/worktree/측정 파일은 보존했다.

## 구현 추적

| 항목 | 변경 | 검증 근거 |
| --- | --- | --- |
| R1 | EDFA I/O와 cached status mutex 분리, 정상 시각/invalid telemetry 명시 | `edfa_concurrency_test.cpp`: 지연/timeout/복구/8 batch 진행 |
| R2 | overflow/write/finalize/예외를 세션 실패로 처리, 두 writer 결과 일치 | `storage_failure_test.cpp`: raw/point 각각 오류 주입, 실제 JSON downgrade |
| R3 | StopResult 단계 오류 집계, queued 이전 취소, upload 중 Start 거부 | `runtime_orchestration_test.cpp`: storage/EDFA off 실패, warmup/upload 취소 |
| R4 | 실제 처리 경계의 config 이벤트 기록, raw replay 이력/override | `processing_history_test.cpp`: threshold 전후 XYZIV/validity/revision, loop/old file/partial history |
| R5 | required backend, DLL 존재/테스트 환경, source/feature manifest, 기존 패키지 보존 | 일반/CenterPoint 빌드 및 새 package 실행 검사 |
| R6 | 실제 Qt controller 테스트 주입점, 후보 설정 사전 검증, prototype 제품 링크 제외 | invalid Apply 유지, 연결 실패 복구, Start/Stop/Start, scanner 무관 Apply |
| R7 | 완성 PointCloudSnapshot을 UDP/저장/화면이 공유, 앱 per-record callback 제거 | network/storage/pipeline 회귀 시험 |
| R8 | CUDA-only에서도 realtime/storage/history/runtime target 생성 | FFTW OFF 독립 빌드와 CTest 목록/실행 |
| R9 | 1-frame 비동기 replay, 세대 취소, incremental log, PCD reader 공유 | async 교체/EOF/rewind/error 복구 및 입력 format 시험 |
| R10 | 200 Hz reference와 실측 DMA 간격 분리, 현재 규약/읽는 순서/과거 문서 표시 | `runtime_contract_v2.md`, GUI screenshot 및 기존 계약 시험 |

## 검증 진행

- 일반 Windows: 최종 빌드 및 CTest 19/19 통과(13.60초). 자연 raw EOF의 16개 record 처리 완료와 EDFA off ACK 실패도 실제 Qt controller fixture로 검증했다.
- CUDA-only Windows: clean 독립 빌드 및 CTest 17/17 통과(11.52초). Storage fixture의 FFTW 고정 profile을 수정했고 FFTW 없이 해당 비교도 실행한다. 제외된 두 시험은 FFTW 성능/realtime 시험이다.
- CenterPoint Windows: ATS/FFTW/CUDA/CenterPoint ON 빌드 및 CTest 19/19 통과(13.37초). 공통 cuDNN 검색 경로를 보완했다. weights를 사용하는 실제 추론 합격을 뜻하지 않는다.
- Git diff whitespace 검사 통과. Jetson build/package Bash 구문 검사 통과.
- 핵심 동시성/저장 실패/설정 이력/비동기 replay/실제 runtime 5종을 각각 10회 연속 실행해 모두 통과(총 49.76초).
- 일반/CenterPoint Windows 새 패키지: 개발용 PATH를 제거한 시작 검사 통과. Qt/FFTW/cuFFT/ATS 및 MSVC CRT/OpenMP DLL 포함. CenterPoint 패키지는 cuDNN도 포함한다.
- Overview/Digitizer/Processing 1480x900 screenshot을 확인해 변경한 text와 control 겹침이 없음을 확인했다. `build/v2-page-0.png`, `v2-page-2.png`, `v2-page-5.png`에 보관한다. 대형 PCD 재생 시 사용자 버튼 응답의 실측 stress 시험을 대체하지 않는다.
- 두 Windows 패키지의 host source manifest SHA256이 `a44ce0f253b49efc98f76b265533ad45dcfa002779835846d4e6cc92a9cf39f1`로 일치한다. 배포 스크립트와 문서는 이 host C++ manifest 범위 밖이며 Jetson 전체 source manifest에 포함된다.

일반 Windows의 2초 simulator realtime probe: FFTW 평균 1.869 ms / 최대 2.814 ms, CUDA 평균 0.920 ms / 최대 1.409 ms. 양쪽 DMA drops 0, rejected batches 0, 5 ms reference 초과 0. 이 수치는 짧은 이 PC 시험이며 Jetson/실제 DMA/장시간 보증으로 해석하지 않는다.

## 유지한 계약

- FFTW/CUDA의 승인된 FFT/peak/distance/velocity/XYZ 수식과 optional quadratic peak는 이번에 바꾸지 않았다.
- raw/point-cloud/UDP binary version, 표준 XYZ 축, 홀수 line spatial alignment, NaN threshold 규약 유지.
- 기존 raw에서 처리 이력이 없으면 초기 setup 재생을 유지한다. 새 기록은 session sidecar와 `.processing` 폴더를 함께 이동해야 당시 변경을 재현한다.
- 큐 overflow 후 부분 파일은 삭제하지 않는다. metadata 쓰기 자체가 실패하면 파일의 완료 표시는 신뢰할 수 없으므로 runtime 오류와 함께 판단한다.
- 일반 Stop의 pending processing discard와 자연 replay EOF drain을 구분한다.

## 배포 위치

- 일반 Windows 실행: `build/package/FMCW_LiDAR_v2/FMCW_LiDAR.exe` (ATS/FFTW/CUDA, CenterPoint OFF).
- CenterPoint 포함 실행: `build/package/FMCW_LiDAR_v2_CenterPoint/FMCW_LiDAR.exe` (ATS/FFTW/CUDA/CenterPoint ON). 실제 weights는 GUI에서 별도 선택한다.
- Jetson 빌드용 source: `build/package/FMCW_LiDAR_v2_Jetson_Source.zip`. ARM64 실행 파일이 아닌 사용자 빌드용 소스다. source revision은 기준 commit에 이번 미커밋 수정을 포함한 `f5e63ef...-dirty`이며 전체 파일 해시는 `SOURCE_MANIFEST.sha256`에 기록한다.

기존 package 폴더는 이번 새 출력으로 덮어쓰지 않는다. 향후 Jetson 재패키징은 기존 runtime 폴더를 `.previous-*`로 보존한다. Source export는 기존 출력이 있으면 새 경로를 요구하며 명시적 `-ReplaceExisting`만 덮어쓰기를 허용한다.

## 남은 실기 검증과 범위

1. Jetson ARM64/Qt 6.2/CUDA 12.6에서 사용자 빌드, CTest, 실행 검증. Windows CUDA-only는 이 환경의 대체 시험이 아니다.
2. Alazar DMA와 EDFA timeout 동시 시험, MCU upload/STOP ACK 지연/단절, 실패 후 10회 재시작.
3. 실제 200 Hz/사용자 100 Hz workload 장시간 처리·NVMe 저장·온도/전력 시험. 600초 realtime 및 약 1.2 TB storage acceptance는 이번 일반 CTest에 포함하지 않았다.
4. CenterPoint 실제 weights 추론, ROS 수신/재조립, 실제 표시 FPS/버튼 응답의 장시간 계측.
5. 하드웨어 E-stop 지연은 미측정. 협력적 취소는 진행 중 serial transaction을 즉시 선점하지 않는다.

큰 MainWindow/RuntimeWorker 전체 분할, 모든 함수 설명의 수동 재작성, 원본/실측 파일 삭제는 R1~R10 수정과 섞지 않았다. 필요한 기능을 보존한 상태에서 별도 변경으로 진행할 수 있다. 과거 자동 DOCX 색인은 보존하고 추정 설명임을 표시했다.
