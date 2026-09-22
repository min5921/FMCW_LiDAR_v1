# 파일 정리 기록 — 2026-09-22

대상: 기본 FMCW, CenterPoint, PCDReplay와 이전 PointCloudFusion·ROS 작업 공간.

- 세 제품 루트에 `RUN.cmd`를 두고 README를 현재 제품의 안내로 정리했습니다.
- 기존 README는 `docs/archive/development_readme.md`에 보존했습니다.
- 문서 목록은 `docs/README.md`, 실행 패키지 안내는 `docs/guides/package_layout.md`입니다.
- 현재 제품 빌드·실행 패키지를 유지하고 과거 패키지는 `build/package_archive/2026-09-22/`에 모았습니다.
- 과거 검증·보고서 생성 스크립트·프로파일링·이미지·로그는 `build/package_archive/2026-09-22/workspace-history/`에 보관했습니다.
- 예전 CMake 빌드 캐시·중간 문서 렌더링·루트의 임시 obj를 삭제했습니다. 전체 6,311개 파일, 약 5.43 GiB입니다.
- 데이터·모델·소스·펌웨어·legacy·현재 패키지 DLL은 삭제 대상에서 제외했습니다.
- PointCloudFusion은 미해결 병합과 staged 변경이 있어 안내만 추가하고 기존 작업 및 빌드를 보존했습니다.
- ROS는 보조 작업 공간 안내를 추가했습니다.

삭제 캐시의 설정·테스트 로그와 원래 문서, 삭제 파일 목록, 이동 파일 SHA256 기록은 기본 프로젝트의
`build/package_archive/2026-09-22/file-organization/`에 있습니다. 이 폴더와 모든 build 산출물은 Git으로 전달되지 않습니다.
이전 작업 분리 때 만든 `build/package_archive/2026-09-22/workspace-separation-backup/`도 유지합니다.

앱 소스·설정·테스트·하드웨어·legacy·ROS 소스는 정리 전후 SHA256으로 확인했으며, 보호한 파일 3,183개가 동일했습니다.
실행 바이너리의 빌드 커밋은 `BUILD_FEATURES.txt`에 기록된 기존 커밋을 유지합니다.
이번 문서 정리만으로 실행 파일을 새 버전으로 표시하지 않습니다.

세 제품의 RUN.cmd를 다른 작업 디렉터리에서 실행하여 패키지 smoke test가 통과했습니다. 개발 도구 PATH 없이 확인했고, 세 실행 파일의 SHA256도 기존과 같았습니다.
