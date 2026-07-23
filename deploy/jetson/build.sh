#!/usr/bin/env bash

set -Eeuo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
root_dir="$(cd -- "${script_dir}/../.." && pwd)"
env_file="${FMCW_JETSON_ENV_FILE:-${script_dir}/jetson.env}"
build_dir="${root_dir}/build/jetson-release"

if [[ ! -f "${env_file}" ]]; then
  echo "ERROR: Jetson environment file not found: ${env_file}" >&2
  exit 2
fi

# shellcheck disable=SC1090
source "${env_file}"

is_on() {
  [[ "${1^^}" == "ON" || "${1}" == "1" || "${1^^}" == "TRUE" ]]
}

cmake_bool() {
  if is_on "$1"; then
    printf 'ON'
  else
    printf 'OFF'
  fi
}

detect_cuda_architectures() {
  local requested="${FMCW_JETSON_CUDA_ARCHITECTURES:-auto}"
  if [[ -n "${requested}" && "${requested,,}" != "auto" &&
        "${requested,,}" != "native" ]]; then
    printf '%s' "${requested}"
    return
  fi

  local device_model=""
  if [[ -r /proc/device-tree/model ]]; then
    device_model="$(tr -d '\0' </proc/device-tree/model)"
  fi
  case "${device_model,,}" in
    *thor*) printf '110'; return ;;
    *orin*) printf '87'; return ;;
    *xavier*) printf '72'; return ;;
    *tx2*) printf '62'; return ;;
    *nano*|*tx1*) printf '53'; return ;;
  esac

  if command -v nvidia-smi >/dev/null 2>&1; then
    local compute_capability
    compute_capability="$(
      nvidia-smi --query-gpu=compute_cap --format=csv,noheader 2>/dev/null |
        head -n 1 | tr -d '.[:space:]'
    )"
    if [[ "${compute_capability}" =~ ^[0-9]+$ ]]; then
      printf '%s' "${compute_capability}"
      return
    fi
  fi

  echo "ERROR: Unable to detect the Jetson CUDA architecture." >&2
  echo "Set FMCW_JETSON_CUDA_ARCHITECTURES in deploy/jetson/jetson.env." >&2
  exit 4
}

bash "${script_dir}/check_dependencies.sh"

cmake_arguments=(
  -S "${root_dir}"
  -B "${build_dir}"
  -G Ninja
  -DCMAKE_BUILD_TYPE=Release
  -DFMCW_TARGET_PLATFORM=JETSON
  -DFMCW_BUILD_QT_APPS=ON
  "-DFMCW_BUILD_TESTS=$(cmake_bool "${FMCW_JETSON_BUILD_TESTS:-ON}")"
  -DFMCW_WITH_FFTW=OFF
  -DFMCW_WITH_CUDA=ON
  -DFMCW_REQUIRE_CUDA=ON
  "-DFMCW_WITH_ALAZAR=$(cmake_bool "${FMCW_JETSON_WITH_ALAZAR:-ON}")"
)

if [[ -n "${FMCW_JETSON_ALAZAR_SDK_ROOT:-}" ]]; then
  cmake_arguments+=("-DALAZAR_SDK_ROOT=${FMCW_JETSON_ALAZAR_SDK_ROOT}")
fi
if [[ -n "${FMCW_JETSON_QT_ROOT:-}" ]]; then
  cmake_arguments+=("-DCMAKE_PREFIX_PATH=${FMCW_JETSON_QT_ROOT}")
fi
cuda_architectures="$(detect_cuda_architectures)"
printf 'Jetson CUDA architecture: %s\n' "${cuda_architectures}"
cmake_arguments+=("-DFMCW_CUDA_ARCHITECTURES=${cuda_architectures}")

if is_on "${FMCW_JETSON_WITH_ALAZAR:-ON}"; then
  alazar_root="${FMCW_JETSON_ALAZAR_SDK_ROOT:-/usr/local/AlazarTech}"
  alazar_library_dir="$(find "${alazar_root}" -name libATSApi.so -printf '%h\n' -quit)"
  cmake_arguments+=(
    "-DCMAKE_BUILD_RPATH=${alazar_library_dir}"
    "-DCMAKE_INSTALL_RPATH=${alazar_library_dir}"
    -DCMAKE_INSTALL_RPATH_USE_LINK_PATH=ON
  )
fi

printf '\nConfiguring Jetson Release build...\n'
cmake "${cmake_arguments[@]}"

build_jobs="${FMCW_JETSON_BUILD_JOBS:-auto}"
if [[ "${build_jobs}" == "auto" ]]; then
  build_jobs="$(nproc)"
fi

printf '\nBuilding with %s job(s)...\n' "${build_jobs}"
cmake --build "${build_dir}" --parallel "${build_jobs}"

if is_on "${FMCW_JETSON_BUILD_TESTS:-ON}"; then
  printf '\nRunning CTest...\n'
  (
    cd "${build_dir}"
    ctest --output-on-failure
  )
fi

executable="${build_dir}/src/fmcw_lidar_jetson"
if [[ ! -x "${executable}" ]]; then
  echo "ERROR: Jetson executable was not produced: ${executable}" >&2
  exit 3
fi

if is_on "${FMCW_JETSON_RUN_SMOKE_TEST:-ON}"; then
  printf '\nRunning Qt smoke test...\n'
  if [[ -n "${DISPLAY:-}" || -n "${WAYLAND_DISPLAY:-}" ]]; then
    "${executable}" --smoke-test
  else
    QT_QPA_PLATFORM=offscreen "${executable}" --smoke-test
  fi
fi

bash "${script_dir}/package.sh"

printf '\nJetson build completed successfully.\n'
printf 'Application: %s\n' "${root_dir}/build/package/FMCW_LiDAR_Jetson/FMCW_LiDAR_Jetson"
