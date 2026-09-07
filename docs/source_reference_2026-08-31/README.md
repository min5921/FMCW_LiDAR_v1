# FMCW LiDAR Source Reference (2026-08-31)

2026-08-31 당시 소스를 대상으로 생성한 **과거 함수 색인**입니다. 현재 v2 구현의 검증된 규약은 `../runtime_contract_v2.md`를 먼저 확인하세요.

함수명/반환형 기반 자동 설명은 의미 추정이며, 오류 정리·검증·성공 반환을 보장하는 감사 결과가 아닙니다. 이번 v2 작업에서는 기존 DOCX를 재생성하거나 모든 함수 설명을 수동 검증하지 않았습니다.

- 상세 대상: 181 files / 2,083 function definitions and declarations
- 제외: legacy, STM32 HAL/CMSIS vendor source, build/package outputs
- 최종 분량: 8 documents / 527 rendered pages

## Documents

- `FMCW_LiDAR_00_Architecture_and_Source_Index_KO.docx`
- `FMCW_LiDAR_01_Core_Configuration_Acquisition_KO.docx`
- `FMCW_LiDAR_02_Device_Drivers_Protocols_KO.docx`
- `FMCW_LiDAR_03_Processing_Detection_KO.docx`
- `FMCW_LiDAR_04_Storage_Network_Replay_KO.docx`
- `FMCW_LiDAR_05_Qt_GUI_Applications_KO.docx`
- `FMCW_LiDAR_06_MCU_Firmware_KO.docx`
- `FMCW_LiDAR_07_ROS_Tools_Deployment_Tests_KO.docx`

## Regeneration

Bundled Python 또는 python-docx가 설치된 Python으로 실행:

```powershell
python docs/source_reference_2026-08-31/tools/generate_source_reference.py
```

`analysis/source_inventory.json`은 함수 위치와 호출 관계의 재검토용 중간 산출물입니다.

## Verification

DOCX 페이지·스타일·표 폭 계약 검사:

```powershell
python docs/source_reference_2026-08-31/tools/audit_docx_structure.py `
  docs/source_reference_2026-08-31 `
  build/doc_render/source_reference/qa/docx_structure_report.json
```

Word/PDF 렌더 결과 검사:

```powershell
python docs/source_reference_2026-08-31/tools/verify_source_reference.py `
  build/doc_render/source_reference `
  build/doc_render/source_reference/qa
```

현재 산출물은 DOCX 구조 감사 0건, 527개 렌더 페이지의 잘림·공백·페이지 크기 이상 0건으로 검증했습니다.
