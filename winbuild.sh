#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ORIGINAL_HOME="${HOME}"
DOTNET_CLI_HOME_DIR="${DOTNET_CLI_HOME:-/tmp/wine-dotnet-home}"
WINE_HOME_DIR="${WINE_HOME_DIR:-/tmp/wine-home}"
WINE_PREFIX_DIR="${WINEPREFIX:-${ORIGINAL_HOME}/.wine}"
USE_WINE_DOTNET="${USE_WINE_DOTNET:-1}"
NATIVE_BUILD_DIR="${SCRIPT_DIR}/native/build/Release/windows-x64"
LLVM_MINGW_ROOT="${LLVM_MINGW_ROOT:-/tmp/rayoptix-win/llvm-mingw}"
WINDOWS_CUDA_ROOT="${WINDOWS_CUDA_ROOT:-/tmp/rayoptix-win/cuda-windows}"
WINDOWS_CUDA_INSTALLER="${WINDOWS_CUDA_INSTALLER:-/tmp/rayoptix-win/cuda_13.2.1_windows.exe}"
WINDOWS_CUDA_RUNTIME_DLLS=(
    "${WINDOWS_CUDA_ROOT}/cuda_cudart/cudart/bin/x64/cudart64_13.dll"
    "${WINDOWS_CUDA_ROOT}/cuda_nvrtc/nvrtc/bin/x64/nvrtc64_130_0.dll"
    "${WINDOWS_CUDA_ROOT}/cuda_nvrtc/nvrtc/bin/x64/nvrtc-builtins64_132.dll"
)
LLVM_MINGW_RUNTIME_DLLS=(
    "${LLVM_MINGW_ROOT}/x86_64-w64-mingw32/bin/libc++.dll"
    "${LLVM_MINGW_ROOT}/x86_64-w64-mingw32/bin/libunwind.dll"
)
ALL_RUNTIME_DLLS=()
NATIVE_RUNTIME_DEPENDENCY_PATHS=""

mkdir -p "${DOTNET_CLI_HOME_DIR}" "${WINE_HOME_DIR}" "${WINE_PREFIX_DIR}"

if [[ ! -x "${LLVM_MINGW_ROOT}/bin/x86_64-w64-mingw32-clang++" ]]; then
    mkdir -p "${LLVM_MINGW_ROOT}"
    tar -xf /tmp/rayoptix-win/llvm-mingw.tar.xz -C "${LLVM_MINGW_ROOT}" --strip-components=1
fi

if [[ ! -f "${WINDOWS_CUDA_ROOT}/cuda_cudart/cudart/lib/x64/cudart.lib" ]]; then
    mkdir -p "${WINDOWS_CUDA_ROOT}"
    7z x -y "${WINDOWS_CUDA_INSTALLER}" \
        "cuda_cudart/cudart/include/*" \
        "cuda_cudart/cudart/lib/x64/*" \
        "cuda_cudart/cudart/bin/x64/cudart64_13.dll" \
        "cuda_nvrtc/nvrtc_dev/include/*" \
        "cuda_nvrtc/nvrtc_dev/lib/x64/*" \
        "cuda_nvrtc/nvrtc/bin/x64/nvrtc64_130_0.dll" \
        "cuda_nvrtc/nvrtc/bin/x64/nvrtc-builtins64_132.dll" \
        "cuda_cccl/thrust/include/*" \
        -o"${WINDOWS_CUDA_ROOT}" >/dev/null
fi

if [[ ! -f "${WINDOWS_CUDA_ROOT}/cuda_cudart/cudart/include/crt/host_defines.h" ]]; then
    mkdir -p "${WINDOWS_CUDA_ROOT}/cuda_cudart/cudart/include/crt"
    cp -r /opt/cuda/targets/x86_64-linux/include/crt/* "${WINDOWS_CUDA_ROOT}/cuda_cudart/cudart/include/crt/"
fi

if [[ ! -f "${LLVM_MINGW_ROOT}/x86_64-w64-mingw32/lib/libLIBCMT.a" ]]; then
    ln -sf libmsvcrt.a "${LLVM_MINGW_ROOT}/x86_64-w64-mingw32/lib/libLIBCMT.a"
fi

if [[ ! -f "${LLVM_MINGW_ROOT}/x86_64-w64-mingw32/lib/libOLDNAMES.a" ]]; then
    "${LLVM_MINGW_ROOT}/bin/llvm-ar" rc "${LLVM_MINGW_ROOT}/x86_64-w64-mingw32/lib/libOLDNAMES.a"
fi

ALL_RUNTIME_DLLS=("${WINDOWS_CUDA_RUNTIME_DLLS[@]}" "${LLVM_MINGW_RUNTIME_DLLS[@]}")
NATIVE_RUNTIME_DEPENDENCY_PATHS="$(IFS=';'; echo "${ALL_RUNTIME_DLLS[*]}")"

COMMON_ENV=(
    "PATH=/usr/bin:/bin:${PATH}"
    "HOME=${WINE_HOME_DIR}"
    "WINEPREFIX=${WINE_PREFIX_DIR}"
    "DOTNET_SKIP_FIRST_TIME_EXPERIENCE=1"
    "DOTNET_NOLOGO=1"
    "DOTNET_GENERATE_ASPNET_CERTIFICATE=false"
    "DOTNET_CLI_TELEMETRY_OPTOUT=1"
    "DOTNET_CLI_HOME=${DOTNET_CLI_HOME_DIR}"
    "NUGET_CERT_REVOCATION_MODE=offline"
    "ROPTIX_NATIVE_RUNTIME_DEPS=${NATIVE_RUNTIME_DEPENDENCY_PATHS}"
)

RESTORE_ARGS=(
    restore
    -r win-x64
    /p:NativeHostIsWindows=true
    /p:RestoreIgnoreFailedSources=true
    /p:NuGetAudit=false
    /p:SkipNativeOptixBuild=true
)

PUBLISH_ARGS=(
    publish
    -c Release
    -r win-x64
    --no-restore
    /p:NativeHostIsWindows=true
    /p:RestoreIgnoreFailedSources=true
    /p:NuGetAudit=false
    /p:SkipNativeOptixBuild=true
)

if [[ "${USE_WINE_DOTNET}" == "1" ]]; then
    cmake -S "${SCRIPT_DIR}/native" \
        -B "${NATIVE_BUILD_DIR}" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_TOOLCHAIN_FILE="${SCRIPT_DIR}/native/toolchains/llvm-mingw-windows-x64.cmake" \
        -DROPTIX_LLVM_MINGW_ROOT="${LLVM_MINGW_ROOT}" \
        -DROPTIX_WINDOWS_CUDA_ROOT="${WINDOWS_CUDA_ROOT}"
    cmake --build "${NATIVE_BUILD_DIR}" --config Release -j4
    env "WINEPREFIX=${WINE_PREFIX_DIR}" wineserver -k >/dev/null 2>&1 || true
    env "WINEPREFIX=${WINE_PREFIX_DIR}" wineserver -w >/dev/null 2>&1 || true
    env "${COMMON_ENV[@]}" \
        wine dotnet "${RESTORE_ARGS[@]}"
    exec env "${COMMON_ENV[@]}" \
        wine dotnet "${PUBLISH_ARGS[@]}" "$@"
else
    exec env "${COMMON_ENV[@]}" \
        dotnet "${PUBLISH_ARGS[@]}" "$@"
fi
