# 장비와 통신

[전체 문서](../README.md)

| 문서 | 내용 |
|---|---|
| [Alazar 지원 사양](alazar_supported_models.md) | 보드별 지원 설정과 확인 사항 |
| [장치 프로토콜](device_protocols.md) | MCU·EDFA 등의 명령·응답 규약 |
| [실장비 검증 절차](hardware_acceptance.md) | 실제 수집·제어·성능을 확인하는 절차 |
| [MCU 펌웨어 설명](../../src/firmware/mcu/FMCW_LiDAR_MCU/README.md) | CubeIDE 프로젝트, timer·파형·부팅 동작 |

PC/Jetson의 장치 연결 코드는 `src/drivers/`, MCU에서 실행되는 코드는 `src/firmware/`에 있습니다.
