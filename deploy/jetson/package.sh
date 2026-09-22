#!/usr/bin/env bash

set -Eeuo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
root_dir="$(cd -- "${script_dir}/../.." && pwd)"
build_dir="${root_dir}/build/jetson-release"
package_dir="${root_dir}/build/package/Jetson-Basic"
env_file="${FMCW_JETSON_ENV_FILE:-${script_dir}/jetson.env}"
source_executable="${build_dir}/src/fmcw_lidar_jetson"

if [[ ! -x "${source_executable}" ]]; then
  echo "ERROR: Build the Jetson target before packaging: ${source_executable}" >&2
  exit 2
fi
if [[ ! -f "${env_file}" ]]; then
  echo "ERROR: Jetson environment file not found: ${env_file}" >&2
  exit 2
fi
for feature in 'WorkspaceVariant=Basic' 'CUDA_FFT=ON' 'FFTW=OFF' 'CenterPoint=OFF'; do
  if ! grep -Fx "${feature}" "${build_dir}/BUILD_FEATURES.txt" >/dev/null; then
    echo "ERROR: Not a Basic Jetson build (required ${feature})" >&2
    exit 2
  fi
done

if [[ -e "${package_dir}" ]]; then
  archive_dir="${root_dir}/build/package_archive/$(date -u +%Y-%m-%d)"
  mkdir -p -- "${archive_dir}"
  backup_dir="${archive_dir}/Jetson-Basic-$(date -u +%H%M%S)-$$"
  mv -- "${package_dir}" "${backup_dir}"
  printf 'Previous runtime, settings and data preserved: %s\n' "${backup_dir}"
fi
mkdir -p -- "${package_dir}/config"

install -m 0755 "${source_executable}" "${package_dir}/FMCW_LiDAR_Jetson"
install -m 0644 "${build_dir}/BUILD_FEATURES.txt" "${package_dir}/BUILD_FEATURES.txt"
install -m 0644 "${build_dir}/BUILD_SOURCES.sha256" "${package_dir}/BUILD_SOURCES.sha256"
install -m 0755 "${script_dir}/run.sh" "${package_dir}/run.sh"
install -m 0644 "${env_file}" "${package_dir}/jetson.env"
cp -a "${root_dir}/config/." "${package_dir}/config/"

revision="source-bundle"
if [[ -f "${build_dir}/BUILD_FEATURES.txt" ]]; then
  while IFS='=' read -r key value; do
    if [[ "${key}" == "Source" ]]; then revision="${value}"; break; fi
  done <"${build_dir}/BUILD_FEATURES.txt"
elif command -v git >/dev/null 2>&1 && git -C "${root_dir}" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
  revision="$(git -C "${root_dir}" rev-parse HEAD)"
elif [[ -f "${root_dir}/SOURCE_REVISION.txt" ]]; then
  revision="$(head -n 1 "${root_dir}/SOURCE_REVISION.txt")"
fi

cmake_version_output="$(cmake --version)"
cmake_version="${cmake_version_output%%$'\n'*}"
cuda_version=""
if command -v nvcc >/dev/null 2>&1; then
  cuda_version_output="$(nvcc --version)"
  cuda_version="${cuda_version_output##*$'\n'}"
fi

{
  printf 'FMCW LiDAR Basic Jetson Release\n'
  printf 'Built UTC: %s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
  printf 'Source revision: %s\n' "${revision}"
  printf 'Architecture: %s\n' "$(uname -m)"
  if [[ -r /proc/device-tree/model ]]; then
    printf 'Device: %s\n' "$(tr -d '\0' </proc/device-tree/model)"
  fi
  printf 'Kernel: %s\n' "$(uname -r)"
  printf 'CMake: %s\n' "${cmake_version}"
  if [[ -n "${cuda_version}" ]]; then
    printf 'CUDA: %s\n' "${cuda_version}"
  fi
} >"${package_dir}/BUILD_INFO.txt"

if command -v ldd >/dev/null 2>&1; then
  ldd "${package_dir}/FMCW_LiDAR_Jetson" >"${package_dir}/runtime_dependencies.txt"
  if grep 'not found' "${package_dir}/runtime_dependencies.txt" >/dev/null; then
    echo 'ERROR: Packaged executable has unresolved runtime dependencies' >&2
    exit 2
  fi
fi

(
  cd "${package_dir}"
  sha256sum FMCW_LiDAR_Jetson >SHA256SUMS
)

printf 'Jetson runtime package: %s\n' "${package_dir}"
