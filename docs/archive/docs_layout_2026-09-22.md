# README·문서 정리 기록

대상: `FMCW_LiDAR` / `codex/fmcw-base`, 2026-09-22.

## 바뀐 안내

- [루트 README](../../README.md)를 제품 소개 → 목적별 시작 링크 → 실행 → 개발 → 폴더 → 검증 상태 순서로 작성했다.
- [문서 목록](../README.md)을 목적별 입구로 만들고 여섯 폴더에 각각 README를 추가했다.
- [현재 Windows 시작 안내](../guides/windows_setup_ko.md)에 기본/CPU preset, SDK 경로, 빌드·패키징과 VS Code `nmake`/헤더 경로 문제를 정리했다.
- 과거 Windows 매뉴얼·일반 빌드 예제는 `archive/`, 다른 제품 설명은 `reference/`에 보존했다.

## 폴더 기준

| 폴더 | 문서 종류 |
|---|---|
| `guides/` | 실행·개발 시작, 작업 공간, 배포와 프로젝트 구조 |
| `source/` | 현재 소스 설명서·파일별 역할 목록 |
| `design/` | 실행·설정·데이터·저장·GUI 규약과 요구사항 |
| `hardware/` | 장비 사양·프로토콜·실장비 검증 |
| `reference/` | CenterPoint·PCDReplay·Waymo 참고 |
| `archive/` | 이전 계획·매뉴얼·리뷰·검증·Word 보고서 |

## 보존과 확인

- 기존 43개 파일을 이동하기 전후 해시를 확인한 후 Markdown 링크를 갱신했다.
- Word 보고서 9개와 분석 JSON 1개의 내용은 그대로 보존했다.
- 상대 경로·문서 내부 링크·Markdown 코드 블록을 검사한다.
- 파일별 목록 생성 도구를 새 위치에 맞추고 224개 소스 파일의 설명·경로를 확인한다.
- 과거 Word 생성 도구는 이동으로 달라진 저장소·출력 경로만 수정했다. 과거 보고서를 다시 생성하지 않았다.
- `AGENTS.md`, `WORKSPACE.md`, 소스 README, Jetson 안내의 문서 경로도 갱신했다.
- Jetson 소스 ZIP을 갱신하고 원본 파일 및 manifest·ZIP 내용의 일치 여부를 확인한다.

이번 작업은 문서·문서 도구·배포본의 안내 링크 정리다. 앱 C++/CUDA 코드는 수정하지 않아 실행 테스트를 반복하지 않는다.
작업 전 사본, 이동 경로와 확인 결과는 `build/reports/docs-layout-20260922/`에 보관한다.
