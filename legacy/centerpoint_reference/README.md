# 기본 작업 공간에 남아 있던 CenterPoint 참고 소스

2026-09-22 소스 정리에서 기본 제품에 연결되지 않던 `src/detection/` 14개 파일과
`tests/detection_input_builder_test.cpp`를 이곳으로 옮겼습니다. 이동 당시의 내용은 그대로 보존했습니다.

이 폴더는 과거 통합 구조를 참고하기 위한 보관본입니다. 예전 include 경로도 그대로 남아 있으며,
현재 기본 제품의 CMake는 이 소스와 테스트를 빌드하지 않습니다. 독립적으로 빌드하는 배포본도 아닙니다.

CenterPoint 기능을 수정하거나 실행하려면 별도 `FMCW_LiDAR_CenterPoint` 작업 공간의
`codex/centerpoint` 브랜치를 사용하세요. 이 작업에서 그 작업 공간은 변경하지 않았습니다.
