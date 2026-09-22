# 기본 FMCW 소스 파일별 역할 목록

[처음 읽는 설명서](source_guide_ko.md) · [짧은 소스 지도](../../src/README.md)

기준: 2026-09-22 정리 이후 기본 작업 공간. **`src/` 파일 224개**를 각각 설명합니다.

파일명은 클릭하면 해당 소스로 이동합니다. `Ctrl+F`로 파일명이나 역할을 찾을 수 있습니다.
제품 코드와 MCU 코드, ST/ARM 제공 코드를 구분했습니다. 제공 파일의 존재가 실제 기능 사용을 뜻하지는 않습니다.
Git 대상 소스를 기준으로 하며 삭제된 옛 경로·빌드 산출물·개인 indexer 설정은 제외합니다.

| 분류 | 파일 수 |
|---|---:|
| ARM/ST 제공 CMSIS | 27 |
| MCU 설정/문서 | 11 |
| MCU 시작 코드 | 1 |
| MCU 프로젝트 코드 | 19 |
| PC/Jetson 제품 코드 | 98 |
| ST 제공 HAL/LL | 63 |
| 빌드 설정 | 1 |
| 안내 문서 | 1 |
| 제공 코드의 라이선스 | 3 |

## 폴더 바로가기

- [src](#folder-1)
- [src/application](#folder-2)
- [src/apps/jetson](#folder-3)
- [src/apps/windows](#folder-4)
- [src/core/acquisition](#folder-5)
- [src/core/config](#folder-6)
- [src/core/devices](#folder-7)
- [src/core/diagnostics](#folder-8)
- [src/core/runtime](#folder-9)
- [src/core/types](#folder-10)
- [src/drivers](#folder-11)
- [src/drivers/alazar](#folder-12)
- [src/drivers/edfa](#folder-13)
- [src/drivers/mcu](#folder-14)
- [src/drivers/replay](#folder-15)
- [src/drivers/serial](#folder-16)
- [src/drivers/simulator](#folder-17)
- [src/firmware/mcu/FMCW_LiDAR_MCU](#folder-18)
- [src/firmware/mcu/FMCW_LiDAR_MCU/.settings](#folder-19)
- [src/firmware/mcu/FMCW_LiDAR_MCU/Core/Inc](#folder-20)
- [src/firmware/mcu/FMCW_LiDAR_MCU/Core/Src](#folder-21)
- [src/firmware/mcu/FMCW_LiDAR_MCU/Core/Startup](#folder-22)
- [src/firmware/mcu/FMCW_LiDAR_MCU/Drivers/CMSIS](#folder-23)
- [src/firmware/mcu/FMCW_LiDAR_MCU/Drivers/CMSIS/Device/ST/STM32H7xx](#folder-24)
- [src/firmware/mcu/FMCW_LiDAR_MCU/Drivers/CMSIS/Device/ST/STM32H7xx/Include](#folder-25)
- [src/firmware/mcu/FMCW_LiDAR_MCU/Drivers/CMSIS/Include](#folder-26)
- [src/firmware/mcu/FMCW_LiDAR_MCU/Drivers/STM32H7xx_HAL_Driver](#folder-27)
- [src/firmware/mcu/FMCW_LiDAR_MCU/Drivers/STM32H7xx_HAL_Driver/Inc](#folder-28)
- [src/firmware/mcu/FMCW_LiDAR_MCU/Drivers/STM32H7xx_HAL_Driver/Inc/Legacy](#folder-29)
- [src/firmware/mcu/FMCW_LiDAR_MCU/Drivers/STM32H7xx_HAL_Driver/Src](#folder-30)
- [src/network](#folder-31)
- [src/processing](#folder-32)
- [src/processing/cpu](#folder-33)
- [src/processing/cuda](#folder-34)
- [src/storage](#folder-35)
- [src/ui](#folder-36)
- [src/ui/plots](#folder-37)
- [src/ui/point_cloud](#folder-38)

<a id="folder-1"></a>
## `src/`

| 파일 | 구분 | 하는 일 |
|---|---|---|
| [CMakeLists.txt](../../src/CMakeLists.txt) | 빌드 설정 | 제품 소스 목록, core·Qt·실행 target과 SDK·FFTW·CUDA 연결 |
| [README.md](../../src/README.md) | 안내 문서 | 역할별 소스 위치를 빠르게 찾는 지도와 상세 안내서 링크 |

<a id="folder-2"></a>
## `src/application/`

| 파일 | 구분 | 하는 일 |
|---|---|---|
| [application_controller.cpp](../../src/application/application_controller.cpp) | PC/Jetson 제품 코드 | 화면의 실행 요청을 worker에 전달하고 장치·계측·처리·저장 연결, 시작·정지 및 상태·결과 전달을 조정 · C++ 구현 |
| [application_controller.h](../../src/application/application_controller.h) | PC/Jetson 제품 코드 | 화면의 실행 요청을 worker에 전달하고 장치·계측·처리·저장 연결, 시작·정지 및 상태·결과 전달을 조정 · 헤더: 선언·자료형·상수/inline 코드 |
| [replay_setup_loader.cpp](../../src/application/replay_setup_loader.cpp) | PC/Jetson 제품 코드 | RAW에 기록된 설정을 읽어 재생 준비에 사용하고 재생 경로 변경 여부를 확인 · C++ 구현 |
| [replay_setup_loader.h](../../src/application/replay_setup_loader.h) | PC/Jetson 제품 코드 | RAW에 기록된 설정을 읽어 재생 준비에 사용하고 재생 경로 변경 여부를 확인 · 헤더: 선언·자료형·상수/inline 코드 |

<a id="folder-3"></a>
## `src/apps/jetson/`

| 파일 | 구분 | 하는 일 |
|---|---|---|
| [main.cpp](../../src/apps/jetson/main.cpp) | PC/Jetson 제품 코드 | Jetson Qt 앱 시작, MainWindow 생성, 명령행 옵션 처리 · C++ 구현 |

<a id="folder-4"></a>
## `src/apps/windows/`

| 파일 | 구분 | 하는 일 |
|---|---|---|
| [main.cpp](../../src/apps/windows/main.cpp) | PC/Jetson 제품 코드 | Windows Qt 앱 시작, MainWindow 생성, 명령행 옵션 처리 · C++ 구현 |

<a id="folder-5"></a>
## `src/core/acquisition/`

| 파일 | 구분 | 하는 일 |
|---|---|---|
| [acquisition_session.cpp](../../src/core/acquisition/acquisition_session.cpp) | PC/Jetson 제품 코드 | digitizer·MCU·EDFA의 계측 설정·연결·준비·트리거·데이터 수신·정지 관리 · C++ 구현 |
| [acquisition_session.h](../../src/core/acquisition/acquisition_session.h) | PC/Jetson 제품 코드 | digitizer·MCU·EDFA의 계측 설정·연결·준비·트리거·데이터 수신·정지 관리 · 헤더: 선언·자료형·상수/inline 코드 |
| [continuous_acquisition_worker.cpp](../../src/core/acquisition/continuous_acquisition_worker.cpp) | PC/Jetson 제품 코드 | 별도 worker에서 연속 수집하고 RAW 배치를 전달하며 중단·종료 상태 관리 · C++ 구현 |
| [continuous_acquisition_worker.h](../../src/core/acquisition/continuous_acquisition_worker.h) | PC/Jetson 제품 코드 | 별도 worker에서 연속 수집하고 RAW 배치를 전달하며 중단·종료 상태 관리 · 헤더: 선언·자료형·상수/inline 코드 |
| [raw_frame_batch_pool.cpp](../../src/core/acquisition/raw_frame_batch_pool.cpp) | PC/Jetson 제품 코드 | RAW 배치를 담을 메모리 버퍼의 재사용 관리 · C++ 구현 |
| [raw_frame_batch_pool.h](../../src/core/acquisition/raw_frame_batch_pool.h) | PC/Jetson 제품 코드 | RAW 배치를 담을 메모리 버퍼의 재사용 관리 · 헤더: 선언·자료형·상수/inline 코드 |

<a id="folder-6"></a>
## `src/core/config/`

| 파일 | 구분 | 하는 일 |
|---|---|---|
| [config_document.cpp](../../src/core/config/config_document.cpp) | PC/Jetson 제품 코드 | 프로젝트 설정 YAML 파싱, 항목 조회·병합 및 YAML/JSON 직렬화 · C++ 구현 |
| [config_document.h](../../src/core/config/config_document.h) | PC/Jetson 제품 코드 | 프로젝트 설정 YAML 파싱, 항목 조회·병합 및 YAML/JSON 직렬화 · 헤더: 선언·자료형·상수/inline 코드 |
| [config_policy.cpp](../../src/core/config/config_policy.cpp) | PC/Jetson 제품 코드 | 초기 설계의 항목별 변경 정책표. 현재 GUI 동작 허용 여부는 RuntimeWorker도 확인 · C++ 구현 |
| [config_policy.h](../../src/core/config/config_policy.h) | PC/Jetson 제품 코드 | 초기 설계의 항목별 변경 정책표. 현재 GUI 동작 허용 여부는 RuntimeWorker도 확인 · 헤더: 선언·자료형·상수/inline 코드 |
| [config_profile.cpp](../../src/core/config/config_profile.cpp) | PC/Jetson 제품 코드 | 설정 문서와 SystemConfig 변환, 계층별 설정 읽기, 프로필·스냅샷 저장 · C++ 구현 |
| [config_profile.h](../../src/core/config/config_profile.h) | PC/Jetson 제품 코드 | 설정 문서와 SystemConfig 변환, 계층별 설정 읽기, 프로필·스냅샷 저장 · 헤더: 선언·자료형·상수/inline 코드 |
| [config_types.cpp](../../src/core/config/config_types.cpp) | PC/Jetson 제품 코드 | 계측·처리·저장 등의 설정 자료형과 enum/문자열 변환 · C++ 구현 |
| [config_types.h](../../src/core/config/config_types.h) | PC/Jetson 제품 코드 | 계측·처리·저장 등의 설정 자료형과 enum/문자열 변환 · 헤더: 선언·자료형·상수/inline 코드 |
| [config_validation.cpp](../../src/core/config/config_validation.cpp) | PC/Jetson 제품 코드 | 설정의 값 범위·지원 사양·항목 조합 검증과 오류·경고 반환 · C++ 구현 |
| [config_validation.h](../../src/core/config/config_validation.h) | PC/Jetson 제품 코드 | 설정의 값 범위·지원 사양·항목 조합 검증과 오류·경고 반환 · 헤더: 선언·자료형·상수/inline 코드 |

<a id="folder-7"></a>
## `src/core/devices/`

| 파일 | 구분 | 하는 일 |
|---|---|---|
| [device_interfaces.h](../../src/core/devices/device_interfaces.h) | PC/Jetson 제품 코드 | 계측기·MCU·EDFA·UDP의 공통 호출 인터페이스와 장치 상태·파형 자료형 · 헤더: 선언·자료형·상수/inline 코드 |
| [digitizer_capabilities.cpp](../../src/core/devices/digitizer_capabilities.cpp) | PC/Jetson 제품 코드 | Alazar 보드별 지원 사양과 샘플링 속도·레코드 길이·입력 범위 등 검사 · C++ 구현 |
| [digitizer_capabilities.h](../../src/core/devices/digitizer_capabilities.h) | PC/Jetson 제품 코드 | Alazar 보드별 지원 사양과 샘플링 속도·레코드 길이·입력 범위 등 검사 · 헤더: 선언·자료형·상수/inline 코드 |

<a id="folder-8"></a>
## `src/core/diagnostics/`

| 파일 | 구분 | 하는 일 |
|---|---|---|
| [app_version.cpp](../../src/core/diagnostics/app_version.cpp) | PC/Jetson 제품 코드 | 프로그램 버전 정보와 표시용 버전 문자열 · C++ 구현 |
| [app_version.h](../../src/core/diagnostics/app_version.h) | PC/Jetson 제품 코드 | 프로그램 버전 정보와 표시용 버전 문자열 · 헤더: 선언·자료형·상수/inline 코드 |
| [logging.cpp](../../src/core/diagnostics/logging.cpp) | PC/Jetson 제품 코드 | 로그 수준·항목·메모리 저장과 시간·문자열 표시 · C++ 구현 |
| [logging.h](../../src/core/diagnostics/logging.h) | PC/Jetson 제품 코드 | 로그 수준·항목·메모리 저장과 시간·문자열 표시 · 헤더: 선언·자료형·상수/inline 코드 |

<a id="folder-9"></a>
## `src/core/runtime/`

| 파일 | 구분 | 하는 일 |
|---|---|---|
| [cancellation.h](../../src/core/runtime/cancellation.h) | PC/Jetson 제품 코드 | 작업 취소 여부를 전달·확인하는 callback 형식과 inline 함수 · 헤더: 선언·자료형·상수/inline 코드 |
| [realtime_thread.cpp](../../src/core/runtime/realtime_thread.cpp) | PC/Jetson 제품 코드 | 처리 스레드와 프로세스의 우선순위 설정을 위한 플랫폼별 지원 · C++ 구현 |
| [realtime_thread.h](../../src/core/runtime/realtime_thread.h) | PC/Jetson 제품 코드 | 처리 스레드와 프로세스의 우선순위 설정을 위한 플랫폼별 지원 · 헤더: 선언·자료형·상수/inline 코드 |
| [stop_result.h](../../src/core/runtime/stop_result.h) | PC/Jetson 제품 코드 | 정지 단계별 오류를 수집하고 성공 여부·오류 요약 반환 · 헤더: 선언·자료형·상수/inline 코드 |
| [system_state.cpp](../../src/core/runtime/system_state.cpp) | PC/Jetson 제품 코드 | 동작 상태, 상태 전환 허용 여부와 상태 문자열 표현 · C++ 구현 |
| [system_state.h](../../src/core/runtime/system_state.h) | PC/Jetson 제품 코드 | 동작 상태, 상태 전환 허용 여부와 상태 문자열 표현 · 헤더: 선언·자료형·상수/inline 코드 |
| [system_telemetry.h](../../src/core/runtime/system_telemetry.h) | PC/Jetson 제품 코드 | 계측 설정·연결·실행 상태와 digitizer·MCU·EDFA 상태를 묶는 자료형 · 헤더: 선언·자료형·상수/inline 코드 |

<a id="folder-10"></a>
## `src/core/types/`

| 파일 | 구분 | 하는 일 |
|---|---|---|
| [frame_types.h](../../src/core/types/frame_types.h) | PC/Jetson 제품 코드 | 샘플 버퍼, RAW 프레임·배치, 스캔 위치, 피크와 처리 결과·XYZ 점의 공통 자료형 · 헤더: 선언·자료형·상수/inline 코드 |
| [scan_trajectory.cpp](../../src/core/types/scan_trajectory.cpp) | PC/Jetson 제품 코드 | 스캔 raster 위치 계산, 생성 데이터의 위치 기록과 RAW 재생 위치 정렬 · C++ 구현 |
| [scan_trajectory.h](../../src/core/types/scan_trajectory.h) | PC/Jetson 제품 코드 | 스캔 raster 위치 계산, 생성 데이터의 위치 기록과 RAW 재생 위치 정렬 · 헤더: 선언·자료형·상수/inline 코드 |

<a id="folder-11"></a>
## `src/drivers/`

| 파일 | 구분 | 하는 일 |
|---|---|---|
| [runtime_adapter_factory.cpp](../../src/drivers/runtime_adapter_factory.cpp) | PC/Jetson 제품 코드 | 입력 종류에 맞는 digitizer 생성. MCU·EDFA는 별도 프로필 모드를 따르는 serial controller로 연결 · C++ 구현 |
| [runtime_adapter_factory.h](../../src/drivers/runtime_adapter_factory.h) | PC/Jetson 제품 코드 | 입력 종류에 맞는 digitizer 생성. MCU·EDFA는 별도 프로필 모드를 따르는 serial controller로 연결 · 헤더: 선언·자료형·상수/inline 코드 |

<a id="folder-12"></a>
## `src/drivers/alazar/`

| 파일 | 구분 | 하는 일 |
|---|---|---|
| [alazar_digitizer.cpp](../../src/drivers/alazar/alazar_digitizer.cpp) | PC/Jetson 제품 코드 | ATS-SDK로 보드 검색·설정·DMA 수집·정지를 수행하는 계측 adapter · C++ 구현 |
| [alazar_digitizer.h](../../src/drivers/alazar/alazar_digitizer.h) | PC/Jetson 제품 코드 | ATS-SDK로 보드 검색·설정·DMA 수집·정지를 수행하는 계측 adapter · 헤더: 선언·자료형·상수/inline 코드 |
| [alazar_sample_conversion.cpp](../../src/drivers/alazar/alazar_sample_conversion.cpp) | PC/Jetson 제품 코드 | Alazar left-aligned ADC 샘플을 signed 16-bit 표현으로 변환 · C++ 구현 |
| [alazar_sample_conversion.h](../../src/drivers/alazar/alazar_sample_conversion.h) | PC/Jetson 제품 코드 | Alazar left-aligned ADC 샘플을 signed 16-bit 표현으로 변환 · 헤더: 선언·자료형·상수/inline 코드 |

<a id="folder-13"></a>
## `src/drivers/edfa/`

| 파일 | 구분 | 하는 일 |
|---|---|---|
| [edfa_protocol.cpp](../../src/drivers/edfa/edfa_protocol.cpp) | PC/Jetson 제품 코드 | EDFA 명령 packet 생성과 상태·출력·모드·활성화 응답 해석 · C++ 구현 |
| [edfa_protocol.h](../../src/drivers/edfa/edfa_protocol.h) | PC/Jetson 제품 코드 | EDFA 명령 packet 생성과 상태·출력·모드·활성화 응답 해석 · 헤더: 선언·자료형·상수/inline 코드 |
| [edfa_serial_controller.cpp](../../src/drivers/edfa/edfa_serial_controller.cpp) | PC/Jetson 제품 코드 | EDFA 시리얼 연결과 명령 왕복, 출력 설정·상태 관리 · C++ 구현 |
| [edfa_serial_controller.h](../../src/drivers/edfa/edfa_serial_controller.h) | PC/Jetson 제품 코드 | EDFA 시리얼 연결과 명령 왕복, 출력 설정·상태 관리 · 헤더: 선언·자료형·상수/inline 코드 |

<a id="folder-14"></a>
## `src/drivers/mcu/`

| 파일 | 구분 | 하는 일 |
|---|---|---|
| [mcu_protocol.cpp](../../src/drivers/mcu/mcu_protocol.cpp) | PC/Jetson 제품 코드 | 파형 구성·XYM 파일 읽기, MCU 명령 생성과 ACK/오류 응답 해석 · C++ 구현 |
| [mcu_protocol.h](../../src/drivers/mcu/mcu_protocol.h) | PC/Jetson 제품 코드 | 파형 구성·XYM 파일 읽기, MCU 명령 생성과 ACK/오류 응답 해석 · 헤더: 선언·자료형·상수/inline 코드 |
| [mcu_serial_controller.cpp](../../src/drivers/mcu/mcu_serial_controller.cpp) | PC/Jetson 제품 코드 | MCU 시리얼 연결, 파형 업로드·시작·정지 명령과 응답·진행 상태 처리 · C++ 구현 |
| [mcu_serial_controller.h](../../src/drivers/mcu/mcu_serial_controller.h) | PC/Jetson 제품 코드 | MCU 시리얼 연결, 파형 업로드·시작·정지 명령과 응답·진행 상태 처리 · 헤더: 선언·자료형·상수/inline 코드 |

<a id="folder-15"></a>
## `src/drivers/replay/`

| 파일 | 구분 | 하는 일 |
|---|---|---|
| [replay_digitizer.cpp](../../src/drivers/replay/replay_digitizer.cpp) | PC/Jetson 제품 코드 | RAW 파일을 읽어 IDigitizer 공통 인터페이스로 데이터를 공급 · C++ 구현 |
| [replay_digitizer.h](../../src/drivers/replay/replay_digitizer.h) | PC/Jetson 제품 코드 | RAW 파일을 읽어 IDigitizer 공통 인터페이스로 데이터를 공급 · 헤더: 선언·자료형·상수/inline 코드 |

<a id="folder-16"></a>
## `src/drivers/serial/`

| 파일 | 구분 | 하는 일 |
|---|---|---|
| [serial_transport.cpp](../../src/drivers/serial/serial_transport.cpp) | PC/Jetson 제품 코드 | Windows COM/Linux tty 열기·읽기·쓰기·종료와 포트 조회 · C++ 구현 |
| [serial_transport.h](../../src/drivers/serial/serial_transport.h) | PC/Jetson 제품 코드 | Windows COM/Linux tty 열기·읽기·쓰기·종료와 포트 조회 · 헤더: 선언·자료형·상수/inline 코드 |

<a id="folder-17"></a>
## `src/drivers/simulator/`

| 파일 | 구분 | 하는 일 |
|---|---|---|
| [fake_digitizer.cpp](../../src/drivers/simulator/fake_digitizer.cpp) | PC/Jetson 제품 코드 | 계측기 없이 사용하는 모의 측정 데이터 생성 · C++ 구현 |
| [fake_digitizer.h](../../src/drivers/simulator/fake_digitizer.h) | PC/Jetson 제품 코드 | 계측기 없이 사용하는 모의 측정 데이터 생성 · 헤더: 선언·자료형·상수/inline 코드 |
| [fake_edfa.cpp](../../src/drivers/simulator/fake_edfa.cpp) | PC/Jetson 제품 코드 | EDFA 상태·출력 제어를 모의 구현. 실제 runtime 선택은 adapter factory와 controller 설정 참조 · C++ 구현 |
| [fake_edfa.h](../../src/drivers/simulator/fake_edfa.h) | PC/Jetson 제품 코드 | EDFA 상태·출력 제어를 모의 구현. 실제 runtime 선택은 adapter factory와 controller 설정 참조 · 헤더: 선언·자료형·상수/inline 코드 |
| [fake_mcu.cpp](../../src/drivers/simulator/fake_mcu.cpp) | PC/Jetson 제품 코드 | MCU 상태·파형 제어를 모의 구현. 실제 runtime 선택은 adapter factory와 controller 설정 참조 · C++ 구현 |
| [fake_mcu.h](../../src/drivers/simulator/fake_mcu.h) | PC/Jetson 제품 코드 | MCU 상태·파형 제어를 모의 구현. 실제 runtime 선택은 adapter factory와 controller 설정 참조 · 헤더: 선언·자료형·상수/inline 코드 |

<a id="folder-18"></a>
## `src/firmware/mcu/FMCW_LiDAR_MCU/`

| 파일 | 구분 | 하는 일 |
|---|---|---|
| [.cproject](../../src/firmware/mcu/FMCW_LiDAR_MCU/.cproject) | MCU 설정/문서 | CubeIDE C/C++ compiler·linker·빌드 구성 |
| [.mxproject](../../src/firmware/mcu/FMCW_LiDAR_MCU/.mxproject) | MCU 설정/문서 | CubeMX 프로젝트와 생성 코드 관련 메타데이터 |
| [.project](../../src/firmware/mcu/FMCW_LiDAR_MCU/.project) | MCU 설정/문서 | Eclipse/CubeIDE 프로젝트 이름·성격·builder 등록 |
| [FMCW_LiDAR_MCU Debug.launch](../../src/firmware/mcu/FMCW_LiDAR_MCU/FMCW_LiDAR_MCU%20Debug.launch) | MCU 설정/문서 | CubeIDE debugger·다운로드·실행 설정 |
| [FMCW_LiDAR_MCU.ioc](../../src/firmware/mcu/FMCW_LiDAR_MCU/FMCW_LiDAR_MCU.ioc) | MCU 설정/문서 | CubeMX pin·clock·주변장치 설정과 코드 생성 입력 |
| [README.md](../../src/firmware/mcu/FMCW_LiDAR_MCU/README.md) | MCU 설정/문서 | MCU 프로젝트 여는 법, 타이머·파형 protocol·동작 설명 |
| [STM32H750VBTX_FLASH.ld](../../src/firmware/mcu/FMCW_LiDAR_MCU/STM32H750VBTX_FLASH.ld) | MCU 설정/문서 | Flash 실행을 위한 코드·데이터·메모리 배치 |
| [STM32H750VBTX_RAM.ld](../../src/firmware/mcu/FMCW_LiDAR_MCU/STM32H750VBTX_RAM.ld) | MCU 설정/문서 | RAM 실행을 위한 코드·데이터·메모리 배치 |

<a id="folder-19"></a>
## `src/firmware/mcu/FMCW_LiDAR_MCU/.settings/`

| 파일 | 구분 | 하는 일 |
|---|---|---|
| [com.st.stm32cube.ide.mcu.sfrview.prefs](../../src/firmware/mcu/FMCW_LiDAR_MCU/.settings/com.st.stm32cube.ide.mcu.sfrview.prefs) | MCU 설정/문서 | MCU register 보기 기능의 IDE 설정 |
| [org.eclipse.core.resources.prefs](../../src/firmware/mcu/FMCW_LiDAR_MCU/.settings/org.eclipse.core.resources.prefs) | MCU 설정/문서 | IDE 리소스·문자 인코딩 설정 |
| [stm32cubeide.project.prefs](../../src/firmware/mcu/FMCW_LiDAR_MCU/.settings/stm32cubeide.project.prefs) | MCU 설정/문서 | STM32CubeIDE 프로젝트 설정 |

<a id="folder-20"></a>
## `src/firmware/mcu/FMCW_LiDAR_MCU/Core/Inc/`

| 파일 | 구분 | 하는 일 |
|---|---|---|
| [ad5664.h](../../src/firmware/mcu/FMCW_LiDAR_MCU/Core/Inc/ad5664.h) | MCU 프로젝트 코드 | AD5664 DAC 명령 word와 A/B/C/D 채널 전송 word 구성 · 헤더: 선언·자료형·상수/inline 코드 |
| [frame_player.h](../../src/firmware/mcu/FMCW_LiDAR_MCU/Core/Inc/frame_player.h) | MCU 프로젝트 코드 | 업로드 파형 buffer·재생 상태와 timer tick에 따른 DAC word·marker 출력 · 헤더: 선언·자료형·상수/inline 코드 |
| [main.h](../../src/firmware/mcu/FMCW_LiDAR_MCU/Core/Inc/main.h) | MCU 프로젝트 코드 | MCU 핀 정의·주변장치 초기화·부팅과 메인 루프 · 헤더: 선언·자료형·상수/inline 코드 |
| [mirrorcle_drv.h](../../src/firmware/mcu/FMCW_LiDAR_MCU/Core/Inc/mirrorcle_drv.h) | MCU 프로젝트 코드 | 미러 구동 DAC 초기화·bias·출력 enable/disable 제어 · 헤더: 선언·자료형·상수/inline 코드 |
| [spi24_tx.h](../../src/firmware/mcu/FMCW_LiDAR_MCU/Core/Inc/spi24_tx.h) | MCU 프로젝트 코드 | 24-bit SPI 전송, CS 제어, timeout 및 오류 상태 관리 · 헤더: 선언·자료형·상수/inline 코드 |
| [stm32h7xx_hal_conf.h](../../src/firmware/mcu/FMCW_LiDAR_MCU/Core/Inc/stm32h7xx_hal_conf.h) | MCU 프로젝트 코드 | 프로젝트에서 사용할 HAL 모듈과 설정 상수 · 헤더: 선언·자료형·상수/inline 코드 |
| [stm32h7xx_it.h](../../src/firmware/mcu/FMCW_LiDAR_MCU/Core/Inc/stm32h7xx_it.h) | MCU 프로젝트 코드 | MCU interrupt handler와 주변장치 interrupt 처리 연결 · 헤더: 선언·자료형·상수/inline 코드 |
| [uart_cmd.h](../../src/firmware/mcu/FMCW_LiDAR_MCU/Core/Inc/uart_cmd.h) | MCU 프로젝트 코드 | UART 수신 명령을 처리하고 파형 업로드·시작·정지와 ACK/오류 응답 연결 · 헤더: 선언·자료형·상수/inline 코드 |

<a id="folder-21"></a>
## `src/firmware/mcu/FMCW_LiDAR_MCU/Core/Src/`

| 파일 | 구분 | 하는 일 |
|---|---|---|
| [ad5664.c](../../src/firmware/mcu/FMCW_LiDAR_MCU/Core/Src/ad5664.c) | MCU 프로젝트 코드 | AD5664 DAC 명령 word와 A/B/C/D 채널 전송 word 구성 · C 구현 |
| [frame_player.c](../../src/firmware/mcu/FMCW_LiDAR_MCU/Core/Src/frame_player.c) | MCU 프로젝트 코드 | 업로드 파형 buffer·재생 상태와 timer tick에 따른 DAC word·marker 출력 · C 구현 |
| [main.c](../../src/firmware/mcu/FMCW_LiDAR_MCU/Core/Src/main.c) | MCU 프로젝트 코드 | MCU 핀 정의·주변장치 초기화·부팅과 메인 루프 · C 구현 |
| [mirrorcle_drv.c](../../src/firmware/mcu/FMCW_LiDAR_MCU/Core/Src/mirrorcle_drv.c) | MCU 프로젝트 코드 | 미러 구동 DAC 초기화·bias·출력 enable/disable 제어 · C 구현 |
| [spi24_tx.c](../../src/firmware/mcu/FMCW_LiDAR_MCU/Core/Src/spi24_tx.c) | MCU 프로젝트 코드 | 24-bit SPI 전송, CS 제어, timeout 및 오류 상태 관리 · C 구현 |
| [stm32h7xx_hal_msp.c](../../src/firmware/mcu/FMCW_LiDAR_MCU/Core/Src/stm32h7xx_hal_msp.c) | MCU 프로젝트 코드 | HAL 초기화에 필요한 pin·clock·interrupt 등의 보드 설정 · C 구현 |
| [stm32h7xx_it.c](../../src/firmware/mcu/FMCW_LiDAR_MCU/Core/Src/stm32h7xx_it.c) | MCU 프로젝트 코드 | MCU interrupt handler와 주변장치 interrupt 처리 연결 · C 구현 |
| [syscalls.c](../../src/firmware/mcu/FMCW_LiDAR_MCU/Core/Src/syscalls.c) | MCU 프로젝트 코드 | C runtime의 시스템 함수 연결을 위한 기본 구현 · C 구현 |
| [sysmem.c](../../src/firmware/mcu/FMCW_LiDAR_MCU/Core/Src/sysmem.c) | MCU 프로젝트 코드 | C runtime에서 사용할 heap 메모리 할당 지원 · C 구현 |
| [system_stm32h7xx.c](../../src/firmware/mcu/FMCW_LiDAR_MCU/Core/Src/system_stm32h7xx.c) | MCU 프로젝트 코드 | MCU system 초기화와 clock 관련 기반 코드 · C 구현 |
| [uart_cmd.c](../../src/firmware/mcu/FMCW_LiDAR_MCU/Core/Src/uart_cmd.c) | MCU 프로젝트 코드 | UART 수신 명령을 처리하고 파형 업로드·시작·정지와 ACK/오류 응답 연결 · C 구현 |

<a id="folder-22"></a>
## `src/firmware/mcu/FMCW_LiDAR_MCU/Core/Startup/`

| 파일 | 구분 | 하는 일 |
|---|---|---|
| [startup_stm32h750vbtx.s](../../src/firmware/mcu/FMCW_LiDAR_MCU/Core/Startup/startup_stm32h750vbtx.s) | MCU 시작 코드 | 리셋 후 실행 시작과 STM32H750 interrupt vector 정의 |

<a id="folder-23"></a>
## `src/firmware/mcu/FMCW_LiDAR_MCU/Drivers/CMSIS/`

| 파일 | 구분 | 하는 일 |
|---|---|---|
| [LICENSE.txt](../../src/firmware/mcu/FMCW_LiDAR_MCU/Drivers/CMSIS/LICENSE.txt) | 제공 코드의 라이선스 | 해당 ARM/ST 제공 코드의 이용 조건 |

<a id="folder-24"></a>
## `src/firmware/mcu/FMCW_LiDAR_MCU/Drivers/CMSIS/Device/ST/STM32H7xx/`

| 파일 | 구분 | 하는 일 |
|---|---|---|
| [LICENSE.txt](../../src/firmware/mcu/FMCW_LiDAR_MCU/Drivers/CMSIS/Device/ST/STM32H7xx/LICENSE.txt) | 제공 코드의 라이선스 | 해당 ARM/ST 제공 코드의 이용 조건 |

<a id="folder-25"></a>
## `src/firmware/mcu/FMCW_LiDAR_MCU/Drivers/CMSIS/Device/ST/STM32H7xx/Include/`

| 파일 | 구분 | 하는 일 |
|---|---|---|
| [stm32h750xx.h](../../src/firmware/mcu/FMCW_LiDAR_MCU/Drivers/CMSIS/Device/ST/STM32H7xx/Include/stm32h750xx.h) | ARM/ST 제공 CMSIS | STM32H750의 register·주변장치·interrupt 정의 · 헤더 |
| [stm32h7xx.h](../../src/firmware/mcu/FMCW_LiDAR_MCU/Drivers/CMSIS/Device/ST/STM32H7xx/Include/stm32h7xx.h) | ARM/ST 제공 CMSIS | 선택한 STM32H7 장치 정의와 HAL 연결을 위한 공통 include · 헤더 |
| [system_stm32h7xx.h](../../src/firmware/mcu/FMCW_LiDAR_MCU/Drivers/CMSIS/Device/ST/STM32H7xx/Include/system_stm32h7xx.h) | ARM/ST 제공 CMSIS | SystemInit·SystemCoreClock 등의 선언 · 헤더 |

<a id="folder-26"></a>
## `src/firmware/mcu/FMCW_LiDAR_MCU/Drivers/CMSIS/Include/`

| 파일 | 구분 | 하는 일 |
|---|---|---|
| [cmsis_armcc.h](../../src/firmware/mcu/FMCW_LiDAR_MCU/Drivers/CMSIS/Include/cmsis_armcc.h) | ARM/ST 제공 CMSIS | Arm Compiler용 CMSIS 명령·compiler 지원 · 헤더 |
| [cmsis_armclang.h](../../src/firmware/mcu/FMCW_LiDAR_MCU/Drivers/CMSIS/Include/cmsis_armclang.h) | ARM/ST 제공 CMSIS | Arm Clang용 CMSIS 명령·compiler 지원 · 헤더 |
| [cmsis_armclang_ltm.h](../../src/firmware/mcu/FMCW_LiDAR_MCU/Drivers/CMSIS/Include/cmsis_armclang_ltm.h) | ARM/ST 제공 CMSIS | Arm Clang LTM용 CMSIS 명령·compiler 지원 · 헤더 |
| [cmsis_compiler.h](../../src/firmware/mcu/FMCW_LiDAR_MCU/Drivers/CMSIS/Include/cmsis_compiler.h) | ARM/ST 제공 CMSIS | compiler별 CMSIS 지원 헤더 선택 · 헤더 |
| [cmsis_gcc.h](../../src/firmware/mcu/FMCW_LiDAR_MCU/Drivers/CMSIS/Include/cmsis_gcc.h) | ARM/ST 제공 CMSIS | GCC용 CMSIS 명령·compiler 지원 · 헤더 |
| [cmsis_iccarm.h](../../src/firmware/mcu/FMCW_LiDAR_MCU/Drivers/CMSIS/Include/cmsis_iccarm.h) | ARM/ST 제공 CMSIS | IAR용 CMSIS 명령·compiler 지원 · 헤더 |
| [cmsis_version.h](../../src/firmware/mcu/FMCW_LiDAR_MCU/Drivers/CMSIS/Include/cmsis_version.h) | ARM/ST 제공 CMSIS | CMSIS 버전 상수 · 헤더 |
| [core_armv81mml.h](../../src/firmware/mcu/FMCW_LiDAR_MCU/Drivers/CMSIS/Include/core_armv81mml.h) | ARM/ST 제공 CMSIS | armv81mml CPU/아키텍처용 CMSIS register·core 기능 정의 (공급 패키지에 함께 포함) · 헤더 |
| [core_armv8mbl.h](../../src/firmware/mcu/FMCW_LiDAR_MCU/Drivers/CMSIS/Include/core_armv8mbl.h) | ARM/ST 제공 CMSIS | armv8mbl CPU/아키텍처용 CMSIS register·core 기능 정의 (공급 패키지에 함께 포함) · 헤더 |
| [core_armv8mml.h](../../src/firmware/mcu/FMCW_LiDAR_MCU/Drivers/CMSIS/Include/core_armv8mml.h) | ARM/ST 제공 CMSIS | armv8mml CPU/아키텍처용 CMSIS register·core 기능 정의 (공급 패키지에 함께 포함) · 헤더 |
| [core_cm0.h](../../src/firmware/mcu/FMCW_LiDAR_MCU/Drivers/CMSIS/Include/core_cm0.h) | ARM/ST 제공 CMSIS | cm0 CPU/아키텍처용 CMSIS register·core 기능 정의 (공급 패키지에 함께 포함) · 헤더 |
| [core_cm0plus.h](../../src/firmware/mcu/FMCW_LiDAR_MCU/Drivers/CMSIS/Include/core_cm0plus.h) | ARM/ST 제공 CMSIS | cm0plus CPU/아키텍처용 CMSIS register·core 기능 정의 (공급 패키지에 함께 포함) · 헤더 |
| [core_cm1.h](../../src/firmware/mcu/FMCW_LiDAR_MCU/Drivers/CMSIS/Include/core_cm1.h) | ARM/ST 제공 CMSIS | cm1 CPU/아키텍처용 CMSIS register·core 기능 정의 (공급 패키지에 함께 포함) · 헤더 |
| [core_cm23.h](../../src/firmware/mcu/FMCW_LiDAR_MCU/Drivers/CMSIS/Include/core_cm23.h) | ARM/ST 제공 CMSIS | cm23 CPU/아키텍처용 CMSIS register·core 기능 정의 (공급 패키지에 함께 포함) · 헤더 |
| [core_cm3.h](../../src/firmware/mcu/FMCW_LiDAR_MCU/Drivers/CMSIS/Include/core_cm3.h) | ARM/ST 제공 CMSIS | cm3 CPU/아키텍처용 CMSIS register·core 기능 정의 (공급 패키지에 함께 포함) · 헤더 |
| [core_cm33.h](../../src/firmware/mcu/FMCW_LiDAR_MCU/Drivers/CMSIS/Include/core_cm33.h) | ARM/ST 제공 CMSIS | cm33 CPU/아키텍처용 CMSIS register·core 기능 정의 (공급 패키지에 함께 포함) · 헤더 |
| [core_cm35p.h](../../src/firmware/mcu/FMCW_LiDAR_MCU/Drivers/CMSIS/Include/core_cm35p.h) | ARM/ST 제공 CMSIS | cm35p CPU/아키텍처용 CMSIS register·core 기능 정의 (공급 패키지에 함께 포함) · 헤더 |
| [core_cm4.h](../../src/firmware/mcu/FMCW_LiDAR_MCU/Drivers/CMSIS/Include/core_cm4.h) | ARM/ST 제공 CMSIS | cm4 CPU/아키텍처용 CMSIS register·core 기능 정의 (공급 패키지에 함께 포함) · 헤더 |
| [core_cm7.h](../../src/firmware/mcu/FMCW_LiDAR_MCU/Drivers/CMSIS/Include/core_cm7.h) | ARM/ST 제공 CMSIS | cm7 CPU/아키텍처용 CMSIS register·core 기능 정의 (공급 패키지에 함께 포함) · 헤더 |
| [core_sc000.h](../../src/firmware/mcu/FMCW_LiDAR_MCU/Drivers/CMSIS/Include/core_sc000.h) | ARM/ST 제공 CMSIS | sc000 CPU/아키텍처용 CMSIS register·core 기능 정의 (공급 패키지에 함께 포함) · 헤더 |
| [core_sc300.h](../../src/firmware/mcu/FMCW_LiDAR_MCU/Drivers/CMSIS/Include/core_sc300.h) | ARM/ST 제공 CMSIS | sc300 CPU/아키텍처용 CMSIS register·core 기능 정의 (공급 패키지에 함께 포함) · 헤더 |
| [mpu_armv7.h](../../src/firmware/mcu/FMCW_LiDAR_MCU/Drivers/CMSIS/Include/mpu_armv7.h) | ARM/ST 제공 CMSIS | armv7 계열 MPU 메모리 보호 설정 지원 · 헤더 |
| [mpu_armv8.h](../../src/firmware/mcu/FMCW_LiDAR_MCU/Drivers/CMSIS/Include/mpu_armv8.h) | ARM/ST 제공 CMSIS | armv8 계열 MPU 메모리 보호 설정 지원 · 헤더 |
| [tz_context.h](../../src/firmware/mcu/FMCW_LiDAR_MCU/Drivers/CMSIS/Include/tz_context.h) | ARM/ST 제공 CMSIS | TrustZone context 관리 인터페이스 (공급 패키지에 함께 포함) · 헤더 |

<a id="folder-27"></a>
## `src/firmware/mcu/FMCW_LiDAR_MCU/Drivers/STM32H7xx_HAL_Driver/`

| 파일 | 구분 | 하는 일 |
|---|---|---|
| [LICENSE.txt](../../src/firmware/mcu/FMCW_LiDAR_MCU/Drivers/STM32H7xx_HAL_Driver/LICENSE.txt) | 제공 코드의 라이선스 | 해당 ARM/ST 제공 코드의 이용 조건 |

<a id="folder-28"></a>
## `src/firmware/mcu/FMCW_LiDAR_MCU/Drivers/STM32H7xx_HAL_Driver/Inc/`

| 파일 | 구분 | 하는 일 |
|---|---|---|
| [stm32h7xx_hal.h](../../src/firmware/mcu/FMCW_LiDAR_MCU/Drivers/STM32H7xx_HAL_Driver/Inc/stm32h7xx_hal.h) | ST 제공 HAL/LL | HAL 공통 초기화·tick 등 기반 기능 · 헤더: 선언·자료형·상수/inline 코드 |
| [stm32h7xx_hal_cortex.h](../../src/firmware/mcu/FMCW_LiDAR_MCU/Drivers/STM32H7xx_HAL_Driver/Inc/stm32h7xx_hal_cortex.h) | ST 제공 HAL/LL | Cortex CPU·interrupt 제어의 HAL API · 헤더: 선언·자료형·상수/inline 코드 |
| [stm32h7xx_hal_def.h](../../src/firmware/mcu/FMCW_LiDAR_MCU/Drivers/STM32H7xx_HAL_Driver/Inc/stm32h7xx_hal_def.h) | ST 제공 HAL/LL | 공통 HAL 자료형·상수·매크로의 HAL API · 헤더: 선언·자료형·상수/inline 코드 |
| [stm32h7xx_hal_dma.h](../../src/firmware/mcu/FMCW_LiDAR_MCU/Drivers/STM32H7xx_HAL_Driver/Inc/stm32h7xx_hal_dma.h) | ST 제공 HAL/LL | DMA 데이터 전송의 HAL API · 헤더: 선언·자료형·상수/inline 코드 |
| [stm32h7xx_hal_dma_ex.h](../../src/firmware/mcu/FMCW_LiDAR_MCU/Drivers/STM32H7xx_HAL_Driver/Inc/stm32h7xx_hal_dma_ex.h) | ST 제공 HAL/LL | DMA 데이터 전송의 HAL API 확장 기능 · 헤더: 선언·자료형·상수/inline 코드 |
| [stm32h7xx_hal_exti.h](../../src/firmware/mcu/FMCW_LiDAR_MCU/Drivers/STM32H7xx_HAL_Driver/Inc/stm32h7xx_hal_exti.h) | ST 제공 HAL/LL | 외부 interrupt/event의 HAL API · 헤더: 선언·자료형·상수/inline 코드 |
| [stm32h7xx_hal_flash.h](../../src/firmware/mcu/FMCW_LiDAR_MCU/Drivers/STM32H7xx_HAL_Driver/Inc/stm32h7xx_hal_flash.h) | ST 제공 HAL/LL | Flash 메모리의 HAL API · 헤더: 선언·자료형·상수/inline 코드 |
| [stm32h7xx_hal_flash_ex.h](../../src/firmware/mcu/FMCW_LiDAR_MCU/Drivers/STM32H7xx_HAL_Driver/Inc/stm32h7xx_hal_flash_ex.h) | ST 제공 HAL/LL | Flash 메모리의 HAL API 확장 기능 · 헤더: 선언·자료형·상수/inline 코드 |
| [stm32h7xx_hal_gpio.h](../../src/firmware/mcu/FMCW_LiDAR_MCU/Drivers/STM32H7xx_HAL_Driver/Inc/stm32h7xx_hal_gpio.h) | ST 제공 HAL/LL | GPIO pin 입출력의 HAL API · 헤더: 선언·자료형·상수/inline 코드 |
| [stm32h7xx_hal_gpio_ex.h](../../src/firmware/mcu/FMCW_LiDAR_MCU/Drivers/STM32H7xx_HAL_Driver/Inc/stm32h7xx_hal_gpio_ex.h) | ST 제공 HAL/LL | GPIO pin 입출력의 HAL API 확장 기능 · 헤더: 선언·자료형·상수/inline 코드 |
| [stm32h7xx_hal_hsem.h](../../src/firmware/mcu/FMCW_LiDAR_MCU/Drivers/STM32H7xx_HAL_Driver/Inc/stm32h7xx_hal_hsem.h) | ST 제공 HAL/LL | hardware semaphore의 HAL API · 헤더: 선언·자료형·상수/inline 코드 |
| [stm32h7xx_hal_i2c.h](../../src/firmware/mcu/FMCW_LiDAR_MCU/Drivers/STM32H7xx_HAL_Driver/Inc/stm32h7xx_hal_i2c.h) | ST 제공 HAL/LL | I2C 통신의 HAL API · 헤더: 선언·자료형·상수/inline 코드 |
| [stm32h7xx_hal_i2c_ex.h](../../src/firmware/mcu/FMCW_LiDAR_MCU/Drivers/STM32H7xx_HAL_Driver/Inc/stm32h7xx_hal_i2c_ex.h) | ST 제공 HAL/LL | I2C 통신의 HAL API 확장 기능 · 헤더: 선언·자료형·상수/inline 코드 |
| [stm32h7xx_hal_mdma.h](../../src/firmware/mcu/FMCW_LiDAR_MCU/Drivers/STM32H7xx_HAL_Driver/Inc/stm32h7xx_hal_mdma.h) | ST 제공 HAL/LL | MDMA 데이터 전송의 HAL API · 헤더: 선언·자료형·상수/inline 코드 |
| [stm32h7xx_hal_pwr.h](../../src/firmware/mcu/FMCW_LiDAR_MCU/Drivers/STM32H7xx_HAL_Driver/Inc/stm32h7xx_hal_pwr.h) | ST 제공 HAL/LL | 전원 관리의 HAL API · 헤더: 선언·자료형·상수/inline 코드 |
| [stm32h7xx_hal_pwr_ex.h](../../src/firmware/mcu/FMCW_LiDAR_MCU/Drivers/STM32H7xx_HAL_Driver/Inc/stm32h7xx_hal_pwr_ex.h) | ST 제공 HAL/LL | 전원 관리의 HAL API 확장 기능 · 헤더: 선언·자료형·상수/inline 코드 |
| [stm32h7xx_hal_rcc.h](../../src/firmware/mcu/FMCW_LiDAR_MCU/Drivers/STM32H7xx_HAL_Driver/Inc/stm32h7xx_hal_rcc.h) | ST 제공 HAL/LL | clock·reset의 HAL API · 헤더: 선언·자료형·상수/inline 코드 |
| [stm32h7xx_hal_rcc_ex.h](../../src/firmware/mcu/FMCW_LiDAR_MCU/Drivers/STM32H7xx_HAL_Driver/Inc/stm32h7xx_hal_rcc_ex.h) | ST 제공 HAL/LL | clock·reset의 HAL API 확장 기능 · 헤더: 선언·자료형·상수/inline 코드 |
| [stm32h7xx_hal_spi.h](../../src/firmware/mcu/FMCW_LiDAR_MCU/Drivers/STM32H7xx_HAL_Driver/Inc/stm32h7xx_hal_spi.h) | ST 제공 HAL/LL | SPI 통신의 HAL API · 헤더: 선언·자료형·상수/inline 코드 |
| [stm32h7xx_hal_spi_ex.h](../../src/firmware/mcu/FMCW_LiDAR_MCU/Drivers/STM32H7xx_HAL_Driver/Inc/stm32h7xx_hal_spi_ex.h) | ST 제공 HAL/LL | SPI 통신의 HAL API 확장 기능 · 헤더: 선언·자료형·상수/inline 코드 |
| [stm32h7xx_hal_tim.h](../../src/firmware/mcu/FMCW_LiDAR_MCU/Drivers/STM32H7xx_HAL_Driver/Inc/stm32h7xx_hal_tim.h) | ST 제공 HAL/LL | timer·PWM·capture의 HAL API · 헤더: 선언·자료형·상수/inline 코드 |
| [stm32h7xx_hal_tim_ex.h](../../src/firmware/mcu/FMCW_LiDAR_MCU/Drivers/STM32H7xx_HAL_Driver/Inc/stm32h7xx_hal_tim_ex.h) | ST 제공 HAL/LL | timer·PWM·capture의 HAL API 확장 기능 · 헤더: 선언·자료형·상수/inline 코드 |
| [stm32h7xx_hal_uart.h](../../src/firmware/mcu/FMCW_LiDAR_MCU/Drivers/STM32H7xx_HAL_Driver/Inc/stm32h7xx_hal_uart.h) | ST 제공 HAL/LL | UART 비동기 직렬 통신의 HAL API · 헤더: 선언·자료형·상수/inline 코드 |
| [stm32h7xx_hal_uart_ex.h](../../src/firmware/mcu/FMCW_LiDAR_MCU/Drivers/STM32H7xx_HAL_Driver/Inc/stm32h7xx_hal_uart_ex.h) | ST 제공 HAL/LL | UART 비동기 직렬 통신의 HAL API 확장 기능 · 헤더: 선언·자료형·상수/inline 코드 |
| [stm32h7xx_ll_bus.h](../../src/firmware/mcu/FMCW_LiDAR_MCU/Drivers/STM32H7xx_HAL_Driver/Inc/stm32h7xx_ll_bus.h) | ST 제공 HAL/LL | bus와 주변장치 clock의 LL 저수준 API · 헤더: 선언·자료형·상수/inline 코드 |
| [stm32h7xx_ll_cortex.h](../../src/firmware/mcu/FMCW_LiDAR_MCU/Drivers/STM32H7xx_HAL_Driver/Inc/stm32h7xx_ll_cortex.h) | ST 제공 HAL/LL | Cortex CPU·interrupt 제어의 LL 저수준 API · 헤더: 선언·자료형·상수/inline 코드 |
| [stm32h7xx_ll_crs.h](../../src/firmware/mcu/FMCW_LiDAR_MCU/Drivers/STM32H7xx_HAL_Driver/Inc/stm32h7xx_ll_crs.h) | ST 제공 HAL/LL | clock 복구·동기화의 LL 저수준 API · 헤더: 선언·자료형·상수/inline 코드 |
| [stm32h7xx_ll_dma.h](../../src/firmware/mcu/FMCW_LiDAR_MCU/Drivers/STM32H7xx_HAL_Driver/Inc/stm32h7xx_ll_dma.h) | ST 제공 HAL/LL | DMA 데이터 전송의 LL 저수준 API · 헤더: 선언·자료형·상수/inline 코드 |
| [stm32h7xx_ll_dmamux.h](../../src/firmware/mcu/FMCW_LiDAR_MCU/Drivers/STM32H7xx_HAL_Driver/Inc/stm32h7xx_ll_dmamux.h) | ST 제공 HAL/LL | DMA 요청 연결의 LL 저수준 API · 헤더: 선언·자료형·상수/inline 코드 |
| [stm32h7xx_ll_exti.h](../../src/firmware/mcu/FMCW_LiDAR_MCU/Drivers/STM32H7xx_HAL_Driver/Inc/stm32h7xx_ll_exti.h) | ST 제공 HAL/LL | 외부 interrupt/event의 LL 저수준 API · 헤더: 선언·자료형·상수/inline 코드 |
| [stm32h7xx_ll_gpio.h](../../src/firmware/mcu/FMCW_LiDAR_MCU/Drivers/STM32H7xx_HAL_Driver/Inc/stm32h7xx_ll_gpio.h) | ST 제공 HAL/LL | GPIO pin 입출력의 LL 저수준 API · 헤더: 선언·자료형·상수/inline 코드 |
| [stm32h7xx_ll_hsem.h](../../src/firmware/mcu/FMCW_LiDAR_MCU/Drivers/STM32H7xx_HAL_Driver/Inc/stm32h7xx_ll_hsem.h) | ST 제공 HAL/LL | hardware semaphore의 LL 저수준 API · 헤더: 선언·자료형·상수/inline 코드 |
| [stm32h7xx_ll_lpuart.h](../../src/firmware/mcu/FMCW_LiDAR_MCU/Drivers/STM32H7xx_HAL_Driver/Inc/stm32h7xx_ll_lpuart.h) | ST 제공 HAL/LL | 저전력 UART의 LL 저수준 API · 헤더: 선언·자료형·상수/inline 코드 |
| [stm32h7xx_ll_pwr.h](../../src/firmware/mcu/FMCW_LiDAR_MCU/Drivers/STM32H7xx_HAL_Driver/Inc/stm32h7xx_ll_pwr.h) | ST 제공 HAL/LL | 전원 관리의 LL 저수준 API · 헤더: 선언·자료형·상수/inline 코드 |
| [stm32h7xx_ll_rcc.h](../../src/firmware/mcu/FMCW_LiDAR_MCU/Drivers/STM32H7xx_HAL_Driver/Inc/stm32h7xx_ll_rcc.h) | ST 제공 HAL/LL | clock·reset의 LL 저수준 API · 헤더: 선언·자료형·상수/inline 코드 |
| [stm32h7xx_ll_spi.h](../../src/firmware/mcu/FMCW_LiDAR_MCU/Drivers/STM32H7xx_HAL_Driver/Inc/stm32h7xx_ll_spi.h) | ST 제공 HAL/LL | SPI 통신의 LL 저수준 API · 헤더: 선언·자료형·상수/inline 코드 |
| [stm32h7xx_ll_system.h](../../src/firmware/mcu/FMCW_LiDAR_MCU/Drivers/STM32H7xx_HAL_Driver/Inc/stm32h7xx_ll_system.h) | ST 제공 HAL/LL | system 제어의 LL 저수준 API · 헤더: 선언·자료형·상수/inline 코드 |
| [stm32h7xx_ll_tim.h](../../src/firmware/mcu/FMCW_LiDAR_MCU/Drivers/STM32H7xx_HAL_Driver/Inc/stm32h7xx_ll_tim.h) | ST 제공 HAL/LL | timer·PWM·capture의 LL 저수준 API · 헤더: 선언·자료형·상수/inline 코드 |
| [stm32h7xx_ll_usart.h](../../src/firmware/mcu/FMCW_LiDAR_MCU/Drivers/STM32H7xx_HAL_Driver/Inc/stm32h7xx_ll_usart.h) | ST 제공 HAL/LL | USART 직렬 통신의 LL 저수준 API · 헤더: 선언·자료형·상수/inline 코드 |
| [stm32h7xx_ll_utils.h](../../src/firmware/mcu/FMCW_LiDAR_MCU/Drivers/STM32H7xx_HAL_Driver/Inc/stm32h7xx_ll_utils.h) | ST 제공 HAL/LL | 저수준 초기화·시간·clock 지원의 LL 저수준 API · 헤더: 선언·자료형·상수/inline 코드 |

<a id="folder-29"></a>
## `src/firmware/mcu/FMCW_LiDAR_MCU/Drivers/STM32H7xx_HAL_Driver/Inc/Legacy/`

| 파일 | 구분 | 하는 일 |
|---|---|---|
| [stm32_hal_legacy.h](../../src/firmware/mcu/FMCW_LiDAR_MCU/Drivers/STM32H7xx_HAL_Driver/Inc/Legacy/stm32_hal_legacy.h) | ST 제공 HAL/LL | 이전 HAL 이름과의 호환 매크로·정의 |

<a id="folder-30"></a>
## `src/firmware/mcu/FMCW_LiDAR_MCU/Drivers/STM32H7xx_HAL_Driver/Src/`

| 파일 | 구분 | 하는 일 |
|---|---|---|
| [stm32h7xx_hal.c](../../src/firmware/mcu/FMCW_LiDAR_MCU/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal.c) | ST 제공 HAL/LL | HAL 공통 초기화·tick 등 기반 기능 · C 구현 |
| [stm32h7xx_hal_cortex.c](../../src/firmware/mcu/FMCW_LiDAR_MCU/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_cortex.c) | ST 제공 HAL/LL | Cortex CPU·interrupt 제어의 HAL API · C 구현 |
| [stm32h7xx_hal_dma.c](../../src/firmware/mcu/FMCW_LiDAR_MCU/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_dma.c) | ST 제공 HAL/LL | DMA 데이터 전송의 HAL API · C 구현 |
| [stm32h7xx_hal_dma_ex.c](../../src/firmware/mcu/FMCW_LiDAR_MCU/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_dma_ex.c) | ST 제공 HAL/LL | DMA 데이터 전송의 HAL API 확장 기능 · C 구현 |
| [stm32h7xx_hal_exti.c](../../src/firmware/mcu/FMCW_LiDAR_MCU/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_exti.c) | ST 제공 HAL/LL | 외부 interrupt/event의 HAL API · C 구현 |
| [stm32h7xx_hal_flash.c](../../src/firmware/mcu/FMCW_LiDAR_MCU/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_flash.c) | ST 제공 HAL/LL | Flash 메모리의 HAL API · C 구현 |
| [stm32h7xx_hal_flash_ex.c](../../src/firmware/mcu/FMCW_LiDAR_MCU/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_flash_ex.c) | ST 제공 HAL/LL | Flash 메모리의 HAL API 확장 기능 · C 구현 |
| [stm32h7xx_hal_gpio.c](../../src/firmware/mcu/FMCW_LiDAR_MCU/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_gpio.c) | ST 제공 HAL/LL | GPIO pin 입출력의 HAL API · C 구현 |
| [stm32h7xx_hal_hsem.c](../../src/firmware/mcu/FMCW_LiDAR_MCU/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_hsem.c) | ST 제공 HAL/LL | hardware semaphore의 HAL API · C 구현 |
| [stm32h7xx_hal_i2c.c](../../src/firmware/mcu/FMCW_LiDAR_MCU/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_i2c.c) | ST 제공 HAL/LL | I2C 통신의 HAL API · C 구현 |
| [stm32h7xx_hal_i2c_ex.c](../../src/firmware/mcu/FMCW_LiDAR_MCU/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_i2c_ex.c) | ST 제공 HAL/LL | I2C 통신의 HAL API 확장 기능 · C 구현 |
| [stm32h7xx_hal_mdma.c](../../src/firmware/mcu/FMCW_LiDAR_MCU/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_mdma.c) | ST 제공 HAL/LL | MDMA 데이터 전송의 HAL API · C 구현 |
| [stm32h7xx_hal_pwr.c](../../src/firmware/mcu/FMCW_LiDAR_MCU/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pwr.c) | ST 제공 HAL/LL | 전원 관리의 HAL API · C 구현 |
| [stm32h7xx_hal_pwr_ex.c](../../src/firmware/mcu/FMCW_LiDAR_MCU/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pwr_ex.c) | ST 제공 HAL/LL | 전원 관리의 HAL API 확장 기능 · C 구현 |
| [stm32h7xx_hal_rcc.c](../../src/firmware/mcu/FMCW_LiDAR_MCU/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_rcc.c) | ST 제공 HAL/LL | clock·reset의 HAL API · C 구현 |
| [stm32h7xx_hal_rcc_ex.c](../../src/firmware/mcu/FMCW_LiDAR_MCU/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_rcc_ex.c) | ST 제공 HAL/LL | clock·reset의 HAL API 확장 기능 · C 구현 |
| [stm32h7xx_hal_spi.c](../../src/firmware/mcu/FMCW_LiDAR_MCU/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_spi.c) | ST 제공 HAL/LL | SPI 통신의 HAL API · C 구현 |
| [stm32h7xx_hal_spi_ex.c](../../src/firmware/mcu/FMCW_LiDAR_MCU/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_spi_ex.c) | ST 제공 HAL/LL | SPI 통신의 HAL API 확장 기능 · C 구현 |
| [stm32h7xx_hal_tim.c](../../src/firmware/mcu/FMCW_LiDAR_MCU/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_tim.c) | ST 제공 HAL/LL | timer·PWM·capture의 HAL API · C 구현 |
| [stm32h7xx_hal_tim_ex.c](../../src/firmware/mcu/FMCW_LiDAR_MCU/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_tim_ex.c) | ST 제공 HAL/LL | timer·PWM·capture의 HAL API 확장 기능 · C 구현 |
| [stm32h7xx_hal_uart.c](../../src/firmware/mcu/FMCW_LiDAR_MCU/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_uart.c) | ST 제공 HAL/LL | UART 비동기 직렬 통신의 HAL API · C 구현 |
| [stm32h7xx_hal_uart_ex.c](../../src/firmware/mcu/FMCW_LiDAR_MCU/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_uart_ex.c) | ST 제공 HAL/LL | UART 비동기 직렬 통신의 HAL API 확장 기능 · C 구현 |

<a id="folder-31"></a>
## `src/network/`

| 파일 | 구분 | 하는 일 |
|---|---|---|
| [udp_point_protocol.cpp](../../src/network/udp_point_protocol.cpp) | PC/Jetson 제품 코드 | 점군 frame을 packet으로 분할·인코딩하고 수신 packet을 해석하는 전송 규격 · C++ 구현 |
| [udp_point_protocol.h](../../src/network/udp_point_protocol.h) | PC/Jetson 제품 코드 | 점군 frame을 packet으로 분할·인코딩하고 수신 packet을 해석하는 전송 규격 · 헤더: 선언·자료형·상수/inline 코드 |
| [udp_sender_service.cpp](../../src/network/udp_sender_service.cpp) | PC/Jetson 제품 코드 | UDP socket, 송신 대기열·worker와 전송·정지 상태 관리 · C++ 구현 |
| [udp_sender_service.h](../../src/network/udp_sender_service.h) | PC/Jetson 제품 코드 | UDP socket, 송신 대기열·worker와 전송·정지 상태 관리 · 헤더: 선언·자료형·상수/inline 코드 |

<a id="folder-32"></a>
## `src/processing/`

| 파일 | 구분 | 하는 일 |
|---|---|---|
| [fft_backend.h](../../src/processing/fft_backend.h) | PC/Jetson 제품 코드 | FFT 길이·배치 plan과 CPU/GPU 구현이 따르는 IFftBackend 인터페이스 · 헤더: 선언·자료형·상수/inline 코드 |
| [fft_backends.cpp](../../src/processing/fft_backends.cpp) | PC/Jetson 제품 코드 | FFTW/CUDA 구현 선언과 선택 factory, 빌드하지 않은 backend의 사용 불가 처리 · C++ 구현 |
| [fft_backends.h](../../src/processing/fft_backends.h) | PC/Jetson 제품 코드 | FFTW/CUDA 구현 선언과 선택 factory, 빌드하지 않은 backend의 사용 불가 처리 · 헤더: 선언·자료형·상수/inline 코드 |
| [point_cloud_postprocessor.cpp](../../src/processing/point_cloud_postprocessor.cpp) | PC/Jetson 제품 코드 | 화면 표시용 점군 누적·수직 보간. 저장·UDP의 원본 점 계약은 유지 · C++ 구현 |
| [point_cloud_postprocessor.h](../../src/processing/point_cloud_postprocessor.h) | PC/Jetson 제품 코드 | 화면 표시용 점군 누적·수직 보간. 저장·UDP의 원본 점 계약은 유지 · 헤더: 선언·자료형·상수/inline 코드 |
| [processing_service.cpp](../../src/processing/processing_service.cpp) | PC/Jetson 제품 코드 | 신호 처리 대기열·worker·설정 변경·지연 통계와 결과 callback 관리 · C++ 구현 |
| [processing_service.h](../../src/processing/processing_service.h) | PC/Jetson 제품 코드 | 신호 처리 대기열·worker·설정 변경·지연 통계와 결과 callback 관리 · 헤더: 선언·자료형·상수/inline 코드 |
| [processing_snapshots.cpp](../../src/processing/processing_snapshots.cpp) | PC/Jetson 제품 코드 | 파형·FFT·라인·B-scan·점군 결과를 모아 화면 등에서 읽는 snapshot 제공 · C++ 구현 |
| [processing_snapshots.h](../../src/processing/processing_snapshots.h) | PC/Jetson 제품 코드 | 파형·FFT·라인·B-scan·점군 결과를 모아 화면 등에서 읽는 snapshot 제공 · 헤더: 선언·자료형·상수/inline 코드 |
| [signal_processor.cpp](../../src/processing/signal_processor.cpp) | PC/Jetson 제품 코드 | RAW 전처리·window·FFT·피크·거리·속도·좌표 계산과 GPU 배치 처리 연결 · C++ 구현 |
| [signal_processor.h](../../src/processing/signal_processor.h) | PC/Jetson 제품 코드 | RAW 전처리·window·FFT·피크·거리·속도·좌표 계산과 GPU 배치 처리 연결 · 헤더: 선언·자료형·상수/inline 코드 |

<a id="folder-33"></a>
## `src/processing/cpu/`

| 파일 | 구분 | 하는 일 |
|---|---|---|
| [fftw_backend.cpp](../../src/processing/cpu/fftw_backend.cpp) | PC/Jetson 제품 코드 | FFTW plan 생성·메모리 관리·FFT 실행을 연결하는 CPU 구현 · C++ 구현 |

<a id="folder-34"></a>
## `src/processing/cuda/`

| 파일 | 구분 | 하는 일 |
|---|---|---|
| [cuda_fft_backend.cu](../../src/processing/cuda/cuda_fft_backend.cu) | PC/Jetson 제품 코드 | cuFFT plan·GPU 메모리·FFT 실행을 연결하는 CUDA 구현 · CUDA 구현 |
| [cuda_module_policy.h](../../src/processing/cuda/cuda_module_policy.h) | PC/Jetson 제품 코드 | CUDA_MODULE_LOADING을 EAGER로 설정하는 inline 지원 함수 · 헤더: 선언·자료형·상수/inline 코드 |
| [cuda_signal_pipeline.cu](../../src/processing/cuda/cuda_signal_pipeline.cu) | PC/Jetson 제품 코드 | GPU에서 배치 전처리·FFT·피크·거리·속도 계산을 수행하고 비동기 결과 회수 · CUDA 구현 |
| [cuda_signal_pipeline.h](../../src/processing/cuda/cuda_signal_pipeline.h) | PC/Jetson 제품 코드 | GPU에서 배치 전처리·FFT·피크·거리·속도 계산을 수행하고 비동기 결과 회수 · 헤더: 선언·자료형·상수/inline 코드 |
| [cuda_signal_pipeline_stub.cpp](../../src/processing/cuda/cuda_signal_pipeline_stub.cpp) | PC/Jetson 제품 코드 | CUDA를 빌드하지 않을 때 GPU 처리 사용 불가를 반환하는 대체 구현 · C++ 구현 |

<a id="folder-35"></a>
## `src/storage/`

| 파일 | 구분 | 하는 일 |
|---|---|---|
| [async_storage_service.cpp](../../src/storage/async_storage_service.cpp) | PC/Jetson 제품 코드 | 저장 대기열·writer 작업·flush·종료·오류 상태 관리 · C++ 구현 |
| [async_storage_service.h](../../src/storage/async_storage_service.h) | PC/Jetson 제품 코드 | 저장 대기열·writer 작업·flush·종료·오류 상태 관리 · 헤더: 선언·자료형·상수/inline 코드 |
| [binary_storage.cpp](../../src/storage/binary_storage.cpp) | PC/Jetson 제품 코드 | RAW·점군 바이너리 기록, RAW 파일을 읽는 RawReplayReader · C++ 구현 |
| [binary_storage.h](../../src/storage/binary_storage.h) | PC/Jetson 제품 코드 | RAW·점군 바이너리 기록, RAW 파일을 읽는 RawReplayReader · 헤더: 선언·자료형·상수/inline 코드 |
| [processing_history.cpp](../../src/storage/processing_history.cpp) | PC/Jetson 제품 코드 | 처리 설정 변경 시점·설정값 기록 및 RAW 재생용 처리 이력 읽기 · C++ 구현 |
| [processing_history.h](../../src/storage/processing_history.h) | PC/Jetson 제품 코드 | 처리 설정 변경 시점·설정값 기록 및 RAW 재생용 처리 이력 읽기 · 헤더: 선언·자료형·상수/inline 코드 |
| [writer_interfaces.h](../../src/storage/writer_interfaces.h) | PC/Jetson 제품 코드 | RAW/점군 writer 인터페이스, 세션·열기·종료 옵션, 저장 대기열 결과·상태 · 헤더: 선언·자료형·상수/inline 코드 |

<a id="folder-36"></a>
## `src/ui/`

| 파일 | 구분 | 하는 일 |
|---|---|---|
| [main_window.cpp](../../src/ui/main_window.cpp) | PC/Jetson 제품 코드 | 전체 창·페이지·버튼·입력칸·상태 표시 구성 및 controller와의 연결 · C++ 구현 |
| [main_window.h](../../src/ui/main_window.h) | PC/Jetson 제품 코드 | 전체 창·페이지·버튼·입력칸·상태 표시 구성 및 controller와의 연결 · 헤더: 선언·자료형·상수/inline 코드 |

<a id="folder-37"></a>
## `src/ui/plots/`

| 파일 | 구분 | 하는 일 |
|---|---|---|
| [plot_widgets.cpp](../../src/ui/plots/plot_widgets.cpp) | PC/Jetson 제품 코드 | 선 그래프·B-scan heatmap·up/down 구간 표시와 축·범위·갱신 통계 처리 · C++ 구현 |
| [plot_widgets.h](../../src/ui/plots/plot_widgets.h) | PC/Jetson 제품 코드 | 선 그래프·B-scan heatmap·up/down 구간 표시와 축·범위·갱신 통계 처리 · 헤더: 선언·자료형·상수/inline 코드 |

<a id="folder-38"></a>
## `src/ui/point_cloud/`

| 파일 | 구분 | 하는 일 |
|---|---|---|
| [point_cloud_camera.h](../../src/ui/point_cloud/point_cloud_camera.h) | PC/Jetson 제품 코드 | 3D 카메라의 회전·이동·확대와 투영·화면 좌표 계산. 헤더 안에 구현 포함 · 헤더: 선언·자료형·상수/inline 코드 |
| [point_cloud_widget.cpp](../../src/ui/point_cloud/point_cloud_widget.cpp) | PC/Jetson 제품 코드 | 3D 점군 위젯, OpenGL/대체 표시 경로와 마우스 조작 처리 · C++ 구현 |
| [point_cloud_widget.h](../../src/ui/point_cloud/point_cloud_widget.h) | PC/Jetson 제품 코드 | 3D 점군 위젯, OpenGL/대체 표시 경로와 마우스 조작 처리 · 헤더: 선언·자료형·상수/inline 코드 |

## 목록 갱신

다음 명령은 Git 작업 공간에서 사용합니다. Jetson 소스 배포 ZIP에서는 생성된 문서를 읽으면 됩니다.
파일을 추가·이동했다면 `tools/generate_source_index.py`의 역할 설명을 먼저 갱신한 다음 실행합니다.
설명이 없는 새 파일은 오류로 표시하므로 자동으로 누락되지 않습니다.

```powershell
python tools/generate_source_index.py
python tools/generate_source_index.py --check
```

`--check`는 파일 누락·설명 누락과 생성 결과의 일치 여부를 확인하며 파일을 수정하지 않습니다.
