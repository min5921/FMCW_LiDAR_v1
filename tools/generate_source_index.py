"""Generate the Korean per-file source guide from a reviewed role catalog.

Run from any directory: python tools/generate_source_index.py
Use --check to verify coverage, links and the checked-in output without writing.
Unknown files fail explicitly so new source files get a human-written description.
"""
from __future__ import annotations

import argparse
from collections import Counter, defaultdict
import os
from pathlib import Path
import re
import subprocess
from urllib.parse import quote

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / 'docs/source/source_file_index_ko.md'

# Keys are repository-relative paths without the .h/.cpp/.cu extension.
HOST_ROLES = {
    'apps/windows/main': 'Windows Qt 앱 시작, MainWindow 생성, 명령행 옵션 처리',
    'apps/jetson/main': 'Jetson Qt 앱 시작, MainWindow 생성, 명령행 옵션 처리',
    'application/application_controller': '화면의 실행 요청을 worker에 전달하고 장치·계측·처리·저장 연결, 시작·정지 및 상태·결과 전달을 조정',
    'application/replay_setup_loader': 'RAW에 기록된 설정을 읽어 재생 준비에 사용하고 재생 경로 변경 여부를 확인',
    'ui/main_window': '전체 창·페이지·버튼·입력칸·상태 표시 구성 및 controller와의 연결',
    'ui/plots/plot_widgets': '선 그래프·B-scan heatmap·up/down 구간 표시와 축·범위·갱신 통계 처리',
    'ui/point_cloud/point_cloud_widget': '3D 점군 위젯, OpenGL/대체 표시 경로와 마우스 조작 처리',
    'ui/point_cloud/point_cloud_camera': '3D 카메라의 회전·이동·확대와 투영·화면 좌표 계산. 헤더 안에 구현 포함',
    'core/acquisition/acquisition_session': 'digitizer·MCU·EDFA의 계측 설정·연결·준비·트리거·데이터 수신·정지 관리',
    'core/acquisition/continuous_acquisition_worker': '별도 worker에서 연속 수집하고 RAW 배치를 전달하며 중단·종료 상태 관리',
    'core/acquisition/raw_frame_batch_pool': 'RAW 배치를 담을 메모리 버퍼의 재사용 관리',
    'core/config/config_document': '프로젝트 설정 YAML 파싱, 항목 조회·병합 및 YAML/JSON 직렬화',
    'core/config/config_profile': '설정 문서와 SystemConfig 변환, 계층별 설정 읽기, 프로필·스냅샷 저장',
    'core/config/config_types': '계측·처리·저장 등의 설정 자료형과 enum/문자열 변환',
    'core/config/config_validation': '설정의 값 범위·지원 사양·항목 조합 검증과 오류·경고 반환',
    'core/config/config_policy': '초기 설계의 항목별 변경 정책표. 현재 GUI 동작 허용 여부는 RuntimeWorker도 확인',
    'core/runtime/system_state': '동작 상태, 상태 전환 허용 여부와 상태 문자열 표현',
    'core/runtime/cancellation': '작업 취소 여부를 전달·확인하는 callback 형식과 inline 함수',
    'core/runtime/stop_result': '정지 단계별 오류를 수집하고 성공 여부·오류 요약 반환',
    'core/runtime/realtime_thread': '처리 스레드와 프로세스의 우선순위 설정을 위한 플랫폼별 지원',
    'core/runtime/system_telemetry': '계측 설정·연결·실행 상태와 digitizer·MCU·EDFA 상태를 묶는 자료형',
    'core/devices/device_interfaces': '계측기·MCU·EDFA·UDP의 공통 호출 인터페이스와 장치 상태·파형 자료형',
    'core/devices/digitizer_capabilities': 'Alazar 보드별 지원 사양과 샘플링 속도·레코드 길이·입력 범위 등 검사',
    'core/types/frame_types': '샘플 버퍼, RAW 프레임·배치, 스캔 위치, 피크와 처리 결과·XYZ 점의 공통 자료형',
    'core/types/scan_trajectory': '스캔 raster 위치 계산, 생성 데이터의 위치 기록과 RAW 재생 위치 정렬',
    'core/diagnostics/app_version': '프로그램 버전 정보와 표시용 버전 문자열',
    'core/diagnostics/logging': '로그 수준·항목·메모리 저장과 시간·문자열 표시',
    'drivers/runtime_adapter_factory': '입력 종류에 맞는 digitizer 생성. MCU·EDFA는 별도 프로필 모드를 따르는 serial controller로 연결',
    'drivers/alazar/alazar_digitizer': 'ATS-SDK로 보드 검색·설정·DMA 수집·정지를 수행하는 계측 adapter',
    'drivers/alazar/alazar_sample_conversion': 'Alazar left-aligned ADC 샘플을 signed 16-bit 표현으로 변환',
    'drivers/serial/serial_transport': 'Windows COM/Linux tty 열기·읽기·쓰기·종료와 포트 조회',
    'drivers/mcu/mcu_protocol': '파형 구성·XYM 파일 읽기, MCU 명령 생성과 ACK/오류 응답 해석',
    'drivers/mcu/mcu_serial_controller': 'MCU 시리얼 연결, 파형 업로드·시작·정지 명령과 응답·진행 상태 처리',
    'drivers/edfa/edfa_protocol': 'EDFA 명령 packet 생성과 상태·출력·모드·활성화 응답 해석',
    'drivers/edfa/edfa_serial_controller': 'EDFA 시리얼 연결과 명령 왕복, 출력 설정·상태 관리',
    'drivers/replay/replay_digitizer': 'RAW 파일을 읽어 IDigitizer 공통 인터페이스로 데이터를 공급',
    'drivers/simulator/fake_digitizer': '계측기 없이 사용하는 모의 측정 데이터 생성',
    'drivers/simulator/fake_mcu': 'MCU 상태·파형 제어를 모의 구현. 실제 runtime 선택은 adapter factory와 controller 설정 참조',
    'drivers/simulator/fake_edfa': 'EDFA 상태·출력 제어를 모의 구현. 실제 runtime 선택은 adapter factory와 controller 설정 참조',
    'processing/signal_processor': 'RAW 전처리·window·FFT·피크·거리·속도·좌표 계산과 GPU 배치 처리 연결',
    'processing/processing_service': '신호 처리 대기열·worker·설정 변경·지연 통계와 결과 callback 관리',
    'processing/processing_snapshots': '파형·FFT·라인·B-scan·점군 결과를 모아 화면 등에서 읽는 snapshot 제공',
    'processing/point_cloud_postprocessor': '화면 표시용 점군 누적·수직 보간. 저장·UDP의 원본 점 계약은 유지',
    'processing/fft_backend': 'FFT 길이·배치 plan과 CPU/GPU 구현이 따르는 IFftBackend 인터페이스',
    'processing/fft_backends': 'FFTW/CUDA 구현 선언과 선택 factory, 빌드하지 않은 backend의 사용 불가 처리',
    'processing/cpu/fftw_backend': 'FFTW plan 생성·메모리 관리·FFT 실행을 연결하는 CPU 구현',
    'processing/cuda/cuda_fft_backend': 'cuFFT plan·GPU 메모리·FFT 실행을 연결하는 CUDA 구현',
    'processing/cuda/cuda_signal_pipeline': 'GPU에서 배치 전처리·FFT·피크·거리·속도 계산을 수행하고 비동기 결과 회수',
    'processing/cuda/cuda_signal_pipeline_stub': 'CUDA를 빌드하지 않을 때 GPU 처리 사용 불가를 반환하는 대체 구현',
    'processing/cuda/cuda_module_policy': 'CUDA_MODULE_LOADING을 EAGER로 설정하는 inline 지원 함수',
    'storage/writer_interfaces': 'RAW/점군 writer 인터페이스, 세션·열기·종료 옵션, 저장 대기열 결과·상태',
    'storage/binary_storage': 'RAW·점군 바이너리 기록, RAW 파일을 읽는 RawReplayReader',
    'storage/async_storage_service': '저장 대기열·writer 작업·flush·종료·오류 상태 관리',
    'storage/processing_history': '처리 설정 변경 시점·설정값 기록 및 RAW 재생용 처리 이력 읽기',
    'network/udp_point_protocol': '점군 frame을 packet으로 분할·인코딩하고 수신 packet을 해석하는 전송 규격',
    'network/udp_sender_service': 'UDP socket, 송신 대기열·worker와 전송·정지 상태 관리',
}

MCU_ROLES = {
    'main': 'MCU 핀 정의·주변장치 초기화·부팅과 메인 루프',
    'uart_cmd': 'UART 수신 명령을 처리하고 파형 업로드·시작·정지와 ACK/오류 응답 연결',
    'frame_player': '업로드 파형 buffer·재생 상태와 timer tick에 따른 DAC word·marker 출력',
    'ad5664': 'AD5664 DAC 명령 word와 A/B/C/D 채널 전송 word 구성',
    'spi24_tx': '24-bit SPI 전송, CS 제어, timeout 및 오류 상태 관리',
    'mirrorcle_drv': '미러 구동 DAC 초기화·bias·출력 enable/disable 제어',
    'stm32h7xx_it': 'MCU interrupt handler와 주변장치 interrupt 처리 연결',
    'stm32h7xx_hal_msp': 'HAL 초기화에 필요한 pin·clock·interrupt 등의 보드 설정',
    'stm32h7xx_hal_conf': '프로젝트에서 사용할 HAL 모듈과 설정 상수',
    'system_stm32h7xx': 'MCU system 초기화와 clock 관련 기반 코드',
    'syscalls': 'C runtime의 시스템 함수 연결을 위한 기본 구현',
    'sysmem': 'C runtime에서 사용할 heap 메모리 할당 지원',
}

PERIPHERALS = {
    'uart': 'UART 비동기 직렬 통신', 'usart': 'USART 직렬 통신',
    'lpuart': '저전력 UART', 'spi': 'SPI 통신', 'i2c': 'I2C 통신',
    'tim': 'timer·PWM·capture', 'rcc': 'clock·reset', 'pwr': '전원 관리',
    'gpio': 'GPIO pin 입출력', 'exti': '외부 interrupt/event',
    'dma': 'DMA 데이터 전송', 'mdma': 'MDMA 데이터 전송',
    'dmamux': 'DMA 요청 연결', 'hsem': 'hardware semaphore',
    'flash': 'Flash 메모리', 'cortex': 'Cortex CPU·interrupt 제어',
    'bus': 'bus와 주변장치 clock', 'crs': 'clock 복구·동기화',
    'system': 'system 제어', 'utils': '저수준 초기화·시간·clock 지원',
    'def': '공통 HAL 자료형·상수·매크로',
}

def file_kind(path: Path) -> str:
    return {'.h': '헤더: 선언·자료형·상수/inline 코드',
            '.cpp': 'C++ 구현', '.cu': 'CUDA 구현', '.c': 'C 구현'}.get(path.suffix, '설정/문서')

def describe(relative: str) -> tuple[str, str]:
    path = Path(relative)
    if relative == 'src/CMakeLists.txt':
        return '빌드 설정', '제품 소스 목록, core·Qt·실행 target과 SDK·FFTW·CUDA 연결'
    if relative == 'src/README.md':
        return '안내 문서', '역할별 소스 위치를 빠르게 찾는 지도와 상세 안내서 링크'
    short = relative.removeprefix('src/')
    if not short.startswith('firmware/'):
        key = str(Path(short).with_suffix('')).replace('\\', '/')
        return 'PC/Jetson 제품 코드', HOST_ROLES[key] + ' · ' + file_kind(path)

    fw = short.removeprefix('firmware/mcu/FMCW_LiDAR_MCU/')
    if fw.startswith(('Core/Inc/', 'Core/Src/')):
        return 'MCU 프로젝트 코드', MCU_ROLES[path.stem] + ' · ' + file_kind(path)
    if fw.startswith('Core/Startup/'):
        return 'MCU 시작 코드', '리셋 후 실행 시작과 STM32H750 interrupt vector 정의'
    if fw.startswith('Drivers/'):
        if path.name == 'LICENSE.txt':
            return '제공 코드의 라이선스', '해당 ARM/ST 제공 코드의 이용 조건'
        if 'STM32H7xx_HAL_Driver' in fw:
            if path.stem == 'stm32_hal_legacy':
                return 'ST 제공 HAL/LL', '이전 HAL 이름과의 호환 매크로·정의'
            if path.stem == 'stm32h7xx_hal':
                return 'ST 제공 HAL/LL', 'HAL 공통 초기화·tick 등 기반 기능 · ' + file_kind(path)
            match = re.fullmatch(r'stm32h7xx_(hal|ll)_(.+?)(_ex)?', path.stem)
            if not match:
                raise KeyError(relative)
            module = PERIPHERALS[match.group(2)]
            layer = 'HAL API' if match.group(1) == 'hal' else 'LL 저수준 API'
            return 'ST 제공 HAL/LL', f'{module}의 {layer}' + (' 확장 기능' if match.group(3) else '') + ' · ' + file_kind(path)
        if 'CMSIS/' in fw:
            name = path.stem
            if '/Device/' in fw:
                role = {
                    'stm32h750xx': 'STM32H750의 register·주변장치·interrupt 정의',
                    'stm32h7xx': '선택한 STM32H7 장치 정의와 HAL 연결을 위한 공통 include',
                    'system_stm32h7xx': 'SystemInit·SystemCoreClock 등의 선언',
                }[name]
            elif name.startswith('core_'):
                role = f'{name.removeprefix("core_")} CPU/아키텍처용 CMSIS register·core 기능 정의 (공급 패키지에 함께 포함)'
            elif name.startswith('cmsis_'):
                role = {
                    'cmsis_version': 'CMSIS 버전 상수',
                    'cmsis_compiler': 'compiler별 CMSIS 지원 헤더 선택',
                    'cmsis_gcc': 'GCC용 CMSIS 명령·compiler 지원',
                    'cmsis_iccarm': 'IAR용 CMSIS 명령·compiler 지원',
                    'cmsis_armcc': 'Arm Compiler용 CMSIS 명령·compiler 지원',
                    'cmsis_armclang': 'Arm Clang용 CMSIS 명령·compiler 지원',
                    'cmsis_armclang_ltm': 'Arm Clang LTM용 CMSIS 명령·compiler 지원',
                }[name]
            elif name.startswith('mpu_armv'):
                role = f'{name.removeprefix("mpu_")} 계열 MPU 메모리 보호 설정 지원'
            elif name == 'tz_context':
                role = 'TrustZone context 관리 인터페이스 (공급 패키지에 함께 포함)'
            else:
                raise KeyError(relative)
            return 'ARM/ST 제공 CMSIS', role + ' · 헤더'
        raise KeyError(relative)

    metadata = {
        'README.md': 'MCU 프로젝트 여는 법, 타이머·파형 protocol·동작 설명',
        'FMCW_LiDAR_MCU.ioc': 'CubeMX pin·clock·주변장치 설정과 코드 생성 입력',
        'FMCW_LiDAR_MCU Debug.launch': 'CubeIDE debugger·다운로드·실행 설정',
        'STM32H750VBTX_FLASH.ld': 'Flash 실행을 위한 코드·데이터·메모리 배치',
        'STM32H750VBTX_RAM.ld': 'RAM 실행을 위한 코드·데이터·메모리 배치',
        '.project': 'Eclipse/CubeIDE 프로젝트 이름·성격·builder 등록',
        '.cproject': 'CubeIDE C/C++ compiler·linker·빌드 구성',
        '.mxproject': 'CubeMX 프로젝트와 생성 코드 관련 메타데이터',
        '.settings/org.eclipse.core.resources.prefs': 'IDE 리소스·문자 인코딩 설정',
        '.settings/stm32cubeide.project.prefs': 'STM32CubeIDE 프로젝트 설정',
        '.settings/com.st.stm32cube.ide.mcu.sfrview.prefs': 'MCU register 보기 기능의 IDE 설정',
    }
    return 'MCU 설정/문서', metadata[fw]

def source_files() -> list[str]:
    raw = subprocess.check_output(
        ['git', 'ls-files', '-z', '--cached', '--others', '--exclude-standard', '--', 'src'], cwd=ROOT)
    return sorted({s for s in raw.decode('utf-8').split('\0') if s and (ROOT / s).is_file()
                   and not any(part in ('Debug', 'Release', '__pycache__') for part in Path(s).parts)
                   and Path(s).name != 'language.settings.xml'})

def render(files: list[str]) -> str:
    groups: dict[str, list[tuple[str, str, str]]] = defaultdict(list)
    counts: Counter[str] = Counter()
    for file in files:
        category, role = describe(file)
        counts[category] += 1
        groups[Path(file).parent.as_posix()].append((file, category, role))
    lines = [
        '# 기본 FMCW 소스 파일별 역할 목록', '',
        '[처음 읽는 설명서](source_guide_ko.md) · [짧은 소스 지도](../../src/README.md)', '',
        f'기준: 2026-09-22 정리 이후 기본 작업 공간. **`src/` 파일 {len(files)}개**를 각각 설명합니다.', '',
        '파일명은 클릭하면 해당 소스로 이동합니다. `Ctrl+F`로 파일명이나 역할을 찾을 수 있습니다.',
        '제품 코드와 MCU 코드, ST/ARM 제공 코드를 구분했습니다. 제공 파일의 존재가 실제 기능 사용을 뜻하지는 않습니다.',
        'Git 대상 소스를 기준으로 하며 삭제된 옛 경로·빌드 산출물·개인 indexer 설정은 제외합니다.', '',
        '| 분류 | 파일 수 |', '|---|---:|',
        *[f'| {kind} | {count} |' for kind, count in sorted(counts.items())], '',
        '## 폴더 바로가기', '',
        *[f'- [{folder}](#folder-{i})' for i, folder in enumerate(sorted(groups), 1)], '',
    ]
    for i, (folder, entries) in enumerate(sorted(groups.items()), 1):
        lines += [f'<a id="folder-{i}"></a>', f'## `{folder}/`', '',
                  '| 파일 | 구분 | 하는 일 |', '|---|---|---|']
        for file, category, role in entries:
            target = quote(os.path.relpath(ROOT / file, OUTPUT.parent).replace('\\', '/'), safe='/')
            lines.append(f'| [{Path(file).name}]({target}) | {category} | {role} |')
        lines += ['']
    lines += ['## 목록 갱신', '',
              '다음 명령은 Git 작업 공간에서 사용합니다. Jetson 소스 배포 ZIP에서는 생성된 문서를 읽으면 됩니다.',
              '파일을 추가·이동했다면 `tools/generate_source_index.py`의 역할 설명을 먼저 갱신한 다음 실행합니다.',
              '설명이 없는 새 파일은 오류로 표시하므로 자동으로 누락되지 않습니다.', '',
              '```powershell', 'python tools/generate_source_index.py',
              'python tools/generate_source_index.py --check', '```', '',
              '`--check`는 파일 누락·설명 누락과 생성 결과의 일치 여부를 확인하며 파일을 수정하지 않습니다.', '']
    return '\n'.join(lines)

def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    files = source_files()
    output = render(files)
    if args.check:
        if not OUTPUT.exists() or OUTPUT.read_text(encoding='utf-8') != output:
            raise SystemExit('Source index is outdated. Run tools/generate_source_index.py.')
    else:
        OUTPUT.write_text(output, encoding='utf-8', newline='\n')
    print(f'Covered {len(files)} source files; all roles and file targets resolved.')

if __name__ == '__main__':
    main()
