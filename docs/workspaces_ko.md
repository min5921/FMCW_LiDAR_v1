# 세 프로젝트 작업 공간 관리

분리일: 2026-09-22. 한 Git 저장소의 **서로 다른 브랜치를 서로 다른 worktree 폴더에서** 관리한다.

| 프로젝트 폴더 | 전용 브랜치 | 범위 | Windows preset |
|---|---|---|---|
| `FMCW_LiDAR` | `codex/fmcw-base` | Alazar/MCU/EDFA 계측, FFT, Live 3D, 저장, RAW 재생. 탐지와 PCD 파일 재생 UI 제외 | `windows-base` |
| `FMCW_LiDAR_CenterPoint` | `codex/centerpoint` | 기본 계측과 CenterPoint 객체 탐지, 파일 입력 | `windows-centerpoint` |
| `FMCW_LiDAR_PCDReplay` | `codex/pcd-replay` | 계측 기능 유지 + PCD/pointcloud 재생, 객체 탐지 제외 | `windows-pcd` |

각 폴더의 `*.code-workspace` 파일을 별도 VS Code 창으로 열거나, 사용하는 개발 도구에 각 폴더를 별도 프로젝트로 등록한다.
세 폴더를 하나의 작업 루트로 열어 동시에 수정하는 대신, 작업할 버전의 폴더 하나를 연다.

## 코드와 변경 공유

- `main`은 과거 통합 이력 `f5e63ef`에 보존했다. 현재 세 제품의 기본 작업 브랜치로 사용하지 않는다.
- 최신 R1~R10 안정화 수정은 기존 `803da89`와 동일함을 확인하고 세 프로젝트에 유지했다.
- 기본/PCD 버전은 `f4a0938`의 3D 카메라 수정도 유지한다. 기본 버전은 여기서 PCD 재생 UI와 명령행 파일 열기를 제거했다.
- CenterPoint는 `803da89`를 기준으로 한다. 예전 `fe8ac2d`에 머물던 CenterPoint 폴더에 최신 runtime/저장/정지/재생 수정을 반영했다.
- PCD의 기존 패키징/문서 수정 6개도 보존했다. PCD는 계측을 제거한 전용 뷰어가 아니다.
- 제품 브랜치 전체를 다른 제품이나 `main`에 병합하지 않는다. 공통 오류 수정만 작은 커밋으로 만들고 필요한 프로젝트에 `git cherry-pick <commit>`하여 각 프로젝트에서 검사한다.
- `src/detection` 등 이전 이력의 일부 소스/문서는 기본/PCD 폴더에도 남아 있지만 앱에 링크되지 않는다. 실제 제품 범위는 이 문서와 각 `WORKSPACE.md`, CMake 구성이 기준이다.
- 모델 가중치·SDK·실측 데이터·build cache는 Git으로 전달되지 않는다. 다른 PC에서는 설치/복사하고 재빌드한다.

## 작업 시작과 커밋

작업할 폴더에서 다음을 먼저 확인한다.

```powershell
git branch --show-current
git status --short --branch
```

폴더와 브랜치가 위 표와 일치해야 한다. 수정한 파일을 검토해 명시적으로 stage하고 해당 제품 브랜치에 커밋한다.
현재 다른 worktree에서 사용 중인 제품 브랜치를 같은 폴더에서 번갈아 checkout하지 않는다.

## Windows 빌드

VS 2022 x64 Developer PowerShell을 사용한다. Qt 6 MSVC x64, CMake, FFTW를 설치한 뒤 실제 경로를 지정한다.

```powershell
$env:QT_ROOT = 'C:\Qt\6.11.0\msvc2022_64'
$env:FFTW_ROOT = 'C:\DEV\vcpkg\installed\x64-windows'
$env:PATH = "$env:QT_ROOT\bin;$env:FFTW_ROOT\bin;$env:PATH"
```

기본 버전은 해당 폴더에서 다음을 사용한다.

```powershell
cmake --preset windows-base "-DCMAKE_PREFIX_PATH=$env:QT_ROOT" "-DFFTW_ROOT=$env:FFTW_ROOT"
cmake --build --preset windows-base
ctest --preset windows-base
.\deploy\windows\package.ps1
```

PCD 버전은 `windows-base`를 `windows-pcd`로 바꾼다. 두 preset은 우선 CPU FFTW로 실행할 수 있게 CUDA/Alazar를 OFF로 둔다.
실제 장비 또는 CUDA 처리에는 configure에 `-DFMCW_WITH_ALAZAR=ON -DALAZAR_SDK_ROOT=<SDK root>` 또는 `-DFMCW_WITH_CUDA=ON -DFMCW_REQUIRE_CUDA=ON`을 추가한다. 설치된 기능과 GUI backend를 일치시킨다.
CenterPoint는 CUDA/cuDNN과 외부 runtime 소스가 필수다. 해당 폴더에서 실제 경로를 지정한다.

```powershell
cmake --preset windows-centerpoint `
  "-DCMAKE_PREFIX_PATH=$env:QT_ROOT" "-DFFTW_ROOT=$env:FFTW_ROOT" `
  "-DFMCW_CENTERPOINT_SOURCE_DIR=$env:FMCW_CENTERPOINT_SOURCE_DIR" `
  "-DFMCW_CUDNN_ROOT=$env:FMCW_CUDNN_ROOT"
cmake --build --preset windows-centerpoint
ctest --preset windows-centerpoint
.\deploy\windows\package.ps1
```

모든 명령은 앞 명령의 성공을 확인한 후 진행한다. 개인 절대 경로는 명령행 또는 무시되는 `CMakeUserPresets.json`에만 둔다.
가중치는 추론 실행에 별도로 필요하며 GUI에서 선택한다. CTest와 smoke test는 실제 가중치 추론·Jetson·계측 하드웨어 인증을 뜻하지 않는다.

## 현재 배포 경로

| 폴더 | 이 분리 작업의 Windows 패키지 |
|---|---|
| `FMCW_LiDAR` | `build/package/Windows-Basic/FMCW_LiDAR.exe` |
| `FMCW_LiDAR_CenterPoint` | `build/package/Windows-CenterPoint/FMCW_LiDAR.exe` |
| `FMCW_LiDAR_PCDReplay` | `build/package/Windows-PCDReplay/FMCW_LiDAR.exe` |

실행 창 제목과 패키지의 `BUILD_FEATURES.txt`에서 `WorkspaceVariant`를 확인한다. 이전 package_archive 폴더와 기존 실행 파일은 보존되며 새 소스 빌드로 자동 갱신되지 않는다.
패키징은 해당 workspace preset의 빌드를 사용한다. Jetson은 각 폴더의 `deploy/jetson/build.sh`를 사용하며 기본/PCD에서 CenterPoint를 켤 수 없다.

## 다른 PC로 가져오기

세 전용 브랜치를 GitHub에 푸시한 뒤 사용할 절차다. 로컬 브랜치만 만든 상태에서는 새 PC에서 받을 수 없다.

```powershell
git clone --branch codex/fmcw-base https://github.com/min5921/FMCW_LiDAR_v1.git FMCW_LiDAR
Set-Location FMCW_LiDAR
git worktree add -b codex/centerpoint ../FMCW_LiDAR_CenterPoint origin/codex/centerpoint
git worktree add -b codex/pcd-replay ../FMCW_LiDAR_PCDReplay origin/codex/pcd-replay
```

각 폴더에서 본인 브랜치를 pull/push한다. 전체 branch push나 강제 push는 사용하지 않는다.

## 보존한 이력과 복구 자료

- 옛 `main`, `codex/jetson-centerpoint-v1`, `codex/pcd-replay-only` 브랜치를 보존했다.
- `FMCW_LiDAR_PointCloudFusion`은 이전 merge 충돌과 staged 변경을 그대로 보존한 과거 작업 공간이다. 현재 세 제품의 작업 폴더가 아니다.
- `FMCW_LiDAR_ROS` 역시 과거 보조 작업 공간으로 보존했다. 현재 ROS 수신 코드는 제품 소스의 `Ros_project/`에도 있다.
- 분리 전 전체 ref bundle, working/staged patch, 변경 파일 사본·SHA256은 기본 폴더의 `build/workspace-separation-backup-20260922/`에 있다.
- 기본 폴더의 원래 미커밋 source는 `Preserve main source before three-workspace separation 2026-09-22`라는 stash에도 보존했다. 새 제품 브랜치에 무조건 stash pop하지 않는다.
- IDE 개인 설정인 `legacy/MEMS_control_v3/.settings/language.settings.xml` 변경은 제품 커밋에 넣지 않고 원래 폴더에 남겼다.

## 파일 정리 이후의 시작 위치

각 제품 루트의 `RUN.cmd`로 실행하고, `docs/README.md`에서 문서를 찾습니다.
과거 실행본은 `build/package_archive/2026-09-22/`, 검증·작업 자료는 `build/archive/2026-09-22/`에 있습니다.
예전 CMake cache는 제거했으며 현재 제품 preset의 cache는 유지했습니다. [정리 기록](file_organization_2026-09-22.md)을 참고하세요.
