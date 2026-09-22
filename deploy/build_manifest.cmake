set(FMCW_SOURCE_REVISION "unknown")
if(EXISTS "${PROJECT_SOURCE_DIR}/SOURCE_REVISION.txt")
  file(STRINGS "${PROJECT_SOURCE_DIR}/SOURCE_REVISION.txt" FMCW_SOURCE_REVISION LIMIT_COUNT 1)
elseif(EXISTS "${PROJECT_SOURCE_DIR}/.git")
  execute_process(COMMAND git rev-parse HEAD WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    OUTPUT_VARIABLE FMCW_SOURCE_REVISION OUTPUT_STRIP_TRAILING_WHITESPACE)
  execute_process(COMMAND git status --porcelain --untracked-files=normal -- src tests config deploy CMakeLists.txt
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}" OUTPUT_VARIABLE _fmcw_dirty)
  if(_fmcw_dirty)
    string(APPEND FMCW_SOURCE_REVISION "-dirty")
  endif()
endif()
file(GLOB_RECURSE _fmcw_manifest_sources RELATIVE "${PROJECT_SOURCE_DIR}"
  "${PROJECT_SOURCE_DIR}/src/*.cpp" "${PROJECT_SOURCE_DIR}/src/*.h"
  "${PROJECT_SOURCE_DIR}/src/*.cu" "${PROJECT_SOURCE_DIR}/src/CMakeLists.txt"
  "${PROJECT_SOURCE_DIR}/tests/*.cpp" "${PROJECT_SOURCE_DIR}/tests/CMakeLists.txt")
list(APPEND _fmcw_manifest_sources "CMakeLists.txt" "deploy/build_manifest.cmake")
list(FILTER _fmcw_manifest_sources EXCLUDE REGEX "/firmware/|/Debug/|/Release/")
list(SORT _fmcw_manifest_sources)
set(_fmcw_source_manifest "")
foreach(_source IN LISTS _fmcw_manifest_sources)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/${_source}")
  file(SHA256 "${PROJECT_SOURCE_DIR}/${_source}" _source_hash)
  string(APPEND _fmcw_source_manifest "${_source_hash}  ${_source}\n")
endforeach()
file(WRITE "${CMAKE_BINARY_DIR}/BUILD_SOURCES.sha256" "${_fmcw_source_manifest}")
file(SHA256 "${CMAKE_BINARY_DIR}/BUILD_SOURCES.sha256" _fmcw_source_hash)
file(WRITE "${CMAKE_BINARY_DIR}/BUILD_FEATURES.txt"
  "WorkspaceVariant=${FMCW_WORKSPACE_VARIANT}\nProject=${PROJECT_NAME}\nVersion=${PROJECT_VERSION}\nSource=${FMCW_SOURCE_REVISION}\n"
  "HostSourceManifestSHA256=${_fmcw_source_hash}\n"
  "Platform=${FMCW_TARGET_PLATFORM}\nCompiler=${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}\n"
  "CMake=${CMAKE_VERSION}\nQt=${Qt6_VERSION}\nCUDA=${CUDAToolkit_VERSION}\n"
  "ATS=${FMCW_HAS_ALAZAR_SDK}\nATS_SDK=${ALAZAR_SDK_ROOT}\n"
  "FFTW=${FMCW_HAS_FFTW}\nCUDA_FFT=${FMCW_HAS_CUDA_FFT}\nCenterPoint=${FMCW_HAS_CENTERPOINT}\n")
