# 세 작업 공간 분리 검증

2026-09-22 Windows x64에서 VS 2022/MSVC 19.44, CMake 4.3.2, Qt 6.11.0으로 검사했다.

| 프로젝트 | 전용 preset | 검증한 빌드 기능 | CTest | 패키지 실행 |
|---|---|---|---|---|
| Basic | `windows-base` | FFTW ON, CUDA/Alazar/CenterPoint OFF | 22/22 통과 | 통과 |
| CenterPoint | `windows-centerpoint` | FFTW/CUDA/cuDNN/CenterPoint ON, Alazar OFF | 19/19 통과 | 통과 |
| PCDReplay | `windows-pcd` | FFTW ON, CUDA/Alazar/CenterPoint OFF | 22/22 통과 | 통과 |

세 폴더 모두 기존 cache를 복사하지 않고 각 전용 preset의 새 디렉터리에서 Release 빌드했다.
패키징 스크립트는 개발용 PATH를 제거하고 완성 패키지의 `--smoke-test` 종료 코드 0을 확인했다.
새 패키지는 각 폴더의 `build/package/Windows-Basic`, `Windows-CenterPoint`, `Windows-PCDReplay`에 있다.

## 제품 경계

- Basic UI 테스트는 Overview 시작, 계측 페이지 8개, Simulator/Alazar/RAW source 선택과 RAW 파일 선택, Live 3D 탭의 보존을 확인한다.
- 같은 테스트는 PCD 재생 버튼과 모델 선택/탐지 버튼이 없음을 확인한다. 기존 기본 계측/RAW 재생/runtime/저장 테스트도 통과했다.
- PCDReplay는 기존 실제 MainWindow 테스트로 PCD 열기와 다중 프레임 play/pause/step/rewind를 확인한다. 계측 기능은 제거하지 않았다.
- Basic/PCDReplay의 카메라와 GPU/Painter 렌더링 회귀 테스트도 통과했다. OpenGL 화면 렌더링과 CUDA FFT는 별도 기능이다.
- CenterPoint는 실제 CUDA/cuDNN backend를 포함해 빌드하고 테스트했다. GUI 제목에도 CenterPoint를 표시한다.

## 검증의 한계

이 패키지들의 Alazar adapter는 OFF다. **실제 보드에서 사용할 때에는** 해당 workspace에서 SDK 경로와 `FMCW_WITH_ALAZAR=ON`을 지정해 재빌드·재패키징해야 한다. 기본/PCD에서도 CUDA FFT를 사용하려면 CUDA 옵션을 켜고 재빌드한다. 자세한 명령은 [작업 공간 안내](workspaces_ko.md)에 있다.

실물 DMA/MCU/EDFA, 광학 출력, Jetson ARM64 실행, 장시간 성능 및 실제 가중치를 이용한 CenterPoint 추론은 이번 작업에서 검증하지 않았다.
테스트 수 차이는 제품 기능과 CUDA 포함 여부에 따른 것이다.

## 보존 확인

원래 main의 runtime 변경은 `803da89`와 동일함을 확인했다. 새 Basic/PCD는 그 후의 `f4a0938` 계열을 사용하고, CenterPoint는 `803da89`를 사용한다.
원래 main의 source 변경과 Windows 매뉴얼은 stash `82ab95b33fd68d526f4e24b8b15557b3e65aeebe`에도 보존했다.
기존 브랜치, 빌드·패키지·실측 데이터와 PointCloudFusion의 병합 충돌은 삭제하거나 초기화하지 않았다.
