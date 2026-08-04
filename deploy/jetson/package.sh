#!/usr/bin/env bash

set -Eeuo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
root_dir="$(cd -- "${script_dir}/../.." && pwd)"
build_dir="${root_dir}/build/jetson-release"
package_dir="${root_dir}/build/package/FMCW_LiDAR_Jetson"
source_executable="${build_dir}/src/fmcw_lidar_jetson"

if [[ ! -x "${source_executable}" ]]; then
  echo "ERROR: Build the Jetson target before packaging: ${source_executable}" >&2
  exit 2
fi

rm -rf -- "${package_dir}"
mkdir -p -- "${package_dir}/config"

install -m 0755 "${source_executable}" "${package_dir}/FMCW_LiDAR_Jetson"
install -m 0755 "${script_dir}/run.sh" "${package_dir}/run.sh"
cp -a "${root_dir}/config/." "${package_dir}/config/"

revision="source-bundle"
if command -v git >/dev/null 2>&1 && git -C "${root_dir}" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
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
  printf 'FMCW LiDAR Jetson Release\n'
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
  ldd "${package_dir}/FMCW_LiDAR_Jetson" >"${package_dir}/runtime_dependencies.txt" || true
fi

(
  cd "${package_dir}"
  sha256sum FMCW_LiDAR_Jetson >SHA256SUMS
)

printf 'Jetson runtime package: %s\n' "${package_dir}"
