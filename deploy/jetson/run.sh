#!/usr/bin/env bash

set -Eeuo pipefail

package_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
executable="${package_dir}/FMCW_LiDAR_Jetson"

if [[ ! -x "${executable}" ]]; then
  echo "ERROR: Jetson executable is missing: ${executable}" >&2
  exit 2
fi

for library_dir in \
  /usr/local/AlazarTech/lib \
  /usr/local/AlazarTech/lib64 \
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
exec "${executable}"
