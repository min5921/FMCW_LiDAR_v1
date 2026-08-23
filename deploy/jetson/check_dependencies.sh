#!/usr/bin/env bash

set -Eeuo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
root_dir="$(cd -- "${script_dir}/../.." && pwd)"
env_file="${FMCW_JETSON_ENV_FILE:-${script_dir}/jetson.env}"

if [[ ! -f "${env_file}" ]]; then
  echo "ERROR: Jetson environment file not found: ${env_file}" >&2
  exit 2
fi

# shellcheck disable=SC1090
source "${env_file}"

errors=0
warnings=0

pass() {
  printf 'PASS  %s\n' "$1"
}

warn() {
  printf 'WARN  %s\n' "$1" >&2
  warnings=$((warnings + 1))
}

fail() {
  printf 'ERROR %s\n' "$1" >&2
  errors=$((errors + 1))
}

require_command() {
  local command_name="$1"
  local package_hint="$2"
  if command -v "${command_name}" >/dev/null 2>&1; then
    pass "${command_name}: $(command -v "${command_name}")"
  else
    fail "${command_name} is missing (${package_hint})"
  fi
}

is_on() {
  [[ "${1^^}" == "ON" || "${1}" == "1" || "${1^^}" == "TRUE" ]]
}

architecture="$(uname -m)"
if [[ "${architecture}" == "aarch64" || "${architecture}" == "arm64" ]]; then
  pass "ARM64 architecture: ${architecture}"
else
  fail "This bundle must be built natively on ARM64; detected ${architecture}"
fi

if [[ -r /proc/device-tree/model ]]; then
  model="$(tr -d '\0' </proc/device-tree/model)"
  if [[ "${model}" == *"Jetson"* || "${model}" == *"NVIDIA"* ]]; then
    pass "Jetson model: ${model}"
  else
    warn "ARM64 device does not identify itself as NVIDIA Jetson: ${model}"
  fi
else
  warn "Unable to read /proc/device-tree/model"
fi

require_command cmake "install CMake 3.18 or newer"
require_command ninja "install ninja-build"
require_command c++ "install build-essential"
require_command pkg-config "install pkg-config"

if command -v cmake >/dev/null 2>&1; then
  cmake_version="$(cmake --version | awk 'NR == 1 { print $3 }')"
  if printf '%s\n%s\n' "3.18.0" "${cmake_version}" | sort -V -C; then
    pass "CMake version: ${cmake_version}"
  else
    fail "CMake ${cmake_version} is too old; version 3.18 or newer is required"
  fi
fi

qt_found=0
qt_version=""
qt_config_path=""
if pkg-config --exists Qt6Core Qt6Gui Qt6Widgets Qt6OpenGL Qt6OpenGLWidgets 2>/dev/null; then
  qt_found=1
  qt_version="$(pkg-config --modversion Qt6Core)"
elif [[ -n "${FMCW_JETSON_QT_ROOT:-}" && -d "${FMCW_JETSON_QT_ROOT}" ]]; then
  qt_found=1
else
  qt_config_path="$(
    find /usr/lib /usr/local/lib \
      -path '*/cmake/Qt6/Qt6Config.cmake' \
      -print -quit 2>/dev/null || true
  )"
  if [[ -n "${qt_config_path}" ]]; then
    qt_found=1
  fi
fi
if [[ "${qt_found}" -eq 1 ]]; then
  if [[ -n "${qt_version}" ]]; then
    if printf '%s\n%s\n' "6.2.0" "${qt_version}" | sort -V -C; then
      pass "Qt ${qt_version} Core/Gui/Widgets/OpenGL development files"
    else
      fail "Qt ${qt_version} is too old; version 6.2 or newer is required"
    fi
  else
    if [[ -n "${qt_config_path}" ]]; then
      pass "Qt 6 development files: ${qt_config_path}; CMake will verify version 6.2 or newer"
    else
      pass "Qt 6 development files; CMake will verify version 6.2 or newer"
    fi
  fi
else
  fail "Qt 6.2+ development files were not found; install Qt or set FMCW_JETSON_QT_ROOT"
fi

cuda_compiler=""
if command -v nvcc >/dev/null 2>&1; then
  cuda_compiler="$(command -v nvcc)"
else
  for candidate in \
    /usr/local/cuda/bin/nvcc \
    /usr/local/cuda-*/bin/nvcc
  do
    if [[ -x "${candidate}" ]]; then
      cuda_compiler="${candidate}"
      break
    fi
  done
fi

if [[ -n "${cuda_compiler}" ]]; then
  pass "CUDA compiler: ${cuda_compiler}"
else
  fail "nvcc is missing; install the JetPack CUDA toolkit"
fi

cufft_library=""

for candidate in \
  /usr/local/cuda/targets/aarch64-linux/lib/libcufft.so \
  /usr/local/cuda/lib64/libcufft.so
do
  if [[ -e "${candidate}" ]]; then
    cufft_library="${candidate}"
    break
  fi
done

if [[ -z "${cufft_library}" ]]; then
  cufft_library="$(
    find -L /usr/local/cuda \
      -name 'libcufft.so' \
      -print -quit 2>/dev/null || true
  )"
fi

if [[ -n "${cufft_library}" ]]; then
  pass "cuFFT runtime: ${cufft_library}"
else
  fail "libcufft.so was not found; install the JetPack CUDA toolkit"
fi

if is_on "${FMCW_JETSON_WITH_CENTERPOINT:-OFF}"; then
  centerpoint_source="${FMCW_JETSON_CENTERPOINT_SOURCE_DIR:-}"
  centerpoint_runtime="${centerpoint_source}"
  if [[ -f "${centerpoint_source}/20_active_count_nms_project/include/centerpoint/gpu_preprocess.hpp" ]]; then
    centerpoint_runtime="${centerpoint_source}/20_active_count_nms_project"
  fi
  if [[ -f "${centerpoint_runtime}/include/centerpoint/gpu_preprocess.hpp" &&
        -f "${centerpoint_runtime}/cuda/gpu_preprocess.cu" &&
        -f "${centerpoint_runtime}/cuda/gpu_rpn.cu" &&
        -f "${centerpoint_runtime}/cuda/gpu_center_head.cu" &&
        -f "${centerpoint_runtime}/cuda/gpu_postprocess.cu" ]]; then
    pass "CenterPoint runtime source: ${centerpoint_runtime}"
  else
    fail "CenterPoint runtime source is incomplete: ${centerpoint_source}"
  fi

  centerpoint_weights="${FMCW_JETSON_CENTERPOINT_WEIGHTS_ROOT:-}"
  weight_manifests=(
    "04_pfn/weights_metadata.json"
    "06_rpn/rpn_weights_metadata.json"
    "07_head/head_weights_metadata.json"
  )
  weight_layout_valid=1
  for manifest in "${weight_manifests[@]}"; do
    if [[ ! -f "${centerpoint_weights}/${manifest}" ]]; then
      fail "CenterPoint weight manifest is missing: ${centerpoint_weights}/${manifest}"
      weight_layout_valid=0
    fi
  done
  if [[ "${weight_layout_valid}" -eq 1 ]]; then
    pass "CenterPoint runtime weights: ${centerpoint_weights}"
  fi

  cudnn_root="${FMCW_JETSON_CUDNN_ROOT:-}"
  cudnn_header=""
  for candidate in \
    "${cudnn_root}/include/cudnn.h" \
    /usr/include/cudnn.h \
    /usr/include/aarch64-linux-gnu/cudnn.h \
    /usr/local/cuda/include/cudnn.h
  do
    if [[ -f "${candidate}" ]]; then
      cudnn_header="${candidate}"
      break
    fi
  done
  if [[ -n "${cudnn_header}" ]]; then
    pass "cuDNN header: ${cudnn_header}"
  else
    fail "cudnn.h was not found; install the JetPack cuDNN development package"
  fi

  cudnn_library=""
  for candidate in \
    "${cudnn_root}/lib/libcudnn.so" \
    "${cudnn_root}/lib64/libcudnn.so" \
    /usr/lib/aarch64-linux-gnu/libcudnn.so \
    /usr/local/cuda/lib64/libcudnn.so
  do
    if [[ -e "${candidate}" ]]; then
      cudnn_library="${candidate}"
      break
    fi
  done
  if [[ -n "${cudnn_library}" ]]; then
    pass "cuDNN runtime: ${cudnn_library}"
  else
    fail "libcudnn.so was not found; install the JetPack cuDNN development package"
  fi
else
  warn "CenterPoint detector is disabled"
fi

if is_on "${FMCW_JETSON_WITH_ALAZAR:-ON}"; then
  alazar_root="${FMCW_JETSON_ALAZAR_SDK_ROOT:-/usr/local/AlazarTech}"
  if [[ ! -d "${alazar_root}" ]]; then
    fail "Alazar SDK root does not exist: ${alazar_root}"
  else
    header="$(find "${alazar_root}" -name AlazarApi.h -print -quit 2>/dev/null || true)"
    library="$(find "${alazar_root}" -name libATSApi.so -print -quit 2>/dev/null || true)"
    [[ -n "${header}" ]] && pass "Alazar header: ${header}" ||
      fail "AlazarApi.h was not found under ${alazar_root}"
    [[ -n "${library}" ]] && pass "Alazar ARM64 library: ${library}" ||
      fail "libATSApi.so was not found under ${alazar_root}"
  fi
  shopt -s nullglob
  alazar_nodes=(
    /dev/ATS*
    /dev/ats*
    /dev/AlazarTech/ATS*
    /dev/AlazarTech/ats*
  )
  shopt -u nullglob
  if (( ${#alazar_nodes[@]} > 0 )); then
    pass "Alazar device node: ${alazar_nodes[0]}"
  else
    warn "An Alazar device node is not visible; the kernel driver may not be installed or loaded"
  fi
else
  warn "Alazar adapter is disabled; only Simulator/Replay can be used"
fi

mcu_uart="${FMCW_JETSON_MCU_UART:-/dev/ttyTHS0}"
if [[ -e "${mcu_uart}" ]]; then
  if [[ -r "${mcu_uart}" && -w "${mcu_uart}" ]]; then
    pass "Jetson 40-pin MCU UART: ${mcu_uart} (read/write access)"
  else
    warn "Jetson 40-pin MCU UART exists but is not accessible by $(id -un): ${mcu_uart}; add the user to dialout and log in again"
  fi
else
  warn "Jetson 40-pin MCU UART is not visible: ${mcu_uart}; enable UART1 on J30 pins 8/10 with Jetson-IO"
fi

if [[ ! -f "${root_dir}/CMakeLists.txt" || ! -d "${root_dir}/src" ]]; then
  fail "Source bundle is incomplete: ${root_dir}"
else
  pass "Source bundle root: ${root_dir}"
fi

printf '\nDependency check: %d error(s), %d warning(s)\n' "${errors}" "${warnings}"
if [[ "${errors}" -ne 0 ]]; then
  exit 1
fi
