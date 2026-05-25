#!/usr/bin/env bash

set -euo pipefail

BOTTLE_NAME="${BOTTLE_NAME:-Default}"
EXE_PATH="${EXE_PATH:-/home/furkan/Temp/RayOptix/bin/Release/net10.0/win-x64/publish/RayOptix.exe}"
LOG_PATH="${LOG_PATH:-/tmp/rayoptix-bottles-nvcuda.log}"
PREFIX_PATH="${PREFIX_PATH:-$HOME/.local/share/bottles/bottles/${BOTTLE_NAME}}"
RUNNER_PATH="${RUNNER_PATH:-$HOME/.local/share/bottles/runners/ge-proton10-33/files/bin/wine}"

if [[ ! -x "${RUNNER_PATH}" ]]; then
  echo "Runner not found: ${RUNNER_PATH}" >&2
  exit 1
fi

env \
  WINEPREFIX="${PREFIX_PATH}" \
  WINEDEBUG="+loaddll" \
  WINEDLLOVERRIDES="nvcuda=b;nvapi=b;nvapi64=b" \
  LD_LIBRARY_PATH="/usr/lib:/usr/lib32:${LD_LIBRARY_PATH:-}" \
  __NV_PRIME_RENDER_OFFLOAD=1 \
  __GLX_VENDOR_LIBRARY_NAME=nvidia \
  __VK_LAYER_NV_optimus=NVIDIA_only \
  "${RUNNER_PATH}" "${EXE_PATH}" \
  >"${LOG_PATH}" 2>&1 || true

echo "${LOG_PATH}"
