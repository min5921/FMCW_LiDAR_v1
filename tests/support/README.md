# 테스트 보조 코드

| 폴더 | 용도 | 연결되는 대상 |
|---|---|---|
| `config_prototypes/` | 과거 ConfigManager·OperationController의 계약 검증 | `fmcw_config_tests`만 |
| `point_cloud_replay/` | 점군 파일 포맷·비동기 로딩·렌더링 회귀 검증 | `fmcw_replay_test_support`를 사용하는 테스트만 |

이 파일들은 기본 Windows/Jetson 실행 앱에 링크하지 않습니다. 제품 기능으로 PCD 파일 재생을 제공하지 않습니다.
현재 기본 제품의 실행 제어는 `src/application/application_controller.*`, RAW 재생 장치는 `src/drivers/replay/`에 있습니다.
