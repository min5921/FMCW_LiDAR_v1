# 과거 문서와 검증 기록

[전체 문서](../README.md) · [현재 시작 안내](../guides/README.md)

작성 당시의 상태·범위·검증 결과를 보존합니다. 현재 설치 방법은 `guides/`, 현재 파일 설명은 `source/`에서 확인하세요.
Word 보고서와 함수 분석 JSON은 재생성하지 않고 원본을 보존했습니다. Markdown의 안내 링크는 현재 문서 위치에 맞췄습니다.

## 2026-09-22 정리·검증

| 문서 | 기록 내용 |
|---|---|
| [기본 v2 구성·검증](basic_v2_2026-09-22.md) | Alazar·CUDA·FFTW 기본 구성과 Windows 검증 |
| [작업 공간 분리 검증](workspace_verification_2026-09-22.md) | 세 작업 공간을 처음 분리했을 때의 상태 |
| [파일 정리](file_organization_2026-09-22.md) | 프로젝트·빌드 자료 정리 기록 |
| [소스 정리](source_layout_2026-09-22.md) | `src` 재배치와 테스트·패키지 검증 |
| [README·문서 정리](docs_layout_2026-09-22.md) | 첫 화면 개편, 문서 분류와 링크 점검 |

## 개발 단계와 리뷰

| 문서 | 기록 내용 |
|---|---|
| [단계별 진행 상태](phase_status.md) | 이전 개발 단계의 진행 상황 |
| [Phase 7 계획](phase7_execution_plan.md) | 장비·성능 확인 계획 |
| [Qt UI MVP](qt_ui_mvp.md) | 초기 화면 구현과 실행 예시 |
| [v2 리뷰 완료 기록](v2_review_completion.md) | 당시 수정 사항과 확인 범위 |
| [2026-09-07 리뷰](review_2026-09-07.md) | 해당 시점의 검토 결과 |
| [2026-08-04 인계](review_handoff_2026-08-04.md) | 이전 검토·인계 사항 |

## 이전 매뉴얼·보고서

- [2026-09-17 Windows 매뉴얼](windows_setup_2026-09-17.md): 당시 GitHub `main`을 재현하는 절차.
- [이전 공통 빌드 안내](build_setup.md): 통합 개발 시기의 빌드 예시.
- [정리 전 프로젝트 README](development_readme.md).
- [소스 구조 Word 보고서](source_code_architecture_report/): 과거 구조 설명 원본.
- [2026-08-31 함수별 Word 보고서](source_reference_2026-08-31/README.md): 8개 Word 문서와 당시 분석 자료.

과거 generator와 분석 JSON에 있는 소스 경로는 작성 시점 기준입니다. 현재 소스에서 과거 보고서를 다시 생성하면 당시 내용이 재현된다고 보장할 수 없습니다.
