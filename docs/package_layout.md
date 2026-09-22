# Windows 실행 패키지

루트의 `RUN.cmd`를 더블클릭하면 `Basic` 실행본을 엽니다.

```text
build/
  preset-windows-base/        현재 개발 빌드
  package/
    Windows-Basic/
      FMCW_LiDAR.exe
      config/
      *.dll, platforms/, 기타 Qt 플러그인
      BUILD_FEATURES.txt
      BUILD_SOURCES.sha256
  package_archive/
    2026-09-22/               이전 실행본·Jetson 소스 번들
  archive/
    2026-09-22/               이전 검증 자료·로그
```

다른 PC에는 `Windows-Basic` 전체를 복사합니다. DLL·Qt 플러그인·config를 분리하지 않습니다.
`BUILD_FEATURES.txt`에서 제품 종류·소스 커밋·포함된 backend를 확인합니다.
실제 장비 사용 여부는 [검증 기록](workspace_verification_2026-09-22.md)을 확인하고 필요한 SDK/드라이버와 빌드 옵션을 준비합니다.

빌드 후 `deploy/windows/package.ps1`로 이 제품의 패키지를 갱신합니다.
현재 빌드 절차는 [작업 공간 안내](workspaces_ko.md)를 따릅니다.
이전 패키지는 실행 폴더에서 분리하여 `build/package_archive/2026-09-22/`로 옮겼습니다.
기존 날짜별 보관 폴더가 있으면 그대로 유지합니다.
Jetson 소스 번들을 새로 만들 때는 `deploy/jetson/export_source.ps1`와 [Jetson 안내](../deploy/jetson/README_KO.md)를 사용합니다.

측정·재생 데이터는 `data/`, 사용 중 생성된 결과는 `outputs/`에서 관리합니다.
PCDReplay의 기존 샘플이 기본 FMCW 폴더의 `data/samples/`에 있다면 그 위치에서 선택합니다.
