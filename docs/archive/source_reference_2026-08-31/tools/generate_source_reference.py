#!/usr/bin/env python3
"""Generate the FMCW LiDAR source-code reference manuals.

The generator intentionally documents project-authored code and identifies
vendor/generated boundaries instead of expanding STM32 HAL, CMSIS, or legacy
third-party headers into the function reference.
"""

from __future__ import annotations

import argparse
import ast
import json
import os
import re
import shutil
import subprocess
import sys
import textwrap
from collections import defaultdict
from dataclasses import dataclass, field
from datetime import date
from pathlib import Path
from typing import Iterable, Sequence

from docx import Document
from docx.enum.section import WD_SECTION
from docx.enum.style import WD_STYLE_TYPE
from docx.enum.table import WD_CELL_VERTICAL_ALIGNMENT, WD_TABLE_ALIGNMENT
from docx.enum.text import WD_ALIGN_PARAGRAPH, WD_BREAK
from docx.oxml import OxmlElement
from docx.oxml.ns import qn
from docx.shared import Inches, Pt, RGBColor


ROOT = Path(__file__).resolve().parents[4]
OUTPUT_DIR = ROOT / "docs" / "archive" / "source_reference_2026-08-31"
ASSET_DIR = OUTPUT_DIR / "assets"
ANALYSIS_DIR = OUTPUT_DIR / "analysis"

GENERATED_ON = date(2026, 8, 31)
PROJECT_NAME = "FMCW LiDAR"
PROJECT_VERSION = "0.2.0"

INK = "18202A"
BLUE = "2E74B5"
DARK_BLUE = "1F4D78"
MUTED = "5D6975"
LIGHT_BLUE = "E8EEF5"
LIGHT_GRAY = "F2F4F7"
CALL_OUT = "F4F6F9"
WHITE = "FFFFFF"
TEAL = "147D73"
GOLD = "7A5A00"
RED = "9B1C1C"

BODY_FONT = "Calibri"
EAST_ASIA_FONT = "Malgun Gothic"
CODE_FONT = "Consolas"
PAGE_WIDTH_DXA = 9360
TABLE_INDENT_DXA = 120


@dataclass
class ParameterInfo:
    declaration: str
    name: str = ""
    type_text: str = ""
    direction: str = "입력"


@dataclass
class FunctionInfo:
    file: str
    line: int
    name: str
    qualified_name: str
    signature: str
    return_type: str
    parameters: list[ParameterInfo] = field(default_factory=list)
    calls: list[str] = field(default_factory=list)
    callers: list[str] = field(default_factory=list)
    assignments: list[str] = field(default_factory=list)
    traits: list[str] = field(default_factory=list)
    body: str = ""
    language: str = "C++"
    declaration_only: bool = False


@dataclass
class FileInfo:
    path: str
    language: str
    role: str
    functions: list[FunctionInfo] = field(default_factory=list)
    declarations: list[str] = field(default_factory=list)
    includes: list[str] = field(default_factory=list)
    lines: int = 0
    category: str = ""


MANUALS = [
    (
        "FMCW_LiDAR_00_Architecture_and_Source_Index_KO.docx",
        "시스템 아키텍처 및 소스 인덱스",
        "전체 런타임 데이터 흐름, 스레드 경계, 구성·프레임 계약, 문서 범위와 파일별 찾아보기",
        "architecture",
    ),
    (
        "FMCW_LiDAR_01_Core_Configuration_Acquisition_KO.docx",
        "Core·구성·수집 참조서",
        "상태 머신, 구성 문서, 설정 검증, 수집 세션, DMA 작업자와 궤적 매핑",
        "core",
    ),
    (
        "FMCW_LiDAR_02_Device_Drivers_Protocols_KO.docx",
        "장치 드라이버 및 프로토콜 참조서",
        "AlazarTech, MCU, EDFA, 직렬 통신, 시뮬레이터·재생 어댑터",
        "drivers",
    ),
    (
        "FMCW_LiDAR_03_Processing_Detection_KO.docx",
        "신호처리 및 객체 검출 참조서",
        "CPU/GPU FFT 공통 파이프라인, peak 보간, 거리·속도·XYZIV, CenterPoint 입력과 검출",
        "processing_detection",
    ),
    (
        "FMCW_LiDAR_04_Storage_Network_Replay_KO.docx",
        "저장·네트워크·재생 참조서",
        "Raw/Point Cloud 저장, 비동기 writer, UDP XYZIV 규격, PCD/BIN/CSV 재생",
        "storage_network",
    ),
    (
        "FMCW_LiDAR_05_Qt_GUI_Applications_KO.docx",
        "Qt GUI 및 애플리케이션 참조서",
        "Windows/Jetson 진입점, ApplicationController, MainWindow, 2D/3D 플롯 위젯",
        "gui",
    ),
    (
        "FMCW_LiDAR_06_MCU_Firmware_KO.docx",
        "MCU 펌웨어 참조서",
        "STM32H750 파형 업로드·재생, DAC/SPI, B-trigger, UART 명령과 인터럽트 흐름",
        "mcu",
    ),
    (
        "FMCW_LiDAR_07_ROS_Tools_Deployment_Tests_KO.docx",
        "ROS·도구·배포·테스트 참조서",
        "ROS/rviz UDP 수신, Waymo 변환, Windows/Jetson 배포, 빌드 옵션과 검증 테스트",
        "support",
    ),
]


SPECIAL_FILE_ROLES = {
    "src/core/acquisition_session.cpp": "장치 3종(디지타이저·EDFA·MCU)의 configure/connect/arm/trigger/stop 순서를 단일 세션으로 조정하고, DMA frame/batch에 구성 revision·광 상태·스캔 좌표를 부여한다.",
    "src/core/continuous_acquisition_worker.cpp": "전용 수집 스레드에서 연속 DMA batch를 기다리고 처리 큐로 전달하며 timeout, overflow, stop 요청과 수집 telemetry를 관리한다.",
    "src/core/operation_controller.cpp": "사용자 명령을 상태 머신 전이로 제한하고 시작·정지·기록·pause·오류 원인을 StopContext로 일관되게 기록한다.",
    "src/core/scan_trajectory.cpp": "MCU marker와 record index를 파형 sample index, raster index, azimuth/elevation으로 변환한다. bidirectional 홀수 line 역방향과 top-to-bottom elevation 규칙이 여기서 적용된다.",
    "src/core/config_document.cpp": "제한된 YAML 형식의 파서·serializer. 중첩 mapping과 scalar를 ConfigNode 트리로 변환하고 알 수 없는/잘못된 형식을 오류로 보고한다.",
    "src/core/config_profile.cpp": "ConfigDocument와 SystemConfig 사이의 필드별 변환 및 profile 파일 load/save를 담당한다.",
    "src/core/config_validation.cpp": "보드 capability, chirp segmentation, trigger, FFT bin, scan geometry, EDFA/MCU 설정의 교차 제약을 ValidationIssue 목록으로 생성한다.",
    "src/drivers/alazar/alazar_digitizer.cpp": "ATS-SDK를 통해 보드 식별·clock/input/trigger/DMA를 설정하고, posted buffer ring에서 full-period record batch를 회수한다.",
    "src/drivers/alazar/alazar_sample_conversion.cpp": "12-bit packed/word-aligned ADC code를 channel A의 정규화 float sample로 변환하는 CPU 경로를 제공한다.",
    "src/drivers/mcu/mcu_serial_controller.cpp": "MCU waveform을 chunk 단위로 업로드하고 CRC/상태 응답을 확인하며 scan start/stop과 marker offset 상태를 관리한다.",
    "src/drivers/edfa/edfa_serial_controller.cpp": "EDFA serial command를 전송하고 APC/ACC/AGC 모드, 출력 setpoint, enable 상태를 조회·변경한다. EDFA None/Bypass는 무통신 동작을 허용한다.",
    "src/processing/signal_processor.cpp": "CPU FFT 공통 신호처리 기준 구현. full-period 분할, window/DC 제거, FFT, threshold peak, 3점 포물선 보간, 거리·속도와 XYZIV를 계산한다.",
    "src/processing/cuda/cuda_signal_pipeline.cu": "998-record batch의 ADC 변환·chirp 분할·전처리·cuFFT·peak/거리·XYZIV를 GPU 메모리에서 연속 처리하는 Jetson/Windows CUDA 경로다.",
    "src/processing/processing_service.cpp": "수집 batch 큐를 소비해 CPU 또는 CUDA pipeline을 실행하고 GUI snapshot, point cloud, 저장·UDP·검출 downstream으로 fan-out한다.",
    "src/processing/point_cloud_postprocessor.cpp": "완료된 frame의 point cloud에 표시/저장용 좌표 및 intensity 후처리를 적용하되 측정 파이프라인의 원본 의미를 유지한다.",
    "src/detection/centerpoint_input_builder.cpp": "XYZIV point cloud를 CenterPoint 네트워크 입력 layout과 범위 제한에 맞춰 pack하고 active point count를 만든다.",
    "src/detection/object_detection_service.cpp": "완료 point-cloud frame만 최신 우선 큐로 받아 detector를 비동기 실행하고 frame id가 일치하는 detection snapshot을 게시한다.",
    "src/storage/binary_storage.cpp": "세션 manifest와 raw DMA batch/point-cloud frame을 binary on-disk contract로 직렬화하고 flush/finalize한다.",
    "src/storage/point_cloud_replay.cpp": "내부 pointcloud.bin과 외부 PCD/CSV/BIN(XYZ, XYZI, XYZIV)을 frame 단위로 읽어 공통 PointCloudFrame으로 변환한다.",
    "src/network/udp_point_protocol.cpp": "XYZIV point를 고정 크기 little-endian UDP fragment header/payload로 encode/decode하고 frame 재조립 검증 정보를 제공한다.",
    "src/apps/common/application_controller.cpp": "Qt UI 명령과 core service 생명주기를 연결한다. 설정 적용, 장치 연결, MCU upload, global start/stop, replay, 저장, UDP, detection을 조정한다.",
    "src/apps/common/main_window.cpp": "운영 UI를 구성하고 설정 widget↔SystemConfig 변환, 상태/로그 갱신, Live View 선택 A-scan과 frame-complete B-scan/3D 표시를 담당한다.",
    "src/apps/common/point_cloud_widget.cpp": "OpenGL로 XYZ point와 detection box를 그리며 LiDAR 원점 기준 고정 카메라, 축·격자·color mode를 제공한다.",
    "src/firmware/mcu/FMCW_LiDAR_MCU/Core/Src/uart_cmd.c": "PC 명령 parser와 waveform upload transaction(CLR/BEGIN/DATA/END), CRC, start/stop/status 응답을 처리한다.",
    "src/firmware/mcu/FMCW_LiDAR_MCU/Core/Src/frame_player.c": "업로드된 X/Y/M frame을 TIM6 tick마다 재생하고 DAC 갱신과 B-trigger GPIO timing을 실행한다.",
    "Ros_project/src/fmcw_lidar_rviz/src/udp_pointcloud_receiver_node.cpp": "FMCW UDP fragment를 수신·재조립하여 sensor_msgs/PointCloud2로 publish하고 rviz에서 XYZIV를 표시한다.",
    "tools/convert_waymo_zip.py": "Waymo frame archive에서 여러 LiDAR return을 vehicle frame으로 합치고 frame별 내부 pointcloud.bin과 replay manifest를 생성한다.",
    "deploy/jetson/check_dependencies.sh": "Jetson Qt/CUDA/cuFFT/Alazar 의존성을 실제 경로로 검사한다. pipefail 안전 검사와 symlink CUDA 경로를 지원한다.",
}


STEM_ROLES = {
    "app_version": "애플리케이션 semantic version 구조와 표시 문자열을 제공한다.",
    "config_manager": "active/pending SystemConfig와 revision을 thread-safe하게 보관하고 validated apply를 원자적으로 수행한다.",
    "config_policy": "실행 중 즉시 적용 가능한 설정과 재시작/재업로드가 필요한 설정의 fingerprint·차이를 판정한다.",
    "config_types": "SystemConfig 하위 구조, enum, 기본값과 문자열 변환을 정의한다.",
    "digitizer_capabilities": "지원 12-bit ATS 모델의 sample rate, range, impedance, AUX trigger capability와 sample-point 정렬을 제공한다.",
    "frame_types": "RawFrame/RawFrameBatch, ScanPosition, ProcessedFrame, PointXYZI 등 pipeline 공통 데이터 계약과 zero-copy sample view를 정의한다.",
    "logging": "runtime log level·record와 sink dispatch를 제공한다.",
    "raw_frame_batch_pool": "RawFrameBatch 객체와 내부 vector 용량을 재사용해 DMA hot path의 동적 할당을 줄인다.",
    "realtime_thread": "Windows process/thread priority와 power throttling을 수집·처리 thread에 적용하고 scope 종료 시 복원한다.",
    "system_state": "Disconnected부터 Error까지 허용된 operation state transition과 오류 문자열을 관리한다.",
    "system_telemetry": "digitizer·EDFA·MCU의 동시점 상태를 묶는 acquisition telemetry snapshot을 선언한다.",
    "runtime_adapter_factory": "AcquisitionSource와 platform build capability에 따라 실제/재생/시뮬레이터 장치 adapter 묶음을 생성한다.",
    "serial_transport": "Windows COM과 Linux tty의 open/configure/read line/write/timeout을 동일 인터페이스로 제공한다.",
    "mcu_protocol": "legacy XYM parse, resample, DAC word 변환, marker shift/edge 검증, upload CRC와 command text를 생성한다.",
    "edfa_protocol": "CivilLaser EDFA command/response text를 생성·해석하고 APC/ACC/AGC 값과 telemetry 단위를 변환한다.",
    "replay_digitizer": "raw recording을 IDigitizer waitForBatch 계약으로 pacing하여 hardware와 동일한 processing 경로에 주입한다.",
    "fake_digitizer": "설정된 record geometry와 200 Hz DMA cadence로 신호·random noise를 생성하고 overflow/telemetry를 모사한다.",
    "fake_edfa": "EDFA None/Manual/Controlled 상태와 output enable을 하드웨어 없이 모사한다.",
    "fake_mcu": "waveform loaded/start/stop/status와 upload progress를 하드웨어 없이 모사한다.",
    "fft_backend": "CPU/GPU FFT backend가 구현해야 할 configure와 batch transform 계약을 선언한다.",
    "fft_backends": "FftBackendKind와 build capability에 맞는 FFTW/cuFFT backend를 생성하고 backend availability를 보고한다.",
    "fftw_backend": "FFTW plan-many와 재사용 buffer로 UP/DOWN batch FFT를 CPU에서 수행한다.",
    "cuda_fft_backend": "cuFFT plan-many와 CUDA stream/device buffer를 사용해 FFT backend 계약을 구현한다.",
    "cuda_signal_pipeline": "ADC code부터 XYZIV까지 batch 전체를 GPU에서 처리하고 최소 host snapshot만 복사한다.",
    "cuda_signal_pipeline_stub": "CUDA 미포함 빌드에서 backend unavailable 오류를 일관되게 반환하는 stub이다.",
    "processing_snapshots": "선택 A-scan waveform/FFT, scan line, frame-complete B-scan/point cloud snapshot을 조립하고 latest snapshot을 보관한다.",
    "object_detection_policy": "CenterPoint 입력 범위, feature layout, queue/latest-frame와 detector enable 조건을 검증한다.",
    "object_detection_service": "point-cloud frame과 detector 실행을 별도 worker thread로 분리하고 최신 frame 교체·telemetry를 관리한다.",
    "object_detector": "detector backend interface와 factory/availability 정보를 제공한다.",
    "centerpoint_detector": "외부 CUDA/cuDNN CenterPoint runtime을 초기화하고 packed point에서 3D box를 추론한다.",
    "centerpoint_detector_stub": "CenterPoint 미포함 빌드에서 명시적 unavailable 상태를 제공한다.",
    "point_cloud_file_loader": "PCD/BIN/CSV의 XYZ, XYZI, XYZIV point를 검증해 detection 또는 GUI용 frame으로 읽는다.",
    "async_storage_service": "raw batch와 완료 point-cloud frame을 분리된 bounded queue/writer thread로 저장하고 throughput·high-water를 측정한다.",
    "writer_interfaces": "raw와 point-cloud writer가 구현할 open/write/finalize 및 telemetry 계약을 선언한다.",
    "udp_sender_service": "완료 PointCloudFrame을 bounded queue로 받아 fragment packet으로 비동기 송신하고 backpressure 정책을 적용한다.",
    "plot_widgets": "Qt painter 기반 line/FFT/segmentation/heatmap 위젯과 decimation·axis tick·framebuffer 저장을 구현한다.",
    "replay_setup_loader": "raw recording 옆 setup/manifest를 찾아 저장 당시 SystemConfig를 replay profile로 복원한다.",
    "main_window": "Qt Widgets 운영 화면과 설정 binding, 상태 표시, plot/replay 상호작용을 구현한다.",
    "ad5664": "AD5664 DAC command word와 SPI write/update 제어를 제공한다.",
    "mirrorcle_drv": "MEMS mirror DAC 채널 초기화와 X/Y command 출력을 보조한다.",
    "spi24_tx": "24-bit SPI frame의 blocking/비동기 전송과 chip-select timing을 제공한다.",
    "main": "프로세스 또는 MCU의 진입점, 플랫폼 초기화, event/main loop를 구성한다.",
    "stm32h7xx_it": "Cortex exception과 TIM/UART 등 peripheral interrupt를 HAL 및 frame/UART callback으로 전달한다.",
    "stm32h7xx_hal_msp": "CubeMX가 생성한 clock/GPIO/DMA/NVIC low-level peripheral 초기화를 수행한다.",
    "system_stm32h7xx": "MCU system clock 변수와 reset 이후 clock update를 제공하는 ST 생성 코드다.",
    "syscalls": "newlib 표준 I/O와 process syscall을 embedded 환경에 연결하는 생성 코드다.",
    "sysmem": "embedded heap 증가 경계를 linker symbol과 RAM 범위로 제한한다.",
    "udp_pointcloud_receiver_node": "UDP XYZIV frame을 재조립해 ROS PointCloud2로 publish한다.",
    "udp_test_sender_node": "ROS 수신/rviz 경로 검증용 합성 XYZIV UDP frame을 전송한다.",
    "export_waymo_frame": "Waymo 단일 frame의 LiDAR range image를 Cartesian point로 추출하는 도구다.",
    "build": "플랫폼 build preset, 의존성 경로와 package 단계를 순서대로 실행한다.",
    "package": "실행 파일과 필요한 runtime library/config만 배포 디렉터리에 수집한다.",
    "export_source": "Jetson에서 재현 빌드할 최소 source tree를 export한다.",
    "run": "배포 package의 library path와 실행 환경을 설정한 뒤 GUI binary를 시작한다.",
}


ROLE_RULES = [
    ("src/core/config_", "구성 모델의 변환·정책·검증을 담당하는 core 계층 파일이다."),
    ("src/core/", "플랫폼 독립 상태, 데이터 계약, 동시성 또는 런타임 orchestration을 제공하는 core 파일이다."),
    ("src/drivers/alazar/", "AlazarTech ATS-SDK 하드웨어 adapter와 ADC 변환 관련 파일이다."),
    ("src/drivers/mcu/", "MCU serial protocol 및 controller adapter 파일이다."),
    ("src/drivers/edfa/", "EDFA serial protocol 및 optional controller adapter 파일이다."),
    ("src/drivers/serial/", "Windows COM/Linux tty를 공통화하는 serial transport 파일이다."),
    ("src/drivers/replay/", "저장된 raw DMA를 IDigitizer 계약으로 재생하는 adapter 파일이다."),
    ("src/drivers/simulator/", "하드웨어 없이 결정적/잡음 포함 데이터를 생성하는 simulator adapter 파일이다."),
    ("src/drivers/", "런타임 장치 인터페이스의 구체 adapter 또는 factory 파일이다."),
    ("src/processing/cuda/", "CUDA/cuFFT batch 처리 구현 또는 module policy 파일이다."),
    ("src/processing/cpu/", "FFTW 기반 CPU batch FFT backend 파일이다."),
    ("src/processing/", "공통 신호처리, snapshot 또는 point-cloud 후처리 파일이다."),
    ("src/detection/", "point-cloud 객체 검출 정책, 입력 변환, backend 또는 service 파일이다."),
    ("src/storage/", "raw/processed 데이터의 비동기 저장과 재생 형식 파일이다."),
    ("src/network/", "UDP point-cloud packet 규격과 sender service 파일이다."),
    ("src/apps/common/", "Windows/Jetson 공용 Qt application 또는 widget 파일이다."),
    ("src/apps/windows/", "Windows GUI 실행 진입점이다."),
    ("src/apps/jetson/", "Jetson GUI 실행 진입점이다."),
    ("src/firmware/mcu/", "STM32H750 MCU 펌웨어 또는 CubeMX 생성 파일이다."),
    ("Ros_project/", "ROS Noetic/rviz 연동 package의 파일이다."),
    ("config/", "Windows/Jetson runtime, profile, calibration 또는 scanner waveform의 기본 적용값을 선언한다."),
    ("tools/", "오프라인 변환·검증 도구 파일이다."),
    ("deploy/windows/", "Windows 빌드 또는 패키징 자동화 파일이다."),
    ("deploy/jetson/", "Jetson 의존성 검사·빌드·패키징·실행 자동화 파일이다."),
    ("tests/", "계약, 회귀, 성능 또는 하드웨어 adapter를 검증하는 테스트 파일이다."),
]


FUNCTION_NOTES = {
    "AcquisitionSession::arm": "MCU trigger가 나오기 전에 digitizer DMA를 먼저 arm한다. Controlled EDFA이면 모드·setpoint·출력을 순서대로 적용하고 warm-up 후 digitizer start를 호출한다.",
    "AcquisitionSession::enableTrigger": "arm 완료 뒤 MCU scan을 시작해 trigger source를 활성화한다. MCU 시작 실패 시 digitizer abort/stop과 EDFA off로 롤백한다.",
    "AcquisitionSession::waitForBatch": "IDigitizer에서 한 DMA buffer의 record 전체를 받고 metadata contract를 검증한다. 각 record에 동일 config revision과 optical state를 찍고 MCU/replay/generated raster 좌표를 매핑한 뒤 immutable batch로 넘긴다.",
    "prepareTrajectoryLine": "line_sequence를 marker index와 y index로 바꾸고 bidirectional 홀수 line의 ScanDirection을 결정한다. 파형 첫/끝 command 변화량으로 fast axis도 판정한다.",
    "mapTrajectoryRecord": "marker부터 record trigger 간격만큼 waveform index를 진행해 X/Y command를 얻는다. 홀수 line이면 x_index만 역방향으로 정렬하며 raw A-scan sample 순서는 뒤집지 않는다.",
    "SignalProcessor::processBatch": "모든 record에 동일 전처리·FFT·peak·거리/속도 계산을 적용하고 완료 line/frame snapshot을 만든다. threshold 미달 peak는 NaN 측정으로 유지한다.",
    "PointCloudReplayReader::readNextFrame": "선택된 형식 parser에서 한 frame만 읽고 XYZ/XYZI/XYZIV 누락 필드를 기본값으로 보완한다. EOF/loop 정책에 따라 다음 파일 또는 처음으로 이동한다.",
    "startRuntime": "설정 revision을 고정하고 storage/UDP/processing을 준비한 뒤 acquisition을 arm하고 마지막에 trigger를 켠다. 부분 실패는 공통 stop 경로로 정리한다.",
    "stopRuntime": "trigger 제거와 digitizer abort를 먼저 수행하고 worker, processing, UDP, storage 순으로 종료하여 producer가 살아 있는 상태에서 consumer를 없애지 않는다.",
    "PointCloudWidget::paintGL": "LiDAR 원점 기준 view/projection을 적용해 point VBO, grid/axes, detection box를 그린다. frame bounds에 따라 카메라 중심이나 배율을 자동 재설정하지 않는다.",
    "FramePlayer_OnTimTick": "TIM6 주기마다 현재 X/Y 값을 DAC 전송 경로에 넣고 M marker의 보정된 edge를 B-trigger GPIO로 출력한 뒤 ring index를 증가시킨다.",
}


CONTROL_WORDS = {
    "if", "for", "while", "switch", "catch", "return", "sizeof", "alignof",
    "decltype", "static_cast", "reinterpret_cast", "const_cast", "dynamic_cast",
    "new", "delete", "case", "do", "else", "requires", "co_await", "co_return",
}


def rel(path: Path) -> str:
    return path.resolve().relative_to(ROOT.resolve()).as_posix()


def normalize_space(text: str) -> str:
    return re.sub(r"\s+", " ", text).strip()


def strip_comments_and_literals(source: str) -> str:
    """Replace comments and string/char contents while preserving line positions."""
    out = list(source)
    i = 0
    state = "code"
    quote = ""
    while i < len(source):
        ch = source[i]
        nxt = source[i + 1] if i + 1 < len(source) else ""
        if state == "code":
            if ch == "/" and nxt == "/":
                out[i] = out[i + 1] = " "
                i += 2
                state = "line_comment"
                continue
            if ch == "/" and nxt == "*":
                out[i] = out[i + 1] = " "
                i += 2
                state = "block_comment"
                continue
            if ch in {'"', "'"}:
                quote = ch
                out[i] = " "
                i += 1
                state = "literal"
                continue
            i += 1
            continue
        if state == "line_comment":
            if ch == "\n":
                state = "code"
            else:
                out[i] = " "
            i += 1
            continue
        if state == "block_comment":
            if ch == "*" and nxt == "/":
                out[i] = out[i + 1] = " "
                i += 2
                state = "code"
            else:
                if ch != "\n":
                    out[i] = " "
                i += 1
            continue
        if state == "literal":
            if ch == "\\":
                out[i] = " "
                if i + 1 < len(source):
                    if source[i + 1] != "\n":
                        out[i + 1] = " "
                    i += 2
                else:
                    i += 1
                continue
            if ch == quote:
                out[i] = " "
                state = "code"
            elif ch != "\n":
                out[i] = " "
            i += 1
    return "".join(out)


def matching_open_paren(text: str, close_index: int) -> int | None:
    depth = 0
    for index in range(close_index, -1, -1):
        if text[index] == ")":
            depth += 1
        elif text[index] == "(":
            depth -= 1
            if depth == 0:
                return index
    return None


def split_parameters(text: str) -> list[str]:
    values: list[str] = []
    start = 0
    depth = 0
    for index, ch in enumerate(text):
        if ch in "(<[{":
            depth += 1
        elif ch in ")>]}":
            depth = max(0, depth - 1)
        elif ch == "," and depth == 0:
            values.append(text[start:index].strip())
            start = index + 1
    tail = text[start:].strip()
    if tail:
        values.append(tail)
    return [value for value in values if value and value != "void"]


def parse_parameter(declaration: str) -> ParameterInfo:
    without_default = re.split(r"=(?!=)", declaration, maxsplit=1)[0].strip()
    without_array = re.sub(r"\[[^\]]*\]\s*$", "", without_default)
    match = re.search(r"([A-Za-z_]\w*)\s*$", without_array)
    name = match.group(1) if match else ""
    type_text = without_array[: match.start()].strip() if match else without_array
    direction = "입력"
    lowered = name.lower()
    if name and ("&" in type_text or "*" in type_text) and "const" not in type_text:
        direction = "입출력"
    if lowered in {"error", "message", "output", "result", "context", "snapshot"} and "const" not in type_text:
        direction = "출력"
    return ParameterInfo(declaration=normalize_space(declaration), name=name,
                         type_text=normalize_space(type_text), direction=direction)


def extract_calls(body: str) -> list[str]:
    calls: list[str] = []
    pattern = re.compile(r"(?<![\w])((?:[A-Za-z_]\w*(?:::|\.|->))*[A-Za-z_~]\w*)\s*\(")
    for match in pattern.finditer(body):
        name = match.group(1).replace("->", ".")
        bare = re.split(r"::|\.", name)[-1]
        if bare in CONTROL_WORDS or bare.startswith("operator"):
            continue
        if name not in calls:
            calls.append(name)
    return calls


def extract_assignments(body: str) -> list[str]:
    assignments: list[str] = []
    for match in re.finditer(r"\b((?:this->)?[A-Za-z_]\w*(?:\.[A-Za-z_]\w*)?)\s*(?:=|\+=|-=|\*=|/=)", body):
        name = match.group(1)
        if name not in assignments:
            assignments.append(name)
    return assignments[:12]


def detect_traits(body: str) -> list[str]:
    traits: list[str] = []
    checks = [
        (r"\bfor\s*\(|\bwhile\s*\(", "반복 처리"),
        (r"std::mutex|lock_guard|unique_lock|QMutex", "mutex 동기화"),
        (r"std::atomic|\.load\(\)|\.store\(", "atomic 상태"),
        (r"std::thread|QThread|cudaStream", "비동기/스레드"),
        (r"cuda|cufft|CUFFT", "CUDA/cuFFT"),
        (r"fftw", "FFTW"),
        (r"sendto|recvfrom|QUdp|socket\(", "네트워크 I/O"),
        (r"serial|UART|HAL_UART|ReadFile|WriteFile", "직렬 I/O"),
        (r"fstream|ifstream|ofstream|fopen|fwrite|fread", "파일 I/O"),
        (r"return\s+false|throw\s|error\s*=|fail\(", "명시적 오류 경로"),
        (r"emit\s+|QMetaObject|update\(\)|repaint\(", "Qt 이벤트/갱신"),
    ]
    for pattern, label in checks:
        if re.search(pattern, body, re.IGNORECASE):
            traits.append(label)
    return traits


def signature_start(cleaned: str, brace_index: int) -> int:
    index = brace_index - 1
    paren = 0
    angle = 0
    while index >= 0:
        ch = cleaned[index]
        if ch == ")":
            paren += 1
        elif ch == "(":
            paren = max(0, paren - 1)
        elif ch == ">":
            angle += 1
        elif ch == "<":
            angle = max(0, angle - 1)
        if paren == 0 and angle == 0 and ch in ";{}":
            return index + 1
        if paren == 0 and ch == "\n":
            line = cleaned[index + 1:brace_index].lstrip()
            if line.startswith("#"):
                return index + 1
        index -= 1
    return 0


def sanitize_signature(text: str) -> str:
    text = re.sub(
        r"(?m)^\s*(?:Q_OBJECT|Q_GADGET|public\s*:|private\s*:|protected\s*:|"
        r"public\s+slots\s*:|private\s+slots\s*:|protected\s+slots\s*:|signals\s*:)\s*$",
        "", text)
    text = re.sub(r"^\s*(?:public|private|protected)(?:\s+slots)?\s*:\s*", "", text)
    return normalize_space(text)


def find_matching_brace(cleaned: str, open_index: int) -> int:
    depth = 0
    for index in range(open_index, len(cleaned)):
        if cleaned[index] == "{":
            depth += 1
        elif cleaned[index] == "}":
            depth -= 1
            if depth == 0:
                return index
    return len(cleaned) - 1


def first_function_declarator(segment: str) -> tuple[str, int, int] | None:
    """Return the first plausible function name and parameter span in a signature."""
    pattern = re.compile(r"((?:[A-Za-z_]\w*::)*~?[A-Za-z_]\w*|operator\s*[^\s(]+)\s*\(")
    for match in pattern.finditer(segment):
        qualified_name = normalize_space(match.group(1))
        bare_name = qualified_name.split("::")[-1]
        if bare_name in CONTROL_WORDS or qualified_name in {"noexcept", "decltype", "sizeof"}:
            continue
        open_paren = segment.find("(", match.start(), match.end() + 1)
        depth = 0
        close_paren = None
        for index in range(open_paren, len(segment)):
            if segment[index] == "(":
                depth += 1
            elif segment[index] == ")":
                depth -= 1
                if depth == 0:
                    close_paren = index
                    break
        if close_paren is None:
            continue
        prefix = segment[:match.start()].strip()
        if prefix.endswith((".", "->")):
            continue
        return qualified_name, open_paren, close_paren
    return None


def parse_c_like(path: Path, language: str) -> list[FunctionInfo]:
    source = path.read_text(encoding="utf-8", errors="replace")
    cleaned = strip_comments_and_literals(source)
    functions: list[FunctionInfo] = []
    seen_spans: list[tuple[int, int]] = []
    for brace_match in re.finditer(r"\{", cleaned):
        brace = brace_match.start()
        if any(start < brace < end for start, end in seen_spans):
            continue
        start = signature_start(cleaned, brace)
        raw_segment = cleaned[start:brace]
        leading_space = len(raw_segment) - len(raw_segment.lstrip())
        signature_segment = raw_segment.strip()
        declarator = first_function_declarator(signature_segment)
        if declarator is None:
            continue
        qualified_name, relative_open, relative_close = declarator
        open_paren = start + leading_space + relative_open
        absolute_close = start + leading_space + relative_close
        bare_name = qualified_name.split("::")[-1]
        raw_signature = cleaned[start:brace].strip()
        signature = sanitize_signature(raw_signature)
        if not signature or signature.startswith(("class ", "struct ", "enum ", "namespace ", "union ")):
            continue
        if "=" in signature and not signature.lstrip().startswith("operator"):
            head = signature.split("(", 1)[0]
            if "=" in head:
                continue
        end = find_matching_brace(cleaned, brace)
        seen_spans.append((start, end))
        body = source[brace + 1:end]
        name_start_in_sig = signature.rfind(qualified_name)
        return_type = normalize_space(signature[:name_start_in_sig]) if name_start_in_sig >= 0 else ""
        if "::" in qualified_name and not return_type:
            return_type = "(constructor/destructor)"
        params_text = source[open_paren + 1:absolute_close]
        params = [parse_parameter(value) for value in split_parameters(params_text)]
        line = source.count("\n", 0, start) + 1
        functions.append(FunctionInfo(
            file=rel(path), line=line, name=bare_name, qualified_name=qualified_name,
            signature=signature, return_type=return_type or "(constructor/destructor)",
            parameters=params, calls=extract_calls(body), assignments=extract_assignments(body),
            traits=detect_traits(body), body=body, language=language,
        ))
    if path.suffix.lower() in {".h", ".hpp", ".cuh"}:
        functions.extend(parse_c_like_declarations(path, language, cleaned, seen_spans, functions))
    return functions


def parse_c_like_declarations(path: Path, language: str, cleaned: str,
                              definition_spans: Sequence[tuple[int, int]],
                              definitions: Sequence[FunctionInfo]) -> list[FunctionInfo]:
    masked = list(cleaned)
    for start, end in definition_spans:
        for index in range(start, min(end + 1, len(masked))):
            if masked[index] != "\n":
                masked[index] = " "
    text = "".join(masked)
    declarations: list[FunctionInfo] = []
    definition_keys = {(fn.name, normalize_space(re.sub(r"\s*=\s*(?:0|default|delete)\s*$", "", fn.signature))) for fn in definitions}
    for match in re.finditer(r";", text):
        end = match.start()
        start = statement_start_before(text, end)
        statement = text[start:end].strip()
        if not statement or "(" not in statement or statement.startswith((
            "using ", "typedef ", "static_assert", "return ", "#", "enum ", "class ", "struct ")):
            continue
        declarator = first_function_declarator(statement)
        if declarator is None:
            continue
        qualified_name, open_paren, close_paren = declarator
        bare_name = qualified_name.split("::")[-1]
        suffix = normalize_space(statement[close_paren + 1:])
        if suffix and not re.fullmatch(
            r"(?:const\s*)?(?:noexcept(?:\s*\([^)]*\))?\s*)?(?:override\s*)?(?:final\s*)?"
            r"(?:=\s*(?:0|default|delete)\s*)?", suffix):
            continue
        signature = sanitize_signature(statement)
        if (bare_name, normalize_space(re.sub(r"\s*=\s*(?:0|default|delete)\s*$", "", signature))) in definition_keys:
            continue
        name_start = signature.find(qualified_name)
        return_type = normalize_space(signature[:name_start]) if name_start >= 0 else ""
        params = [parse_parameter(value) for value in split_parameters(statement[open_paren + 1:close_paren])]
        name_offset = start + max(0, statement.find(qualified_name))
        declarations.append(FunctionInfo(
            file=rel(path), line=cleaned.count("\n", 0, name_offset) + 1,
            name=bare_name, qualified_name=qualified_name, signature=signature,
            return_type=return_type or "(constructor/destructor)", parameters=params,
            language=language, declaration_only=True,
        ))
    return declarations


def statement_start_before(text: str, end: int) -> int:
    paren_depth = 0
    bracket_depth = 0
    brace_depth = 0
    for index in range(end - 1, -1, -1):
        ch = text[index]
        if ch == ")":
            paren_depth += 1
        elif ch == "(":
            paren_depth = max(0, paren_depth - 1)
        elif ch == "]":
            bracket_depth += 1
        elif ch == "[":
            bracket_depth = max(0, bracket_depth - 1)
        elif ch == "}":
            brace_depth += 1
        elif ch == "{":
            if brace_depth > 0:
                brace_depth -= 1
            elif paren_depth == 0 and bracket_depth == 0:
                return index + 1
        elif ch == ";" and paren_depth == 0 and bracket_depth == 0 and brace_depth == 0:
            return index + 1
    return 0


def parse_python(path: Path) -> list[FunctionInfo]:
    source = path.read_text(encoding="utf-8", errors="replace")
    try:
        tree = ast.parse(source)
    except SyntaxError:
        return []
    functions: list[FunctionInfo] = []
    parents: list[str] = []

    class Visitor(ast.NodeVisitor):
        def visit_ClassDef(self, node: ast.ClassDef) -> None:
            parents.append(node.name)
            self.generic_visit(node)
            parents.pop()

        def _visit_function(self, node: ast.FunctionDef | ast.AsyncFunctionDef) -> None:
            qname = "::".join([*parents, node.name])
            segment = ast.get_source_segment(source, node) or ""
            header = segment.split(":", 1)[0] + ":" if segment else node.name
            parameters: list[ParameterInfo] = []
            all_args = [*node.args.posonlyargs, *node.args.args, *node.args.kwonlyargs]
            for arg in all_args:
                annotation = ast.unparse(arg.annotation) if arg.annotation is not None else "Any"
                parameters.append(ParameterInfo(f"{arg.arg}: {annotation}", arg.arg, annotation, "입력"))
            if node.args.vararg:
                parameters.append(ParameterInfo(f"*{node.args.vararg.arg}", node.args.vararg.arg, "tuple", "입력"))
            if node.args.kwarg:
                parameters.append(ParameterInfo(f"**{node.args.kwarg.arg}", node.args.kwarg.arg, "dict", "입력"))
            calls: list[str] = []
            assignments: list[str] = []
            for child in ast.walk(node):
                if isinstance(child, ast.Call):
                    try:
                        name = ast.unparse(child.func)
                    except Exception:
                        continue
                    if name not in calls:
                        calls.append(name)
                elif isinstance(child, (ast.Assign, ast.AnnAssign, ast.AugAssign)):
                    targets = child.targets if isinstance(child, ast.Assign) else [child.target]
                    for target in targets:
                        try:
                            name = ast.unparse(target)
                        except Exception:
                            continue
                        if name not in assignments:
                            assignments.append(name)
            return_type = ast.unparse(node.returns) if node.returns is not None else "Any/None"
            traits = []
            if any(isinstance(child, (ast.For, ast.While, ast.comprehension)) for child in ast.walk(node)):
                traits.append("반복 처리")
            if any(isinstance(child, (ast.Raise, ast.Try)) for child in ast.walk(node)):
                traits.append("예외 처리")
            if any(isinstance(child, (ast.With, ast.AsyncWith)) for child in ast.walk(node)):
                traits.append("context manager/자원 관리")
            functions.append(FunctionInfo(
                file=rel(path), line=node.lineno, name=node.name, qualified_name=qname,
                signature=normalize_space(header), return_type=return_type,
                parameters=parameters, calls=calls, assignments=assignments[:12], traits=traits,
                body=segment, language="Python",
            ))
            parents.append(node.name)
            self.generic_visit(node)
            parents.pop()

        visit_FunctionDef = _visit_function
        visit_AsyncFunctionDef = _visit_function

    Visitor().visit(tree)
    return functions


def parse_shell(path: Path, language: str) -> list[FunctionInfo]:
    source = path.read_text(encoding="utf-8", errors="replace")
    functions: list[FunctionInfo] = []
    if language == "PowerShell":
        pattern = re.compile(r"(?im)^\s*function\s+([A-Za-z_][\w-]*)\s*(?:\([^)]*\))?\s*\{")
    else:
        pattern = re.compile(r"(?m)^\s*(?:function\s+)?([A-Za-z_]\w*)\s*\(\s*\)\s*\{")
    cleaned = strip_comments_and_literals(source)
    for match in pattern.finditer(cleaned):
        brace = cleaned.find("{", match.start(), match.end() + 1)
        end = find_matching_brace(cleaned, brace)
        body = source[brace + 1:end]
        name = match.group(1)
        functions.append(FunctionInfo(
            file=rel(path), line=source.count("\n", 0, match.start()) + 1,
            name=name, qualified_name=name, signature=normalize_space(source[match.start():brace]),
            return_type="process exit status / stdout", parameters=[], calls=extract_calls(body),
            assignments=extract_assignments(body), traits=detect_traits(body), body=body,
            language=language,
        ))
    return functions


def language_for(path: Path) -> str:
    suffix = path.suffix.lower()
    return {
        ".cpp": "C++", ".cc": "C++", ".cxx": "C++", ".h": "C/C++ Header",
        ".hpp": "C++ Header", ".cu": "CUDA C++", ".cuh": "CUDA Header",
        ".c": "C", ".py": "Python", ".sh": "Shell", ".ps1": "PowerShell",
        ".cmake": "CMake", ".json": "JSON", ".xml": "XML", ".ioc": "STM32CubeMX",
        ".ld": "Linker Script", ".s": "Assembly", ".yaml": "YAML", ".yml": "YAML",
        ".launch": "ROS Launch XML", ".rviz": "RViz Config", ".env": "Environment Config",
    }.get(suffix, "Text")


def role_for(path_text: str) -> str:
    if path_text in SPECIAL_FILE_ROLES:
        return SPECIAL_FILE_ROLES[path_text]
    path = Path(path_text)
    if path.suffix.lower() in {".h", ".hpp", ".cuh"}:
        cpp_key = path.with_suffix(".cpp").as_posix()
        cu_key = path.with_suffix(".cu").as_posix()
        if cpp_key in SPECIAL_FILE_ROLES or cu_key in SPECIAL_FILE_ROLES:
            implementation_role = SPECIAL_FILE_ROLES.get(cpp_key, SPECIAL_FILE_ROLES.get(cu_key, ""))
            return "연결 구현의 공개 타입·함수 계약을 선언한다. 구현 역할: " + implementation_role
    if path.stem in STEM_ROLES:
        role = STEM_ROLES[path.stem]
        if path.suffix.lower() in {".h", ".hpp", ".cuh"}:
            return role + " 이 파일은 공개 타입·함수 계약을 선언한다."
        return role
    for prefix, role in ROLE_RULES:
        if path_text.startswith(prefix):
            stem = Path(path_text).stem
            if path_text.endswith(('.h', '.hpp')):
                return role + f" `{stem}`의 공개 타입·함수 계약을 선언한다."
            return role
    if path_text.endswith("CMakeLists.txt") or path_text.endswith("CMakePresets.json"):
        return "빌드 target, feature option, 의존성 및 플랫폼별 source 조합을 정의한다."
    return "프로젝트 실행 또는 검증을 보조하는 소스/설정 파일이다."


def declarations_for(path: Path, language: str) -> list[str]:
    if language not in {"C++", "C", "C/C++ Header", "C++ Header", "CUDA C++", "CUDA Header"}:
        return []
    source = strip_comments_and_literals(path.read_text(encoding="utf-8", errors="replace"))
    declarations: list[str] = []
    for match in re.finditer(r"(?m)^\s*(?:enum\s+class|enum|class|struct)\s+([A-Za-z_]\w*)", source):
        value = normalize_space(match.group(0))
        if value not in declarations:
            declarations.append(value)
    return declarations[:40]


def includes_for(path: Path, language: str) -> list[str]:
    source = path.read_text(encoding="utf-8", errors="replace")
    if language in {"C++", "C", "C/C++ Header", "C++ Header", "CUDA C++", "CUDA Header"}:
        return re.findall(r"(?m)^\s*#include\s+[<\"]([^>\"]+)[>\"]", source)
    if language == "Python":
        values = []
        for line in source.splitlines():
            match = re.match(r"\s*(?:from\s+([\w.]+)\s+import|import\s+([\w.]+))", line)
            if match:
                values.append(match.group(1) or match.group(2))
        return values
    return []


def discover_files() -> list[Path]:
    files: list[Path] = []
    patterns = [
        "src/core/*", "src/drivers/**/*", "src/processing/**/*", "src/detection/**/*",
        "src/storage/**/*", "src/network/**/*", "src/apps/**/*",
        "src/firmware/mcu/FMCW_LiDAR_MCU/Core/Inc/*",
        "src/firmware/mcu/FMCW_LiDAR_MCU/Core/Src/*",
        "src/firmware/mcu/FMCW_LiDAR_MCU/Core/Startup/*",
        "src/firmware/mcu/FMCW_LiDAR_MCU/*.ioc",
        "src/firmware/mcu/FMCW_LiDAR_MCU/*.ld",
        "Ros_project/src/fmcw_lidar_rviz/include/**/*",
        "Ros_project/src/fmcw_lidar_rviz/src/**/*",
        "Ros_project/src/fmcw_lidar_rviz/test/**/*",
        "Ros_project/src/fmcw_lidar_rviz/launch/*",
        "Ros_project/src/fmcw_lidar_rviz/rviz/*",
        "Ros_project/src/fmcw_lidar_rviz/package.xml",
        "config/**/*.yaml", "config/**/*.yml",
        "tools/*", "deploy/windows/*", "deploy/jetson/*", "tests/*",
    ]
    allowed = {
        ".h", ".hpp", ".cpp", ".c", ".cu", ".cuh", ".py", ".sh", ".ps1",
        ".cmake", ".json", ".xml", ".ioc", ".ld", ".s", ".yaml", ".yml",
        ".launch", ".rviz", ".env",
    }
    for pattern in patterns:
        for path in ROOT.glob(pattern):
            if path.is_file() and path.suffix.lower() in allowed and path.name != ".gitkeep":
                files.append(path)
    for path in [ROOT / "CMakeLists.txt", ROOT / "CMakePresets.json",
                 ROOT / "src" / "CMakeLists.txt", ROOT / "tests" / "CMakeLists.txt",
                 ROOT / "Ros_project" / "src" / "CMakeLists.txt",
                 ROOT / "Ros_project" / "src" / "fmcw_lidar_rviz" / "CMakeLists.txt"]:
        if path.exists():
            files.append(path)
    unique = {path.resolve(): path for path in files}
    return sorted(unique.values(), key=lambda path: rel(path).lower())


def category_for(path_text: str) -> str:
    if path_text.startswith("src/core/"):
        return "core"
    if path_text.startswith("src/drivers/"):
        return "drivers"
    if path_text.startswith(("src/processing/", "src/detection/")):
        return "processing_detection"
    if path_text.startswith(("src/storage/", "src/network/")):
        return "storage_network"
    if path_text.startswith("src/apps/"):
        return "gui"
    if path_text.startswith("src/firmware/mcu/"):
        return "mcu"
    return "support"


def analyze_repository() -> list[FileInfo]:
    infos: list[FileInfo] = []
    for path in discover_files():
        language = language_for(path)
        if language in {"C++", "C", "C/C++ Header", "C++ Header", "CUDA C++", "CUDA Header"}:
            functions = parse_c_like(path, language)
        elif language == "Python":
            functions = parse_python(path)
        elif language in {"Shell", "PowerShell"}:
            functions = parse_shell(path, language)
        else:
            functions = []
        path_text = rel(path)
        infos.append(FileInfo(
            path=path_text, language=language, role=role_for(path_text), functions=functions,
            declarations=declarations_for(path, language), includes=includes_for(path, language),
            lines=len(path.read_text(encoding="utf-8", errors="replace").splitlines()),
            category=category_for(path_text),
        ))
    resolve_call_graph(infos)
    return infos


def resolve_call_graph(files: Sequence[FileInfo]) -> None:
    functions = [function for file in files for function in file.functions]
    by_bare: dict[str, list[FunctionInfo]] = defaultdict(list)
    by_qualified: dict[str, list[FunctionInfo]] = defaultdict(list)
    for function in functions:
        if function.declaration_only:
            continue
        by_bare[function.name].append(function)
        by_qualified[function.qualified_name].append(function)
    for caller in functions:
        for call in caller.calls:
            bare = re.split(r"::|\.", call)[-1]
            candidates = by_qualified.get(call, []) or by_bare.get(bare, [])
            if len(candidates) == 1:
                target = candidates[0]
                label = f"{caller.qualified_name} ({caller.file}:{caller.line})"
                if label not in target.callers:
                    target.callers.append(label)


def save_analysis(files: Sequence[FileInfo]) -> None:
    ANALYSIS_DIR.mkdir(parents=True, exist_ok=True)
    serializable = []
    for file in files:
        serializable.append({
            "path": file.path, "language": file.language, "role": file.role,
            "category": file.category, "lines": file.lines,
            "declarations": file.declarations, "includes": file.includes,
            "functions": [{
                "name": fn.name, "qualified_name": fn.qualified_name, "line": fn.line,
                "file": fn.file, "declaration_only": fn.declaration_only,
                "signature": fn.signature, "return_type": fn.return_type,
                "parameters": [param.__dict__ for param in fn.parameters],
                "calls": fn.calls, "callers": fn.callers, "assignments": fn.assignments,
                "traits": fn.traits,
            } for fn in file.functions],
        })
    (ANALYSIS_DIR / "source_inventory.json").write_text(
        json.dumps(serializable, ensure_ascii=False, indent=2), encoding="utf-8")


def set_cell_shading(cell, fill: str) -> None:
    tc_pr = cell._tc.get_or_add_tcPr()
    shd = tc_pr.find(qn("w:shd"))
    if shd is None:
        shd = OxmlElement("w:shd")
        tc_pr.append(shd)
    shd.set(qn("w:fill"), fill)


def set_cell_width(cell, width_dxa: int) -> None:
    tc_pr = cell._tc.get_or_add_tcPr()
    tc_w = tc_pr.find(qn("w:tcW"))
    if tc_w is None:
        tc_w = OxmlElement("w:tcW")
        tc_pr.append(tc_w)
    tc_w.set(qn("w:w"), str(width_dxa))
    tc_w.set(qn("w:type"), "dxa")


def set_cell_margins(cell, top=80, start=120, bottom=80, end=120) -> None:
    tc = cell._tc
    tc_pr = tc.get_or_add_tcPr()
    tc_mar = tc_pr.first_child_found_in("w:tcMar")
    if tc_mar is None:
        tc_mar = OxmlElement("w:tcMar")
        tc_pr.append(tc_mar)
    for tag, value in (("top", top), ("start", start), ("bottom", bottom), ("end", end)):
        node = tc_mar.find(qn(f"w:{tag}"))
        if node is None:
            node = OxmlElement(f"w:{tag}")
            tc_mar.append(node)
        node.set(qn("w:w"), str(value))
        node.set(qn("w:type"), "dxa")


def set_table_geometry(table, widths: Sequence[int], indent_dxa: int = TABLE_INDENT_DXA) -> None:
    table.alignment = WD_TABLE_ALIGNMENT.LEFT
    table.autofit = False
    tbl_pr = table._tbl.tblPr
    tbl_w = tbl_pr.find(qn("w:tblW"))
    if tbl_w is None:
        tbl_w = OxmlElement("w:tblW")
        tbl_pr.append(tbl_w)
    tbl_w.set(qn("w:w"), str(sum(widths)))
    tbl_w.set(qn("w:type"), "dxa")
    tbl_ind = tbl_pr.find(qn("w:tblInd"))
    if tbl_ind is None:
        tbl_ind = OxmlElement("w:tblInd")
        tbl_pr.append(tbl_ind)
    tbl_ind.set(qn("w:w"), str(indent_dxa))
    tbl_ind.set(qn("w:type"), "dxa")
    grid = table._tbl.tblGrid
    for child in list(grid):
        grid.remove(child)
    for width in widths:
        grid_col = OxmlElement("w:gridCol")
        grid_col.set(qn("w:w"), str(width))
        grid.append(grid_col)
    for row in table.rows:
        for index, cell in enumerate(row.cells):
            set_cell_width(cell, widths[index])
            set_cell_margins(cell)
            cell.vertical_alignment = WD_CELL_VERTICAL_ALIGNMENT.CENTER


def keep_table_row_together(row) -> None:
    tr_pr = row._tr.get_or_add_trPr()
    if tr_pr.find(qn("w:cantSplit")) is None:
        tr_pr.append(OxmlElement("w:cantSplit"))


def set_row_geometry(row, widths: Sequence[int]) -> None:
    for index, cell in enumerate(row.cells):
        set_cell_width(cell, widths[index])
        set_cell_margins(cell)
        cell.vertical_alignment = WD_CELL_VERTICAL_ALIGNMENT.CENTER


def repeat_table_header(row) -> None:
    tr_pr = row._tr.get_or_add_trPr()
    if tr_pr.find(qn("w:tblHeader")) is None:
        tr_pr.append(OxmlElement("w:tblHeader"))


def configure_bullet_numbering(doc: Document) -> int:
    numbering = doc.part.numbering_part.element
    abstract_ids = [int(node.get(qn("w:abstractNumId")))
                    for node in numbering.findall(qn("w:abstractNum"))]
    num_ids = [int(node.get(qn("w:numId"))) for node in numbering.findall(qn("w:num"))]
    abstract_id = max(abstract_ids, default=0) + 1
    num_id = max(num_ids, default=0) + 1

    abstract = OxmlElement("w:abstractNum")
    abstract.set(qn("w:abstractNumId"), str(abstract_id))
    multi = OxmlElement("w:multiLevelType")
    multi.set(qn("w:val"), "singleLevel")
    abstract.append(multi)
    level = OxmlElement("w:lvl")
    level.set(qn("w:ilvl"), "0")
    start = OxmlElement("w:start")
    start.set(qn("w:val"), "1")
    level.append(start)
    num_fmt = OxmlElement("w:numFmt")
    num_fmt.set(qn("w:val"), "bullet")
    level.append(num_fmt)
    lvl_text = OxmlElement("w:lvlText")
    lvl_text.set(qn("w:val"), "•")
    level.append(lvl_text)
    lvl_jc = OxmlElement("w:lvlJc")
    lvl_jc.set(qn("w:val"), "left")
    level.append(lvl_jc)
    p_pr = OxmlElement("w:pPr")
    tabs = OxmlElement("w:tabs")
    tab = OxmlElement("w:tab")
    tab.set(qn("w:val"), "num")
    tab.set(qn("w:pos"), "540")
    tabs.append(tab)
    p_pr.append(tabs)
    indent = OxmlElement("w:ind")
    indent.set(qn("w:left"), "540")
    indent.set(qn("w:hanging"), "270")
    p_pr.append(indent)
    spacing = OxmlElement("w:spacing")
    spacing.set(qn("w:after"), "80")
    spacing.set(qn("w:line"), "300")
    spacing.set(qn("w:lineRule"), "auto")
    p_pr.append(spacing)
    level.append(p_pr)
    r_pr = OxmlElement("w:rPr")
    fonts = OxmlElement("w:rFonts")
    fonts.set(qn("w:ascii"), BODY_FONT)
    fonts.set(qn("w:hAnsi"), BODY_FONT)
    fonts.set(qn("w:eastAsia"), EAST_ASIA_FONT)
    r_pr.append(fonts)
    level.append(r_pr)
    abstract.append(level)
    numbering.append(abstract)

    num = OxmlElement("w:num")
    num.set(qn("w:numId"), str(num_id))
    abstract_ref = OxmlElement("w:abstractNumId")
    abstract_ref.set(qn("w:val"), str(abstract_id))
    num.append(abstract_ref)
    numbering.append(num)
    return num_id


def apply_bullet_numbering(paragraph, num_id: int) -> None:
    p_pr = paragraph._p.get_or_add_pPr()
    num_pr = p_pr.find(qn("w:numPr"))
    if num_pr is None:
        num_pr = OxmlElement("w:numPr")
        p_pr.append(num_pr)
    ilvl = OxmlElement("w:ilvl")
    ilvl.set(qn("w:val"), "0")
    num_pr.append(ilvl)
    num_id_node = OxmlElement("w:numId")
    num_id_node.set(qn("w:val"), str(num_id))
    num_pr.append(num_id_node)


def set_run_font(run, name=BODY_FONT, size=None, color=INK, bold=None, italic=None) -> None:
    run.font.name = name
    run._element.get_or_add_rPr().rFonts.set(qn("w:ascii"), name)
    run._element.get_or_add_rPr().rFonts.set(qn("w:hAnsi"), name)
    run._element.get_or_add_rPr().rFonts.set(qn("w:eastAsia"),
                                             EAST_ASIA_FONT if name == BODY_FONT else name)
    if size is not None:
        run.font.size = Pt(size)
    if color:
        run.font.color.rgb = RGBColor.from_string(color)
    if bold is not None:
        run.bold = bold
    if italic is not None:
        run.italic = italic


def set_paragraph_keep(paragraph, keep_with_next=True, keep_lines=True) -> None:
    p_pr = paragraph._p.get_or_add_pPr()
    if keep_with_next:
        p_pr.append(OxmlElement("w:keepNext"))
    if keep_lines:
        p_pr.append(OxmlElement("w:keepLines"))


def style_document(doc: Document, short_title: str) -> None:
    section = doc.sections[0]
    section.page_width = Inches(8.5)
    section.page_height = Inches(11)
    section.top_margin = Inches(1.0)
    section.bottom_margin = Inches(1.0)
    section.left_margin = Inches(1.0)
    section.right_margin = Inches(1.0)
    section.header_distance = Inches(0.492)
    section.footer_distance = Inches(0.492)

    styles = doc.styles
    normal = styles["Normal"]
    normal.font.name = BODY_FONT
    normal._element.rPr.rFonts.set(qn("w:ascii"), BODY_FONT)
    normal._element.rPr.rFonts.set(qn("w:hAnsi"), BODY_FONT)
    normal._element.rPr.rFonts.set(qn("w:eastAsia"), EAST_ASIA_FONT)
    normal.font.size = Pt(11)
    normal.font.color.rgb = RGBColor.from_string(INK)
    normal.paragraph_format.space_before = Pt(0)
    normal.paragraph_format.space_after = Pt(6)
    normal.paragraph_format.line_spacing = 1.25

    heading_tokens = {
        "Heading 1": (16, BLUE, 18, 10),
        "Heading 2": (13, BLUE, 14, 7),
        "Heading 3": (11, DARK_BLUE, 10, 5),
        "Heading 4": (10, INK, 7, 3),
    }
    for name, (size, color, before, after) in heading_tokens.items():
        style = styles[name]
        style.font.name = BODY_FONT
        style._element.rPr.rFonts.set(qn("w:ascii"), BODY_FONT)
        style._element.rPr.rFonts.set(qn("w:hAnsi"), BODY_FONT)
        style._element.rPr.rFonts.set(qn("w:eastAsia"), EAST_ASIA_FONT)
        style.font.size = Pt(size)
        style.font.bold = True
        style.font.color.rgb = RGBColor.from_string(color)
        style.paragraph_format.space_before = Pt(before)
        style.paragraph_format.space_after = Pt(after)
        style.paragraph_format.keep_with_next = True
        style.paragraph_format.keep_together = True
        if name == "Heading 2":
            style.paragraph_format.page_break_before = True

    if "Code Signature" not in styles:
        code = styles.add_style("Code Signature", WD_STYLE_TYPE.PARAGRAPH)
    else:
        code = styles["Code Signature"]
    code.font.name = CODE_FONT
    code._element.rPr.rFonts.set(qn("w:ascii"), CODE_FONT)
    code._element.rPr.rFonts.set(qn("w:hAnsi"), CODE_FONT)
    code.font.size = Pt(8)
    code.font.color.rgb = RGBColor.from_string(DARK_BLUE)
    code.paragraph_format.left_indent = Inches(0.12)
    code.paragraph_format.right_indent = Inches(0.08)
    code.paragraph_format.space_before = Pt(2)
    code.paragraph_format.space_after = Pt(5)
    code.paragraph_format.line_spacing = 1.0
    p_pr = code._element.get_or_add_pPr()
    shd = OxmlElement("w:shd")
    shd.set(qn("w:fill"), LIGHT_GRAY)
    p_pr.append(shd)

    list_style = styles["List Bullet"]
    list_style.font.name = BODY_FONT
    list_style._element.rPr.rFonts.set(qn("w:eastAsia"), EAST_ASIA_FONT)
    list_style.font.size = Pt(11)
    list_style.paragraph_format.left_indent = Inches(0.375)
    list_style.paragraph_format.first_line_indent = Inches(-0.188)
    list_style.paragraph_format.space_after = Pt(4)
    list_style.paragraph_format.line_spacing = 1.25
    doc._fmcw_bullet_num_id = configure_bullet_numbering(doc)

    header = section.header
    hp = header.paragraphs[0]
    hp.alignment = WD_ALIGN_PARAGRAPH.LEFT
    hp.paragraph_format.space_after = Pt(0)
    run = hp.add_run(f"{PROJECT_NAME} | {short_title}")
    set_run_font(run, size=8, color=MUTED, bold=True)
    p_pr = hp._p.get_or_add_pPr()
    borders = OxmlElement("w:pBdr")
    bottom = OxmlElement("w:bottom")
    bottom.set(qn("w:val"), "single")
    bottom.set(qn("w:sz"), "4")
    bottom.set(qn("w:space"), "4")
    bottom.set(qn("w:color"), "D7DBE2")
    borders.append(bottom)
    p_pr.append(borders)

    footer = section.footer
    fp = footer.paragraphs[0]
    fp.alignment = WD_ALIGN_PARAGRAPH.RIGHT
    run = fp.add_run("Page ")
    set_run_font(run, size=8, color=MUTED)
    fld = OxmlElement("w:fldSimple")
    fld.set(qn("w:instr"), "PAGE")
    fp._p.append(fld)


def add_title_page(doc: Document, title: str, subtitle: str, manual_index: int) -> None:
    for _ in range(4):
        spacer = doc.add_paragraph()
        spacer.paragraph_format.space_after = Pt(12)
    kicker = doc.add_paragraph()
    kicker.alignment = WD_ALIGN_PARAGRAPH.CENTER
    run = kicker.add_run(f"SOURCE REFERENCE {manual_index:02d}")
    set_run_font(run, size=10, color=TEAL, bold=True)
    title_p = doc.add_paragraph()
    title_p.alignment = WD_ALIGN_PARAGRAPH.CENTER
    title_p.paragraph_format.space_before = Pt(14)
    title_p.paragraph_format.space_after = Pt(10)
    run = title_p.add_run(title)
    set_run_font(run, size=25, color=INK, bold=True)
    sub = doc.add_paragraph()
    sub.alignment = WD_ALIGN_PARAGRAPH.CENTER
    sub.paragraph_format.space_after = Pt(34)
    run = sub.add_run(subtitle)
    set_run_font(run, size=11.5, color=MUTED)

    table = doc.add_table(rows=4, cols=2)
    table.style = "Table Grid"
    set_table_geometry(table, [2700, 6660])
    rows = [
        ("프로젝트", f"{PROJECT_NAME} v{PROJECT_VERSION}"),
        ("기준 소스", "현재 worktree의 active build source"),
        ("문서 기준일", GENERATED_ON.isoformat()),
        ("문서 목적", "함수 입출력·연산·호출 관계·출력/부작용 추적"),
    ]
    for row, (label, value) in zip(table.rows, rows):
        set_cell_shading(row.cells[0], LIGHT_BLUE)
        p = row.cells[0].paragraphs[0]
        p.paragraph_format.space_after = Pt(0)
        set_run_font(p.add_run(label), size=8.5, color=DARK_BLUE, bold=True)
        p = row.cells[1].paragraphs[0]
        p.paragraph_format.space_after = Pt(0)
        set_run_font(p.add_run(value), size=8.5)
    doc.add_page_break()


def add_scope(doc: Document, category: str) -> None:
    doc.add_heading("문서 사용 범위", level=1)
    p = doc.add_paragraph()
    p.add_run("포함: ").bold = True
    p.add_run("CMake active source, 공용 header, Windows/Jetson 앱, MCU custom/generated Core, ROS package, 배포·도구·테스트 코드.")
    p = doc.add_paragraph()
    p.add_run("제외: ").bold = True
    p.add_run("STM32 HAL/CMSIS vendor 함수 본문, legacy/OpenCV/SDK vendor code, build·package 산출물. 이들은 연결 경계와 의존성만 기록한다.")
    p = doc.add_paragraph()
    p.add_run("읽는 법: ").bold = True
    p.add_run("각 함수 항목은 선언 위치, 입력, 내부 연산, 호출/호출자, 반환·상태 변경, 오류·동시성 특성을 순서대로 보여준다.")
    if category != "architecture":
        callout = doc.add_table(rows=1, cols=1)
        set_table_geometry(callout, [PAGE_WIDTH_DXA])
        set_cell_shading(callout.cell(0, 0), CALL_OUT)
        p = callout.cell(0, 0).paragraphs[0]
        set_run_font(p.add_run("정확성 기준 | "), size=8.5, color=DARK_BLUE, bold=True)
        set_run_font(p.add_run("호출 관계는 정적 이름 해석 결과이며 function pointer, Qt signal/slot, template·macro dispatch는 파일 설명과 특성 항목으로 보완한다."), size=8.5)


def add_manual_contents(doc: Document, files: Sequence[FileInfo]) -> None:
    doc.add_heading("수록 소스", level=1)
    grouped: dict[str, list[FileInfo]] = defaultdict(list)
    for file in files:
        grouped[str(Path(file.path).parent).replace("\\", "/")].append(file)
    for directory, entries in grouped.items():
        p = doc.add_paragraph()
        set_paragraph_keep(p)
        set_run_font(p.add_run(directory), size=9, color=DARK_BLUE, bold=True)
        for file in entries:
            p = doc.add_paragraph(style="List Bullet")
            apply_bullet_numbering(p, doc._fmcw_bullet_num_id)
            p.paragraph_format.left_indent = Inches(0.375)
            p.paragraph_format.first_line_indent = Inches(-0.188)
            p.paragraph_format.space_after = Pt(2)
            set_run_font(p.add_run(f"{Path(file.path).name}  "), size=8.5, bold=True)
            set_run_font(p.add_run(f"{len(file.functions)} functions | {file.lines} lines"), size=8, color=MUTED)


def return_description(function: FunctionInfo) -> str:
    return_type = function.return_type.strip()
    if return_type in {"void", "None"}:
        return "직접 반환값은 없다. 상태 변경·출력 parameter·I/O 또는 downstream publish가 결과다."
    if "bool" in return_type:
        return "bool을 반환한다. 성공/실패 또는 조건 판정인지와 true의 정확한 의미는 함수 본문 및 호출자를 확인해야 한다."
    if "FrameWaitResult" in return_type:
        return "FrameReady, Timeout, Stopped, Error 중 하나를 반환해 수집 loop의 다음 행동을 결정한다."
    if "optional" in return_type.lower() or "Ptr" in return_type or "shared_ptr" in return_type:
        return f"`{return_type}` 객체/소유권을 반환하며 유효 데이터가 없거나 실패한 경우 empty/null일 수 있다."
    if return_type == "(constructor/destructor)":
        return "객체 생성/정리 과정에서 member 또는 외부 자원의 생명주기를 설정한다."
    return f"`{return_type}` 값을 반환한다. 값의 의미는 함수명과 호출자의 후속 사용 조건을 따른다."


def processing_description(function: FunctionInfo) -> str:
    if function.declaration_only:
        return "이 파일은 구현이 아니라 함수 계약을 선언한다. 실제 연산은 override 구현, 연결된 .cpp 파일, Qt meta-object 또는 외부 backend에서 수행된다."
    note = FUNCTION_NOTES.get(function.qualified_name) or FUNCTION_NOTES.get(function.name)
    if note:
        return note
    name = function.name.lower()
    calls = [re.split(r"::|\.", call)[-1] for call in function.calls[:8]]
    call_text = ", ".join(f"`{call}`" for call in calls) if calls else "직접 계산/상태 갱신"
    if name.startswith(("validate", "isvalid", "valid", "check")):
        lead = "입력 구조와 범위·상호 제약을 검사하고 조건별 오류/경고를 누적하거나 boolean 결과를 만든다."
    elif name.startswith(("parse", "decode", "deserialize", "read")):
        lead = "입력 byte/text/stream을 경계 검사하면서 내부 타입으로 해석한다. 잘못된 길이·형식은 정상 데이터로 통과시키지 않는다."
    elif name.startswith(("encode", "serialize", "write", "save", "export")):
        lead = "내부 타입을 외부 byte/text/file 계약으로 변환하고 길이·순서·metadata를 함께 기록한다."
    elif name.startswith(("start", "arm", "enable", "connect", "open")):
        lead = "선행 상태를 확인한 뒤 필요한 자원을 준비하고 runtime 상태를 active로 전환한다. 중간 실패는 오류를 반환하거나 이미 연 자원을 정리한다."
    elif name.startswith(("stop", "abort", "disconnect", "close", "finalize", "shutdown")):
        lead = "producer/trigger를 먼저 중지하고 진행 중 I/O를 해제한 뒤 상태와 소유 자원을 종료 상태로 정리한다."
    elif name.startswith(("process", "compute", "build", "convert", "map", "update")):
        lead = "입력 데이터를 검증·변환하고 후속 계층이 소비할 결과 또는 상태 snapshot을 구성한다."
    elif name.startswith(("set", "apply", "configure")):
        lead = "입력 설정을 검증하거나 하위 adapter에 전달하고 성공한 값만 active member/state에 반영한다."
    elif name.startswith(("get", "status", "telemetry", "snapshot", "current", "to")):
        lead = "현재 member 또는 runtime 상태를 읽어 값/불변 snapshot/문자열 표현으로 반환한다."
    elif name.startswith(("paint", "draw", "render")):
        lead = "현재 표시 snapshot을 좌표/색상/화면 primitive로 변환해 GUI 또는 OpenGL surface에 그린다."
    elif name.startswith(("on", "handle")):
        lead = "event/callback 입력을 확인하고 관련 상태 변경 또는 다음 작업을 dispatch한다."
    else:
        lead = "함수 계약에 맞춰 입력을 검사하고 필요한 계산·상태 전이를 수행한다."
    detail = f" 주요 하위 호출은 {call_text}이다."
    if function.assignments:
        detail += " 갱신 대상은 " + ", ".join(f"`{value}`" for value in function.assignments[:5]) + " 등이다."
    return "[함수명 기반 추정, 구현 검증 아님] " + lead + detail


def parameter_description(parameter: ParameterInfo) -> str:
    lowered = parameter.name.lower()
    if lowered in {"error", "message"}:
        return "실패 원인을 호출자에게 전달하는 문자열 출력 parameter"
    if "config" in lowered:
        return "현재 적용할/참조할 SystemConfig 또는 하위 설정"
    if "revision" in lowered:
        return "설정 snapshot과 frame 결과를 결합하는 단조 증가 revision"
    if "frame" in lowered and "batch" in lowered:
        return "동일 DMA buffer에 포함된 A-scan record 묶음"
    if "frame" in lowered:
        return "raw 또는 processed frame 데이터와 metadata"
    if "timeout" in lowered:
        return "blocking I/O 또는 queue 대기의 최대 시간"
    if "index" in lowered or lowered.endswith("_id") or lowered.endswith("id"):
        return "배열/record/frame/장치 식별 또는 순서 값"
    if "path" in lowered or "file" in lowered:
        return "입력 또는 출력 파일 시스템 위치"
    if "enabled" in lowered:
        return "기능 활성/비활성 선택"
    if "callback" in lowered or "handler" in lowered:
        return "완료·오류·상태 이벤트를 상위 계층에 전달하는 callback"
    if not parameter.name:
        return "선언된 위치 기반 parameter"
    return f"`{parameter.name}`에 전달되는 {parameter.direction} 값"


def add_function_entry(doc: Document, function: FunctionInfo, index: int) -> None:
    kind = "선언" if function.declaration_only else "정의"
    title = f"{index}. {function.qualified_name} [{kind}]"
    heading = doc.add_heading(title, level=3)
    set_paragraph_keep(heading)
    loc = doc.add_paragraph()
    loc.paragraph_format.space_after = Pt(3)
    set_run_font(loc.add_run(f"{function.file}:{function.line}"), size=7.5, color=MUTED, italic=True)
    sig = doc.add_paragraph(style="Code Signature")
    sig.paragraph_format.keep_together = True
    sig.add_run(textwrap.fill(function.signature, width=110, subsequent_indent="  "))

    if function.parameters:
        table = doc.add_table(rows=1, cols=4)
        table.style = "Table Grid"
        set_table_geometry(table, [1450, 2350, 1150, 4410])
        headers = ["변수", "형식/선언", "방향", "역할"]
        for cell, text_value in zip(table.rows[0].cells, headers):
            set_cell_shading(cell, LIGHT_BLUE)
            p = cell.paragraphs[0]
            p.paragraph_format.space_after = Pt(0)
            set_run_font(p.add_run(text_value), size=7.5, color=DARK_BLUE, bold=True)
        for parameter in function.parameters:
            cells = table.add_row().cells
            set_table_geometry(table, [1450, 2350, 1150, 4410])
            values = [parameter.name or "(unnamed)", parameter.declaration,
                      parameter.direction, parameter_description(parameter)]
            for cell, text_value in zip(cells, values):
                p = cell.paragraphs[0]
                p.paragraph_format.space_after = Pt(0)
                set_run_font(p.add_run(text_value), name=CODE_FONT if cell is cells[1] else BODY_FONT,
                             size=7.2, color=INK)
    else:
        p = doc.add_paragraph()
        p.paragraph_format.space_after = Pt(3)
        set_run_font(p.add_run("입력 | "), size=8.2, color=DARK_BLUE, bold=True)
        set_run_font(p.add_run("명시적 parameter 없음. member/global state 또는 event context를 사용한다."), size=8.2)

    details = [
        ("연산", processing_description(function)),
        ("호출", ", ".join(f"`{call}`" for call in function.calls[:12]) if function.calls else "직접 계산 또는 단순 상태 접근"),
        ("호출자", "; ".join(function.callers[:8]) if function.callers else "외부 entry point, virtual dispatch, Qt signal/slot, test 또는 동일 파일 내부에서 호출"),
        ("출력", return_description(function)),
    ]
    if function.assignments:
        details.append(("상태/부작용", "갱신 또는 기록: " + ", ".join(f"`{value}`" for value in function.assignments)))
    if function.traits:
        details.append(("특성", ", ".join(function.traits)))
    table = doc.add_table(rows=len(details), cols=2)
    table.style = "Table Grid"
    set_table_geometry(table, [1700, 7660])
    for row, (label, value) in zip(table.rows, details):
        set_cell_shading(row.cells[0], LIGHT_GRAY)
        p = row.cells[0].paragraphs[0]
        p.paragraph_format.space_after = Pt(0)
        set_run_font(p.add_run(label), size=7.8, color=DARK_BLUE, bold=True)
        p = row.cells[1].paragraphs[0]
        p.paragraph_format.space_after = Pt(0)
        set_run_font(p.add_run(value), size=7.8)


def compact_return(function: FunctionInfo) -> str:
    value = function.return_type.strip()
    if value in {"void", "None"}:
        return "직접 반환 없음; 상태/I/O/callback이 결과"
    if "bool" in value:
        return "성공 true / 실패 false와 error·상태"
    if "FrameWaitResult" in value:
        return "FrameReady/Timeout/Stopped/Error"
    if value == "(constructor/destructor)":
        return "객체·자원 생명주기 변경"
    return value


def compact_contract(function: FunctionInfo) -> str:
    if function.parameters:
        params = ", ".join(
            f"{param.name or '?'}:{param.direction}" for param in function.parameters[:7])
        if len(function.parameters) > 7:
            params += f" 외 {len(function.parameters) - 7}개"
    else:
        params = "명시적 parameter 없음"
    if function.declaration_only:
        operation = "interface/함수 계약 선언; 구현은 override 또는 연결 구현 파일"
    else:
        operation = processing_description(function)
    operation = operation.replace("`", "")
    calls = ", ".join(re.split(r"::|\.", call)[-1] for call in function.calls[:5])
    if calls:
        operation += f" 연결: {calls}."
    output = compact_return(function)
    return f"입력 | {params}\n연산·연결 | {operation}\n출력 | {output}"


def is_detailed_function(function: FunctionInfo) -> bool:
    if function.declaration_only:
        return False
    if function.qualified_name in FUNCTION_NOTES or function.name in FUNCTION_NOTES:
        return True
    body_lines = len([line for line in function.body.splitlines() if line.strip()])
    high_risk_traits = {"CUDA/cuFFT", "FFTW", "네트워크 I/O", "직렬 I/O", "파일 I/O", "비동기/스레드"}
    return body_lines >= 45 or (body_lines >= 25 and bool(high_risk_traits.intersection(function.traits)))


def add_file_reference(doc: Document, file: FileInfo) -> None:
    heading = doc.add_heading(file.path, level=2)
    set_paragraph_keep(heading)
    meta = doc.add_table(rows=4, cols=2)
    meta.style = "Table Grid"
    set_table_geometry(meta, [1700, 7660])
    rows = [
        ("주요 역할", file.role),
        ("언어/규모", f"{file.language} | {file.lines} lines | {len(file.functions)} function definitions"),
        ("공개 타입", ", ".join(file.declarations) if file.declarations else "별도 class/struct/enum 선언 없음 또는 다른 header에 선언"),
        ("직접 의존", ", ".join(file.includes[:18]) if file.includes else "표준 script/runtime 또는 build-time command"),
    ]
    for row, (label, value) in zip(meta.rows, rows):
        set_cell_shading(row.cells[0], LIGHT_BLUE)
        p = row.cells[0].paragraphs[0]
        p.paragraph_format.space_after = Pt(0)
        set_run_font(p.add_run(label), size=7.8, color=DARK_BLUE, bold=True)
        p = row.cells[1].paragraphs[0]
        p.paragraph_format.space_after = Pt(0)
        set_run_font(p.add_run(value), size=7.8)
    if not file.functions:
        p = doc.add_paragraph()
        p.paragraph_format.space_before = Pt(4)
        p.paragraph_format.space_after = Pt(8)
        set_run_font(p.add_run("함수 정의 없음 | "), size=8.2, color=MUTED, bold=True)
        set_run_font(p.add_run("데이터 타입·상수·빌드 단계·설정 계약을 선언하는 파일로, 실제 실행 함수는 연결된 구현 파일 또는 도구 런타임에 있다."), size=8.2, color=MUTED)
        return
    doc.add_heading("함수 목록과 계약", level=3)
    table = doc.add_table(rows=1, cols=2)
    table.style = "Table Grid"
    set_table_geometry(table, [3650, 5710])
    repeat_table_header(table.rows[0])
    for cell, value in zip(table.rows[0].cells, ["함수 / 위치", "입력 → 연산·연결 → 출력"]):
        set_cell_shading(cell, LIGHT_BLUE)
        p = cell.paragraphs[0]
        p.paragraph_format.space_after = Pt(0)
        set_run_font(p.add_run(value), size=7.5, color=DARK_BLUE, bold=True)
    for function in file.functions:
        row = table.add_row()
        keep_table_row_together(row)
        set_row_geometry(row, [3650, 5710])
        left = row.cells[0].paragraphs[0]
        left.paragraph_format.space_after = Pt(0)
        kind = "선언" if function.declaration_only else "정의"
        set_run_font(left.add_run(f"{function.qualified_name} [{kind}]\n"), size=7.2,
                     color=DARK_BLUE, bold=True)
        set_run_font(left.add_run(textwrap.fill(function.signature, 58)), name=CODE_FONT,
                     size=6.5, color=INK)
        set_run_font(left.add_run(f"\nL{function.line}"), size=6.5, color=MUTED, italic=True)
        right = row.cells[1].paragraphs[0]
        right.paragraph_format.space_after = Pt(0)
        set_run_font(right.add_run(compact_contract(function)), size=6.9)

    detailed = [function for function in file.functions if is_detailed_function(function)]
    if detailed:
        doc.add_heading("주요 함수 상세 흐름", level=3)
        for index, function in enumerate(detailed, 1):
            add_function_entry(doc, function, index)


def add_architecture(doc: Document, files: Sequence[FileInfo]) -> None:
    doc.add_heading("1. 런타임 구성", level=1)
    p = doc.add_paragraph(
        "Windows와 Jetson은 동일한 fmcw_core 및 Qt 공용 계층을 사용한다. 플랫폼 차이는 실행 entry point, "
        "AlazarTech/CUDA 라이브러리 탐색, serial 장치 이름과 package script에 한정된다. CPU FFTW와 CUDA cuFFT는 "
        "동일한 chirp segmentation·window/DC·peak·거리·XYZIV 계약을 구현하며 Jetson 제품 빌드는 CUDA를 요구한다."
    )
    p.paragraph_format.space_after = Pt(8)

    doc.add_heading("2. 주 데이터 흐름", level=1)
    flow = [
        ("1", "Qt MainWindow", "operator 설정·start/stop·표시 선택", "ApplicationController"),
        ("2", "ApplicationController", "설정 revision 고정, service/장치 lifecycle", "AcquisitionSession"),
        ("3", "AcquisitionSession", "EDFA 준비 → digitizer arm → MCU trigger enable", "ContinuousAcquisitionWorker"),
        ("4", "Alazar/Replay/Simulator", "RawFrameBatch: records × full-period samples", "ProcessingService"),
        ("5", "CPU FFTW / CUDA cuFFT", "UP/DOWN 분할 → FFT → threshold peak → 거리·속도", "PointCloudFrame XYZIV"),
        ("6", "Frame completion", "B-scan/3D snapshot, 저장, UDP, 객체 검출", "Qt / file / ROS / CenterPoint"),
    ]
    table = doc.add_table(rows=1, cols=4)
    table.style = "Table Grid"
    set_table_geometry(table, [600, 2150, 3950, 2660])
    for cell, value in zip(table.rows[0].cells, ["단계", "주체", "입력·연산", "출력/다음 단계"]):
        set_cell_shading(cell, LIGHT_BLUE)
        set_run_font(cell.paragraphs[0].add_run(value), size=8, color=DARK_BLUE, bold=True)
    for values in flow:
        row = table.add_row()
        set_table_geometry(table, [600, 2150, 3950, 2660])
        for cell, value in zip(row.cells, values):
            cell.paragraphs[0].paragraph_format.space_after = Pt(0)
            set_run_font(cell.paragraphs[0].add_run(value), size=7.7)

    doc.add_heading("3. 시작·정지 순서", level=1)
    sequences = [
        ("Apply Setup", "ConfigManager load/apply → ConfigValidator → runtime adapter configure. scanner 관련 fingerprint가 바뀐 경우에만 MCU waveform loaded 상태를 무효화한다."),
        ("Connect", "RuntimeAdapterFactory가 source에 맞는 Alazar/Replay/Simulator와 MCU/EDFA adapter를 만들고 각 장치에 연결한다."),
        ("START", "storage/UDP/processing 준비 → acquisition worker 준비 → digitizer DMA arm → MCU scan/B-trigger enable. EDFA Controlled는 arm 전에 출력이 준비된다."),
        ("Run", "DMA batch는 bounded queue로 이동한다. GUI는 선택 A-scan snapshot만 낮은 rate로 복사하고 B-scan/3D는 frame 완료 시 교체한다."),
        ("STOP/E-STOP", "MCU trigger 제거 → digitizer abort/stop → acquisition worker join → queued processing 정리 → processing/UDP/storage finalize → EDFA off."),
    ]
    for label, detail in sequences:
        p = doc.add_paragraph()
        set_run_font(p.add_run(label + " | "), size=9, color=DARK_BLUE, bold=True)
        set_run_font(p.add_run(detail), size=9)

    doc.add_heading("4. 데이터 계약", level=1)
    contracts = [
        ("RawFrame", "단일 channel의 full-period ADC sample, UP/DOWN segment, config revision, optical state, scan position."),
        ("RawFrameBatch", "한 DMA buffer의 record 묶음. metadata record_count/record_length/sequence가 각 RawFrame metadata와 일치해야 한다."),
        ("ProcessedMeasurement", "UP/DOWN peak bin(분수 bin 포함), magnitude, 거리, velocity. threshold 미달은 NaN으로 표현한다."),
        ("PointCloudPoint", "일반 LiDAR 좌표계 X=forward, Y=left, Z=up와 intensity, radial velocity를 갖는 XYZIV."),
        ("PointCloudFrame", "scanner의 y_line_count가 완료된 한 frame 단위 point 집합. GUI·저장·UDP·검출은 완료 frame만 소비한다."),
    ]
    table = doc.add_table(rows=1, cols=2)
    table.style = "Table Grid"
    set_table_geometry(table, [2350, 7010])
    for cell, value in zip(table.rows[0].cells, ["계약", "내용"]):
        set_cell_shading(cell, LIGHT_BLUE)
        set_run_font(cell.paragraphs[0].add_run(value), size=8, color=DARK_BLUE, bold=True)
    for values in contracts:
        row = table.add_row()
        set_table_geometry(table, [2350, 7010])
        for cell, value in zip(row.cells, values):
            cell.paragraphs[0].paragraph_format.space_after = Pt(0)
            set_run_font(cell.paragraphs[0].add_run(value), size=8)

    doc.add_heading("5. 실행 스레드와 queue", level=1)
    threads = [
        ("Qt GUI", "설정/상태/plot paint. DMA 또는 FFT를 직접 수행하지 않는다."),
        ("Acquisition", "AlazarWaitAsyncBufferComplete 또는 replay/simulator 대기, batch metadata/trajectory stamping, processing queue push."),
        ("Processing", "CPU/GPU batch 처리, frame assembly, snapshot publish, storage/UDP/detection fan-out."),
        ("Storage", "raw/point-cloud file write와 주기 flush. 수집 thread의 disk I/O를 제거한다."),
        ("UDP", "완료 point-cloud frame fragment 송신. bounded queue 정책으로 수집을 막지 않는다."),
        ("Detection", "최신 완료 frame을 CenterPoint backend에 넣고 detection snapshot을 frame id와 함께 publish한다."),
    ]
    table = doc.add_table(rows=1, cols=2)
    table.style = "Table Grid"
    set_table_geometry(table, [2100, 7260])
    for cell, value in zip(table.rows[0].cells, ["실행 경계", "책임"]):
        set_cell_shading(cell, LIGHT_BLUE)
        set_run_font(cell.paragraphs[0].add_run(value), size=8, color=DARK_BLUE, bold=True)
    for values in threads:
        row = table.add_row()
        set_table_geometry(table, [2100, 7260])
        for cell, value in zip(row.cells, values):
            cell.paragraphs[0].paragraph_format.space_after = Pt(0)
            set_run_font(cell.paragraphs[0].add_run(value), size=8)

    doc.add_heading("6. 소스 범위와 외부 경계", level=1)
    p = doc.add_paragraph(
        "이 참조서는 프로젝트가 소유한 active source를 상세 대상으로 한다. STM32 HAL/CMSIS, ATS-SDK, Qt, FFTW, "
        "CUDA/cuFFT, cuDNN 및 외부 CenterPoint runtime은 호출 API와 빌드 연결만 기록한다. legacy는 설계 근거 비교용으로 "
        "보존하되 현행 함수 호출 그래프에는 포함하지 않는다."
    )

    doc.add_heading("7. 전체 소스 인덱스", level=1)
    table = doc.add_table(rows=1, cols=4)
    table.style = "Table Grid"
    set_table_geometry(table, [4750, 1350, 950, 2310])
    for cell, value in zip(table.rows[0].cells, ["파일", "언어", "함수", "수록 문서"]):
        set_cell_shading(cell, LIGHT_BLUE)
        set_run_font(cell.paragraphs[0].add_run(value), size=7.5, color=DARK_BLUE, bold=True)
    manual_name = {manual[3]: manual[0].replace("FMCW_LiDAR_", "").replace("_KO.docx", "") for manual in MANUALS}
    for file in files:
        row = table.add_row()
        set_table_geometry(table, [4750, 1350, 950, 2310])
        values = [file.path, file.language, str(len(file.functions)), manual_name.get(file.category, "00")]
        for cell, value in zip(row.cells, values):
            cell.paragraphs[0].paragraph_format.space_after = Pt(0)
            set_run_font(cell.paragraphs[0].add_run(value), size=6.7)


def add_appendix(doc: Document, files: Sequence[FileInfo], category: str) -> None:
    doc.add_heading("부록 A. 문서 통계", level=1)
    function_count = sum(len(file.functions) for file in files)
    definition_count = sum(1 for file in files for fn in file.functions if not fn.declaration_only)
    declaration_count = function_count - definition_count
    detailed_count = sum(1 for file in files for fn in file.functions if is_detailed_function(fn))
    table = doc.add_table(rows=4, cols=2)
    table.style = "Table Grid"
    set_table_geometry(table, [2600, 6760])
    for row, values in zip(table.rows, [
        ("수록 파일", str(len(files))),
        ("함수 정의", str(definition_count)),
        ("함수 선언", str(declaration_count)),
        ("상세 흐름 전개", str(detailed_count)),
    ]):
        set_cell_shading(row.cells[0], LIGHT_BLUE)
        set_run_font(row.cells[0].paragraphs[0].add_run(values[0]), size=8, color=DARK_BLUE, bold=True)
        set_run_font(row.cells[1].paragraphs[0].add_run(values[1]), size=8)

    doc.add_heading("부록 B. 정적 분석 한계", level=1)
    limitations = [
        "Qt signal/slot, virtual interface, std::function callback, macro와 template instantiation은 이름 기반 호출자 목록에 완전히 나타나지 않을 수 있다.",
        "CUDA kernel launch와 device helper는 일반 C++ 호출과 실행 경계가 다르므로 특성 및 파일 역할 설명을 함께 읽어야 한다.",
        "CMake, YAML, IOC처럼 선언형 파일은 함수 대신 build/config contract로 문서화한다.",
        "line 번호는 문서 생성 시점 worktree 기준이며 이후 편집으로 이동할 수 있다. qualified name과 파일 경로를 우선 검색한다.",
    ]
    for item in limitations:
        p = doc.add_paragraph(style="List Bullet")
        apply_bullet_numbering(p, doc._fmcw_bullet_num_id)
        p.paragraph_format.left_indent = Inches(0.375)
        p.paragraph_format.first_line_indent = Inches(-0.188)
        p.paragraph_format.space_after = Pt(3)
        set_run_font(p.add_run(item), size=8.5)


def create_manual(manual_index: int, filename: str, title: str, subtitle: str,
                  category: str, all_files: Sequence[FileInfo]) -> Path:
    doc = Document()
    style_document(doc, title)
    add_title_page(doc, title, subtitle, manual_index)
    add_scope(doc, category)
    if category == "architecture":
        add_architecture(doc, all_files)
    else:
        files = [file for file in all_files if file.category == category]
        add_manual_contents(doc, files)
        for file in files:
            add_file_reference(doc, file)
        add_appendix(doc, files, category)
    path = OUTPUT_DIR / filename
    doc.save(path)
    return path


def write_readme(files: Sequence[FileInfo], outputs: Sequence[Path]) -> None:
    function_count = sum(len(file.functions) for file in files)
    lines = [
        "# FMCW LiDAR Source Reference (2026-08-31)", "",
        "현행 worktree 기준 함수·데이터 흐름 참조서입니다.", "",
        f"- 상세 대상: {len(files)} files / {function_count} function definitions",
        "- 제외: legacy, STM32 HAL/CMSIS vendor source, build/package outputs", "",
        "## Documents", "",
    ]
    lines.extend(f"- `{path.name}`" for path in outputs)
    lines.extend([
        "", "## Regeneration", "",
        "Bundled Python 또는 python-docx가 설치된 Python으로 실행:", "",
        "```powershell", "python docs/archive/source_reference_2026-08-31/tools/generate_source_reference.py", "```", "",
        "`analysis/source_inventory.json`은 함수 위치와 호출 관계의 재검토용 중간 산출물입니다.",
    ])
    (OUTPUT_DIR / "README.md").write_text("\n".join(lines), encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--analysis-only", action="store_true")
    args = parser.parse_args()
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    ASSET_DIR.mkdir(parents=True, exist_ok=True)
    files = analyze_repository()
    save_analysis(files)
    print(f"Analyzed {len(files)} files and {sum(len(file.functions) for file in files)} functions")
    if args.analysis_only:
        return 0
    outputs = [
        create_manual(index, filename, title, subtitle, category, files)
        for index, (filename, title, subtitle, category) in enumerate(MANUALS)
    ]
    write_readme(files, outputs)
    for output in outputs:
        print(output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
