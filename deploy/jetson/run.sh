#!/usr/bin/env bash

set -Eeuo pipefail

package_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
executable="${package_dir}/FMCW_LiDAR_Jetson"
runtime_env="${package_dir}/jetson.env"

if [[ -f "${runtime_env}" ]]; then
  # shellcheck disable=SC1090
  source "${runtime_env}"
fi

if [[ ! -x "${executable}" ]]; then
  echo "ERROR: Jetson executable is missing: ${executable}" >&2
  exit 2
fi

for library_dir in \
  "${FMCW_JETSON_ALAZAR_SDK_ROOT:-/usr/local/AlazarTech}/lib" \
  "${FMCW_JETSON_ALAZAR_SDK_ROOT:-/usr/local/AlazarTech}/lib64" \
  /usr/local/cuda/targets/aarch64-linux/lib \
  /usr/local/cuda/lib64; do
  if [[ -d "${library_dir}" ]]; then
    export LD_LIBRARY_PATH="${library_dir}${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"
  fi
done

if [[ -z "${DISPLAY:-}" && -z "${WAYLAND_DISPLAY:-}" ]]; then
  echo "ERROR: No graphical desktop session is available. Start this script from the Jetson desktop." >&2
  exit 3
fi

cd -- "${package_dir}"
exec "${executable}" "$@"
