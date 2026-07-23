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
if pkg-config --exists Qt6Core Qt6Gui Qt6Widgets Qt6OpenGL Qt6OpenGLWidgets 2>/dev/null; then
  qt_found=1
  qt_version="$(pkg-config --modversion Qt6Core)"
elif [[ -n "${FMCW_JETSON_QT_ROOT:-}" && -d "${FMCW_JETSON_QT_ROOT}" ]]; then
  qt_found=1
elif find /usr/lib /usr/local/lib -path '*/cmake/Qt6/Qt6Config.cmake' \
     -print -quit 2>/dev/null | grep -q .; then
  qt_found=1
fi
if [[ "${qt_found}" -eq 1 ]]; then
  if [[ -n "${qt_version}" ]]; then
    if printf '%s\n%s\n' "6.2.0" "${qt_version}" | sort -V -C; then
      pass "Qt ${qt_version} Core/Gui/Widgets/OpenGL development files"
    else
      fail "Qt ${qt_version} is too old; version 6.2 or newer is required"
    fi
  else
    pass "Qt 6 development files; CMake will verify version 6.2 or newer"
  fi
else
  fail "Qt 6.2+ development files were not found; install Qt or set FMCW_JETSON_QT_ROOT"
fi

if command -v nvcc >/dev/null 2>&1; then
  pass "CUDA compiler: $(command -v nvcc)"
else
  fail "nvcc is missing; install the JetPack CUDA toolkit"
fi

if ldconfig -p 2>/dev/null | grep -q 'libcufft\.so'; then
  pass "cuFFT runtime"
elif find /usr/local/cuda -name 'libcufft.so*' -print -quit 2>/dev/null | grep -q .; then
  pass "cuFFT runtime under /usr/local/cuda"
else
  fail "libcufft.so was not found; install the JetPack CUDA toolkit"
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
  alazar_nodes=(/dev/ATS* /dev/ats*)
  shopt -u nullglob
  if (( ${#alazar_nodes[@]} > 0 )); then
    pass "Alazar device node: ${alazar_nodes[0]}"
  else
    warn "An Alazar device node is not visible; the kernel driver may not be installed or loaded"
  fi
else
  warn "Alazar adapter is disabled; only Simulator/Replay can be used"
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
