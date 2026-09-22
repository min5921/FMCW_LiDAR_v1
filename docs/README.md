# 문서 안내

[프로젝트 첫 화면](../README.md) · [현재 작업 공간](../WORKSPACE.md)

처음에는 **소스 설명서** 또는 **Windows 시작 안내**를 읽으세요. 문서는 현재 기본 제품의 사용·개발 목적별로 묶었습니다.

| 찾는 내용 | 바로가기 |
|---|---|
| 각 폴더와 파일이 하는 일 | [소스 설명서](source/source_guide_ko.md) · [전체 파일 목록](source/source_file_index_ko.md) |
| 다른 PC에서 설치·빌드·실행 | [Windows 시작 안내](guides/windows_setup_ko.md) |
| VS Code 빨간 밑줄·`nmake` 오류 | [VS Code 설정](guides/windows_setup_ko.md#vscode) |
| Basic·CenterPoint·PCDReplay 구분 | [작업 공간 관리](guides/workspaces_ko.md) |
| 실행 파일·배포 ZIP·이전 패키지 위치 | [배포 파일 안내](guides/package_layout.md) |
| 3D 격자의 미터 간격·밝기·확대 | [3D 화면 조작](guides/point_cloud_view_ko.md) |
| Jetson 빌드·배포 | [Jetson 매뉴얼](../deploy/jetson/README_KO.md) |
| MCU 프로젝트 열기 | [펌웨어 README](../src/firmware/mcu/FMCW_LiDAR_MCU/README.md) |

## 목적별 폴더

| 폴더 | 들어 있는 내용 |
|---|---|
| [guides/](guides/README.md) | 개발 시작, 작업 공간, 폴더 구조, 배포 경로 |
| [source/](source/README.md) | 소스 설명서와 파일별 역할 목록 |
| [design/](design/README.md) | 실행 흐름, 설정, 데이터 형식, 저장·처리, GUI 설계 |
| [hardware/](hardware/README.md) | Alazar 지원 사양, MCU·EDFA 통신, 실장비 검증 |
| [reference/](reference/README.md) | CenterPoint·PCDReplay·Waymo 관련 참고 자료 |
| [archive/](archive/README.md) | 날짜별 검증·정리 기록, 초기 계획, 과거 매뉴얼·Word 보고서 |

## 문서를 추가할 때

- 실행·개발 방법은 `guides/`, 소스 구조 설명은 `source/`에 둡니다.
- 동작·데이터 규약은 `design/`, 장비 통신·실측 확인은 `hardware/`에 둡니다.
- 다른 제품 설명은 `reference/`, 작성 시점에 고정된 결과·계획은 `archive/`에 둡니다.
- 문서에 링크를 걸고 해당 폴더의 `README.md`에도 등록합니다.
- 현재 경로는 프로젝트 루트에서의 경로인지, 문서 기준 상대 경로인지 구분합니다.
- 과거 검증 결과는 작성 날짜와 범위를 유지합니다. 현재 제품 범위는 루트 README와 `WORKSPACE.md`를 기준으로 합니다.
