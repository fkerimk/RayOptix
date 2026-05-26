#!/usr/bin/env fish

set SCRIPT_DIR (cd (dirname (status filename)); pwd)
set EXE_PATH (if set -q EXE_PATH; echo $EXE_PATH; else; echo "$SCRIPT_DIR/bin/Release/net10.0/win-x64/publish/RayOptix.exe"; end)
set WINBUILD_ROOT "$SCRIPT_DIR/external/winbuild"
set PREFIX_PATH (if set -q PREFIX_PATH; echo $PREFIX_PATH; else; echo "$WINBUILD_ROOT/prefix"; end)
set WINE_HOME_DIR (if set -q WINE_HOME_DIR; echo $WINE_HOME_DIR; else; echo "$WINBUILD_ROOT/wine-home"; end)
set NVIDIA_LIBS_DLL_ROOT (if set -q NVIDIA_LIBS_DLL_ROOT; echo $NVIDIA_LIBS_DLL_ROOT; else; echo "$WINBUILD_ROOT/nvidia-libs"; end)
set WINE_NVOPTIX_DLL_ROOT (if set -q WINE_NVOPTIX_DLL_ROOT; echo $WINE_NVOPTIX_DLL_ROOT; else; echo "$WINBUILD_ROOT/wine-nvoptix-package/nvoptix-rayoptix/lib/wine"; end)
set DOTNET_ROOT_WIN (if set -q DOTNET_ROOT_WIN; echo $DOTNET_ROOT_WIN; else; echo "Z:$WINBUILD_ROOT\\tools\\dotnet-sdk-10.0.300-win-x64"; end)
set RUNNER_CANDIDATES \
    "$HOME/.local/share/bottles/runners/ge-proton10-33/files/bin/wine" \
    "$HOME/.local/share/bottles/runners/ge-proton10-34/files/bin/wine" \
    "$HOME/.local/share/bottles/runners/GE-Proton10-32/files/bin/wine" \
    "$HOME/.local/share/Steam/compatibilitytools.d/Proton-CachyOS Latest/files/bin/wine" \
    "$HOME/.local/share/Steam/compatibilitytools.d/Proton-GE Latest/files/bin/wine" \
    "/usr/bin/wine"

set RESOLVED_RUNNER_PATH ""
if set -q RUNNER_PATH
    set RESOLVED_RUNNER_PATH $RUNNER_PATH
else
    for candidate in $RUNNER_CANDIDATES
        if test -x "$candidate"
            set RESOLVED_RUNNER_PATH "$candidate"
            break
        end
    end
end

if test -z "$RESOLVED_RUNNER_PATH"
    echo "Runner not found under ~/.local/share/bottles/runners." >&2
    exit 1
end

env \
    HOME="$WINE_HOME_DIR" \
    WINEPREFIX="$PREFIX_PATH" \
    WINEDLLPATH="$NVIDIA_LIBS_DLL_ROOT:$WINE_NVOPTIX_DLL_ROOT" \
    DOTNET_ROOT="$DOTNET_ROOT_WIN" \
    DOTNET_ROOT_X64="$DOTNET_ROOT_WIN" \
    DOTNET_MULTILEVEL_LOOKUP=0 \
    WINEDLLOVERRIDES="nvcuda=n,b;nvoptix=n,b;nvapi=n,b;nvapi64=n,b;nvml=n,b" \
    LD_LIBRARY_PATH="/usr/lib:/usr/lib32:$LD_LIBRARY_PATH" \
    __NV_PRIME_RENDER_OFFLOAD=1 \
    __GLX_VENDOR_LIBRARY_NAME=nvidia \
    __VK_LAYER_NV_optimus=NVIDIA_only \
    "$RESOLVED_RUNNER_PATH" "$EXE_PATH"

exit $status
