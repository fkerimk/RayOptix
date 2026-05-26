#!/usr/bin/env fish

set SCRIPT_DIR (cd (dirname (status filename)); pwd)
set TARGET_FRAMEWORK "net10.0"
set BUILD_SCRIPT_ARGS $argv

if test (uname -s) != Linux
    echo "build.fish only supports Linux hosts." >&2
    exit 1
end

set BUILD_ROOT "$SCRIPT_DIR/external/winbuild"
set DOWNLOAD_ROOT "$BUILD_ROOT/downloads"
set TOOL_ROOT "$BUILD_ROOT/tools"
set DOTNET_CLI_HOME_DIR "$BUILD_ROOT/wine-dotnet-home"
set WINE_HOME_DIR "$BUILD_ROOT/wine-home"
set WINE_PREFIX_DIR "$BUILD_ROOT/prefix"
set LLVM_MINGW_ARCHIVE "$DOWNLOAD_ROOT/llvm-mingw-20260519-ucrt-ubuntu-22.04-x86_64.tar.xz"
set LLVM_MINGW_ROOT "$BUILD_ROOT/llvm-mingw"
set WINDOWS_CUDA_INSTALLER "$DOWNLOAD_ROOT/cuda_12.9.0_windows.exe"
set WINDOWS_CUDA_ROOT "$BUILD_ROOT/cuda-windows"
set WINDOWS_CUDA_INCLUDE_ROOT "$WINDOWS_CUDA_ROOT/cuda_cudart/cudart/include"
set LINUX_CUDA_INCLUDE_ROOT "/opt/cuda/targets/x86_64-linux/include"
set WINDOWS_DOTNET_ARCHIVE "$DOWNLOAD_ROOT/dotnet-sdk-10.0.300-win-x64.zip"
set WINDOWS_DOTNET_ROOT "$TOOL_ROOT/dotnet-sdk-10.0.300-win-x64"
set WINDOWS_DOTNET_EXE "$WINDOWS_DOTNET_ROOT/dotnet.exe"
set WINE_NVOPTIX_ROOT "$BUILD_ROOT/wine-nvoptix"
set WINE_NVOPTIX_OUTPUT_ROOT "$BUILD_ROOT/wine-nvoptix-package"
set WINE_NVOPTIX_PACKAGE_ROOT "$WINE_NVOPTIX_OUTPUT_ROOT/nvoptix-rayoptix"
set WINE_NVOPTIX_DLL_ROOT "$WINE_NVOPTIX_PACKAGE_ROOT/lib/wine"
set WINE_NVOPTIX_WINDOWS_DLL "$WINE_NVOPTIX_DLL_ROOT/x86_64-windows/nvoptix.dll"
set WINE_NVOPTIX_UNIX_DLL "$WINE_NVOPTIX_DLL_ROOT/x86_64-unix/nvoptix.dll.so"
set NVIDIA_LIBS_ARCHIVE "$DOWNLOAD_ROOT/nvidia-libs-v0.8.6.tar.xz"
set NVIDIA_LIBS_ROOT "$BUILD_ROOT/nvidia-libs"
set NVIDIA_LIBS_PACKAGE_ROOT "$NVIDIA_LIBS_ROOT"
set NVIDIA_LIBS_DLL_ROOT "$NVIDIA_LIBS_PACKAGE_ROOT"
set NVIDIA_LIBS_SETUP_SCRIPT "$NVIDIA_LIBS_PACKAGE_ROOT/setup_nvlibs.sh"
set WINDOWS_DOTNET_ROOT_WIN "Z:$BUILD_ROOT\\tools\\dotnet-sdk-10.0.300-win-x64"
set WINE_RUNNER_CANDIDATES \
    "$HOME/.local/share/bottles/runners/ge-proton10-33/files/bin/wine" \
    "$HOME/.local/share/bottles/runners/ge-proton10-34/files/bin/wine" \
    "$HOME/.local/share/bottles/runners/GE-Proton10-32/files/bin/wine" \
    "$HOME/.local/share/Steam/compatibilitytools.d/Proton-CachyOS Latest/files/bin/wine" \
    "$HOME/.local/share/Steam/compatibilitytools.d/Proton-GE Latest/files/bin/wine" \
    "/usr/bin/wine"

function require_command --argument-names name
    if not command -sq $name
        echo "Required command not found: $name" >&2
        exit 1
    end
end

function require_file --argument-names path description
    if not test -f "$path"
        echo "$description not found: $path" >&2
        exit 1
    end
end

function ensure_dir --argument-names path
    mkdir -p "$path"
end

function redownload_file --argument-names url target description
    rm -f "$target"
    curl -L --fail "$url" -o "$target"
    if test $status -ne 0
        echo "Failed to download $description." >&2
        exit 1
    end
end

function ensure_valid_archive --argument-names path url description
    if not test -f "$path"
        redownload_file "$url" "$path" "$description"
        return
    end

    if string match -q "*.exe" -- "$path"
        7z t "$path" >/dev/null 2>&1
    else if string match -q "*.zip" -- "$path"
        unzip -tq "$path" >/dev/null 2>&1
    else
        tar -tf "$path" >/dev/null 2>&1
    end

    if test $status -ne 0
        echo "Invalid $description archive, re-downloading: $path" >&2
        redownload_file "$url" "$path" "$description"
    end
end

function resolve_choice --argument-names variable_name prompt
    set -l options $argv[3..-1]
    if set -q $variable_name
        set -l value $$variable_name
        if contains -- "$value" $options
            echo "$value"
            return 0
        end

        echo "Invalid value for $variable_name: $value" >&2
        exit 1
    end

    require_command fzf
    set -l selected (printf '%s\n' $options | fzf --prompt "$prompt> " --height 40% --layout=reverse --border --ansi)
    if test -z "$selected"
        echo "Selection cancelled." >&2
        exit 1
    end

    echo "$selected"
end

function log_path_for --argument-names name
    echo "$BUILD_ROOT/logs/$name.log"
end

function run_quiet --argument-names step_name
    set -l log_path (log_path_for "$step_name")
    $argv[2..-1] >"$log_path" 2>&1
    set -l cmd_status $status
    if test $cmd_status -ne 0
        echo "$step_name failed. Log: $log_path" >&2
        sed -n '1,220p' "$log_path" >&2
        exit $cmd_status
    end
end

function run_quiet_allow_fail --argument-names step_name
    set -l log_path (log_path_for "$step_name")
    $argv[2..-1] >"$log_path" 2>&1
    return $status
end

function to_msbuild_configuration --argument-names value
    switch "$value"
        case debug
            echo "Debug"
        case release
            echo "Release"
        case '*'
            echo "Unsupported configuration: $value" >&2
            exit 1
    end
end

function resolve_wine_runner
    if set -q WINE_RUNNER_PATH
        if test -x "$WINE_RUNNER_PATH"
            echo "$WINE_RUNNER_PATH"
            return 0
        end

        echo "Configured WINE_RUNNER_PATH is not executable: $WINE_RUNNER_PATH" >&2
        exit 1
    end

    for candidate in $WINE_RUNNER_CANDIDATES
        if test -x "$candidate"
            echo "$candidate"
            return 0
        end
    end

    echo "No Wine runner found. Set WINE_RUNNER_PATH to a Proton/Wine runner binary." >&2
    exit 1
end

function clear_stale_windows_cmake_cache --argument-names native_build_dir
    set -l cache_file "$native_build_dir/CMakeCache.txt"

    if not test -f "$cache_file"
        return 0
    end

    set -l cached_llvm_root (string replace -r '^ROPTIX_LLVM_MINGW_ROOT(:[A-Z]+)?=' '' -- (string match -r '^ROPTIX_LLVM_MINGW_ROOT(:[A-Z]+)?=.*$' < "$cache_file"))
    set -l cached_cxx (string replace -r '^CMAKE_CXX_COMPILER:FILEPATH=' '' -- (string match -r '^CMAKE_CXX_COMPILER:FILEPATH=.*$' < "$cache_file"))

    if test "$cached_llvm_root" != "$LLVM_MINGW_ROOT"; or begin test -n "$cached_cxx"; and not test -e "$cached_cxx"; end
        echo "Clearing stale Windows CMake cache in $native_build_dir" >&2
        rm -f "$cache_file"
        rm -rf "$native_build_dir/CMakeFiles"
    end
end

function ensure_windows_dotnet
    ensure_valid_archive \
        "$WINDOWS_DOTNET_ARCHIVE" \
        "https://builds.dotnet.microsoft.com/dotnet/Sdk/10.0.300/dotnet-sdk-10.0.300-win-x64.zip" \
        "Windows .NET SDK"

    if not test -f "$WINDOWS_DOTNET_EXE"
        rm -rf "$WINDOWS_DOTNET_ROOT"
        ensure_dir "$WINDOWS_DOTNET_ROOT"
        unzip -q "$WINDOWS_DOTNET_ARCHIVE" -d "$WINDOWS_DOTNET_ROOT"
        if test $status -ne 0
            echo "Failed to extract Windows .NET SDK." >&2
            exit 1
        end
    end

    require_file "$WINDOWS_DOTNET_EXE" "Windows .NET SDK host"
end

function ensure_nvidia_libs_source
    ensure_valid_archive \
        "$NVIDIA_LIBS_ARCHIVE" \
        "https://github.com/SveSop/nvidia-libs/releases/download/v0.8.6/nvidia-libs-v0.8.6.tar.xz" \
        "nvidia-libs"

    if not test -f "$NVIDIA_LIBS_ROOT/README.md"
        rm -rf "$NVIDIA_LIBS_ROOT"
        ensure_dir "$NVIDIA_LIBS_ROOT"
        tar -xf "$NVIDIA_LIBS_ARCHIVE" -C "$NVIDIA_LIBS_ROOT" --strip-components=1
        if test $status -ne 0
            echo "Failed to extract nvidia-libs." >&2
            exit 1
        end
    end
end

function ensure_nvidia_libs_package
    ensure_nvidia_libs_source

    if test -f "$NVIDIA_LIBS_SETUP_SCRIPT"
        return 0
    end

    require_file "$NVIDIA_LIBS_SETUP_SCRIPT" "nvidia-libs setup script"
end

function install_nvidia_libs_into_prefix
    ensure_dir "$WINE_PREFIX_DIR/drive_c/windows/system32"
    ensure_dir "$WINE_PREFIX_DIR/drive_c/windows/syswow64"

    ln -sf "$NVIDIA_LIBS_PACKAGE_ROOT/x64/nvcuda.dll" "$WINE_PREFIX_DIR/drive_c/windows/system32/nvcuda.dll"
    ln -sf "$NVIDIA_LIBS_PACKAGE_ROOT/x64/nvoptix.dll" "$WINE_PREFIX_DIR/drive_c/windows/system32/nvoptix.dll"
    ln -sf "$NVIDIA_LIBS_PACKAGE_ROOT/x64/nvcuvid.dll" "$WINE_PREFIX_DIR/drive_c/windows/system32/nvcuvid.dll"
    ln -sf "$NVIDIA_LIBS_PACKAGE_ROOT/x64/nvencodeapi64.dll" "$WINE_PREFIX_DIR/drive_c/windows/system32/nvencodeapi64.dll"
    ln -sf "$NVIDIA_LIBS_PACKAGE_ROOT/x64/nvapi64.dll" "$WINE_PREFIX_DIR/drive_c/windows/system32/nvapi64.dll"
    ln -sf "$NVIDIA_LIBS_PACKAGE_ROOT/x64/nvofapi64.dll" "$WINE_PREFIX_DIR/drive_c/windows/system32/nvofapi64.dll"

    if test -f "$NVIDIA_LIBS_PACKAGE_ROOT/x32/nvcuda.dll"
        ln -sf "$NVIDIA_LIBS_PACKAGE_ROOT/x32/nvcuda.dll" "$WINE_PREFIX_DIR/drive_c/windows/syswow64/nvcuda.dll"
    end
    if test -f "$NVIDIA_LIBS_PACKAGE_ROOT/x32/nvcuvid.dll"
        ln -sf "$NVIDIA_LIBS_PACKAGE_ROOT/x32/nvcuvid.dll" "$WINE_PREFIX_DIR/drive_c/windows/syswow64/nvcuvid.dll"
    end
    if test -f "$NVIDIA_LIBS_PACKAGE_ROOT/x32/nvencodeapi.dll"
        ln -sf "$NVIDIA_LIBS_PACKAGE_ROOT/x32/nvencodeapi.dll" "$WINE_PREFIX_DIR/drive_c/windows/syswow64/nvencodeapi.dll"
    end
    if test -f "$NVIDIA_LIBS_PACKAGE_ROOT/x32/nvapi.dll"
        ln -sf "$NVIDIA_LIBS_PACKAGE_ROOT/x32/nvapi.dll" "$WINE_PREFIX_DIR/drive_c/windows/syswow64/nvapi.dll"
    end

    if test $status -ne 0
        echo "Failed to link nvidia-libs into the Wine prefix." >&2
        exit 1
    end
end

function set_wine_dll_override --argument-names wine_runner dll_name dll_value
    env \
        HOME="$WINE_HOME_DIR" \
        WINEPREFIX="$WINE_PREFIX_DIR" \
        WINEDLLPATH="$NVIDIA_LIBS_DLL_ROOT:$WINE_NVOPTIX_DLL_ROOT" \
        "$wine_runner" reg add "HKEY_CURRENT_USER\\Software\\Wine\\DllOverrides" /v "$dll_name" /d "$dll_value" /f >/dev/null 2>&1

    if test $status -ne 0
        echo "Failed to set Wine DLL override for $dll_name." >&2
        exit 1
    end
end

function ensure_wine_nvoptix_checkout
    if test -d "$WINE_NVOPTIX_ROOT/.git"
        return 0
    end

    rm -rf "$WINE_NVOPTIX_ROOT"
    git clone --depth 1 https://github.com/SveSop/wine-nvoptix.git "$WINE_NVOPTIX_ROOT"
    if test $status -ne 0
        echo "Failed to clone wine-nvoptix." >&2
        exit 1
    end
end

function ensure_wine_nvoptix_package
    ensure_wine_nvoptix_checkout

    if test -f "$WINE_NVOPTIX_WINDOWS_DLL"; and test -f "$WINE_NVOPTIX_UNIX_DLL"
        return 0
    end

    rm -rf "$WINE_NVOPTIX_OUTPUT_ROOT"
    ensure_dir "$WINE_NVOPTIX_OUTPUT_ROOT"

    if test -x "$WINE_NVOPTIX_ROOT/package-release.sh"
        "$WINE_NVOPTIX_ROOT/package-release.sh" rayoptix "$WINE_NVOPTIX_OUTPUT_ROOT" --fakedll
    else if test -x "$WINE_NVOPTIX_ROOT/package_release.sh"
        "$WINE_NVOPTIX_ROOT/package_release.sh" rayoptix "$WINE_NVOPTIX_OUTPUT_ROOT" --fakedll
    else
        echo "wine-nvoptix package script not found." >&2
        exit 1
    end

    if test $status -ne 0
        echo "Failed to build wine-nvoptix package." >&2
        exit 1
    end

    require_file "$WINE_NVOPTIX_WINDOWS_DLL" "wine-nvoptix Windows relay DLL"
    require_file "$WINE_NVOPTIX_UNIX_DLL" "wine-nvoptix Unix relay DLL"
end

function initialize_wine_prefix --argument-names wine_runner
    ensure_dir "$WINE_PREFIX_DIR"
    ensure_nvidia_libs_package

    env \
        HOME="$WINE_HOME_DIR" \
        WINEPREFIX="$WINE_PREFIX_DIR" \
        WINEDLLPATH="$NVIDIA_LIBS_DLL_ROOT:$WINE_NVOPTIX_DLL_ROOT" \
        WINEDEBUG=-all \
        "$wine_runner" wineboot -u
    if test $status -ne 0
        echo "Failed to initialize Wine prefix." >&2
        exit 1
    end

    install_nvidia_libs_into_prefix

    ensure_dir "$WINE_PREFIX_DIR/drive_c/windows/system32"
    ln -sf "$WINE_NVOPTIX_WINDOWS_DLL" "$WINE_PREFIX_DIR/drive_c/windows/system32/nvoptix.dll"
    if test $status -ne 0
        echo "Failed to link nvoptix.dll into the Wine prefix." >&2
        exit 1
    end

    set_wine_dll_override "$wine_runner" nvcuda native,builtin
    set_wine_dll_override "$wine_runner" nvoptix native,builtin
    set_wine_dll_override "$wine_runner" nvcuvid native,builtin
    set_wine_dll_override "$wine_runner" nvencodeapi64 native,builtin
    set_wine_dll_override "$wine_runner" nvapi64 native,builtin
    set_wine_dll_override "$wine_runner" nvofapi64 native,builtin
    set_wine_dll_override "$wine_runner" nvapi native,builtin
    if test -f "$NVIDIA_LIBS_PACKAGE_ROOT/x32/nvencodeapi.dll"
        set_wine_dll_override "$wine_runner" nvencodeapi native,builtin
    end
end

function resolve_linux_native_library --argument-names native_build_dir
    if test -f "$native_build_dir/RayOptixNative.so"
        echo "$native_build_dir/RayOptixNative.so"
        return 0
    end

    if test -f "$native_build_dir/libRayOptixNative.so"
        echo "$native_build_dir/libRayOptixNative.so"
        return 0
    end

    echo "Linux native library not found in $native_build_dir" >&2
    exit 1
end

function copy_files_to_dir --argument-names destination
    ensure_dir "$destination"
    for file in $argv[2..-1]
        require_file "$file" "Build output"
        cp -f "$file" "$destination/"
        if test $status -ne 0
            echo "Failed to copy $file to $destination" >&2
            exit 1
        end
    end
end

function run_windows_app --argument-names exe_path trace_nvcuda
    set -l wine_runner (resolve_wine_runner)
    set -l runner_dir (dirname "$wine_runner")
    set -l wineserver_path "$runner_dir/wineserver"

    if test -x "$wineserver_path"
        env HOME="$WINE_HOME_DIR" WINEPREFIX="$WINE_PREFIX_DIR" "$wineserver_path" -k >/dev/null 2>&1 || true
        env HOME="$WINE_HOME_DIR" WINEPREFIX="$WINE_PREFIX_DIR" "$wineserver_path" -w >/dev/null 2>&1 || true
    end

    set -lx HOME "$WINE_HOME_DIR"
    set -lx WINEPREFIX "$WINE_PREFIX_DIR"
    set -lx WINEDLLPATH "$NVIDIA_LIBS_DLL_ROOT:$WINE_NVOPTIX_DLL_ROOT"
    set -lx DOTNET_ROOT "$WINDOWS_DOTNET_ROOT_WIN"
    set -lx DOTNET_ROOT_X64 "$WINDOWS_DOTNET_ROOT_WIN"
    set -lx DOTNET_MULTILEVEL_LOOKUP 0
    set -lx WINEDLLOVERRIDES "nvcuda=n,b;nvoptix=n,b;nvapi=n,b;nvapi64=n,b;nvml=n,b"
    set -lx LD_LIBRARY_PATH "/usr/lib:/usr/lib32:$LD_LIBRARY_PATH"
    set -lx __NV_PRIME_RENDER_OFFLOAD 1
    set -lx __GLX_VENDOR_LIBRARY_NAME nvidia
    set -lx __VK_LAYER_NV_optimus NVIDIA_only

    if test "$trace_nvcuda" = yes
        set -lx WINEDEBUG "+loaddll"
        set -l trace_log_path (log_path_for "windows-trace-nvcuda")
        "$wine_runner" "$exe_path" >"$trace_log_path" 2>&1
        set -l trace_status $status
        echo "trace log: $trace_log_path"
        return $trace_status
    end

    set -lx WINEDEBUG -all
    "$wine_runner" "$exe_path"
end

function build_linux --argument-names configuration run_after_build
    require_command cmake
    require_command dotnet

    set -l native_build_dir "$SCRIPT_DIR/native/build/$configuration/linux-x64"
    set -l managed_output_dir "$SCRIPT_DIR/bin/$configuration/$TARGET_FRAMEWORK"

    echo "linux: cmake configure"
    run_quiet "linux-cmake-configure" cmake -S "$SCRIPT_DIR/native" -B "$native_build_dir" -DCMAKE_BUILD_TYPE="$configuration"

    echo "linux: cmake build"
    run_quiet "linux-cmake-build" cmake --build "$native_build_dir" --config "$configuration" -j4

    echo "linux: dotnet build"
    run_quiet "linux-dotnet-build" dotnet build "$SCRIPT_DIR/RayOptix.csproj" -c "$configuration" -v:q -nologo $BUILD_SCRIPT_ARGS

    set -l native_library (resolve_linux_native_library "$native_build_dir")
    copy_files_to_dir "$managed_output_dir" "$native_library"

    if test "$run_after_build" = yes
        set -lx LD_LIBRARY_PATH "$managed_output_dir:$native_build_dir:$LD_LIBRARY_PATH"
        "$managed_output_dir/RayOptix"
        if test $status -ne 0
            dotnet "$managed_output_dir/RayOptix.dll"
            exit $status
        end
    end
end

function build_windows --argument-names configuration run_after_build trace_nvcuda
    require_command cmake
    require_command curl
    require_command tar
    require_command unzip
    require_command 7z
    require_command git

    set -l native_build_dir "$SCRIPT_DIR/native/build/$configuration/windows-x64"
    set -l windows_msbuild_root "$BUILD_ROOT/msbuild/$configuration"
    set -l windows_msbuild_obj_root "external/winbuild/msbuild/$configuration/obj/"
    set -l windows_publish_dir "$SCRIPT_DIR/bin/$configuration/$TARGET_FRAMEWORK/win-x64/publish"
    set -l windows_native_library_path "$native_build_dir/RayOptixNative.dll"
    set -l all_runtime_dlls

    ensure_dir "$BUILD_ROOT"
    ensure_dir "$DOWNLOAD_ROOT"
    ensure_dir "$TOOL_ROOT"
    ensure_dir "$DOTNET_CLI_HOME_DIR"
    ensure_dir "$WINE_HOME_DIR"
    ensure_dir "$WINE_PREFIX_DIR"
    ensure_dir "$BUILD_ROOT/logs"
    ensure_dir "$windows_msbuild_root"

    set -l wine_runner (resolve_wine_runner)

    ensure_valid_archive \
        "$LLVM_MINGW_ARCHIVE" \
        "https://github.com/mstorsjo/llvm-mingw/releases/download/20260519/llvm-mingw-20260519-ucrt-ubuntu-22.04-x86_64.tar.xz" \
        "llvm-mingw"

    if not test -x "$LLVM_MINGW_ROOT/bin/x86_64-w64-mingw32-clang++"
        ensure_dir "$LLVM_MINGW_ROOT"
        tar -xf "$LLVM_MINGW_ARCHIVE" -C "$LLVM_MINGW_ROOT" --strip-components=1
        if test $status -ne 0
            echo "Failed to extract llvm-mingw." >&2
            exit 1
        end
    end

    ensure_valid_archive \
        "$WINDOWS_CUDA_INSTALLER" \
        "https://developer.download.nvidia.com/compute/cuda/12.9.0/local_installers/cuda_12.9.0_576.02_windows.exe" \
        "CUDA Windows installer"

    ensure_windows_dotnet
    run_quiet "windows-wine-nvoptix-package" ensure_wine_nvoptix_package
    run_quiet "windows-wine-prefix-init" initialize_wine_prefix "$wine_runner"

    if not test -f "$WINDOWS_CUDA_ROOT/cuda_cudart/cudart/lib/x64/cudart.lib"; \
        or not test -f "$WINDOWS_CUDA_ROOT/cuda_nvrtc/nvrtc_dev/lib/x64/nvrtc.lib"; \
        or not test -f "$WINDOWS_CUDA_ROOT/cuda_cudart/cudart/bin/cudart64_12.dll"; \
        or not test -f "$WINDOWS_CUDA_ROOT/cuda_nvrtc/nvrtc/bin/nvrtc64_120_0.dll"; \
        or not test -f "$WINDOWS_CUDA_ROOT/cuda_nvrtc/nvrtc/bin/nvrtc-builtins64_129.dll"
        rm -rf "$WINDOWS_CUDA_ROOT"
        ensure_dir "$WINDOWS_CUDA_ROOT"
        7z x -y "$WINDOWS_CUDA_INSTALLER" \
            "cuda_cudart/cudart/include/*" \
            "cuda_cudart/cudart/lib/x64/*" \
            "cuda_cudart/cudart/bin/cudart64_12.dll" \
            "cuda_nvrtc/nvrtc_dev/include/*" \
            "cuda_nvrtc/nvrtc_dev/lib/x64/*" \
            "cuda_nvrtc/nvrtc/bin/nvrtc64_120_0.dll" \
            "cuda_nvrtc/nvrtc/bin/nvrtc-builtins64_129.dll" \
            "cuda_cccl/thrust/include/*" \
            "-o$WINDOWS_CUDA_ROOT" >/dev/null
        if test $status -ne 0
            echo "Failed to extract Windows CUDA payloads." >&2
            exit 1
        end
    end

    require_file "$LINUX_CUDA_INCLUDE_ROOT/cuda.h" "Linux CUDA header"
    require_file "$LINUX_CUDA_INCLUDE_ROOT/cuda_runtime_api.h" "Linux CUDA runtime header"
    require_file "$LINUX_CUDA_INCLUDE_ROOT/nvrtc.h" "Linux NVRTC header"

    ensure_dir "$WINDOWS_CUDA_INCLUDE_ROOT"
    ensure_dir "$WINDOWS_CUDA_INCLUDE_ROOT/crt"
    cp -rn "$LINUX_CUDA_INCLUDE_ROOT/"* "$WINDOWS_CUDA_INCLUDE_ROOT/"
    cp -rn "$LINUX_CUDA_INCLUDE_ROOT/crt/"* "$WINDOWS_CUDA_INCLUDE_ROOT/crt/"

    require_file "$WINDOWS_CUDA_ROOT/cuda_cudart/cudart/lib/x64/cudart.lib" "Windows cudart import library"
    require_file "$WINDOWS_CUDA_ROOT/cuda_cudart/cudart/lib/x64/cuda.lib" "Windows CUDA driver import library"
    require_file "$WINDOWS_CUDA_ROOT/cuda_nvrtc/nvrtc_dev/lib/x64/nvrtc.lib" "Windows NVRTC import library"
    require_file "$WINDOWS_CUDA_ROOT/cuda_cudart/cudart/bin/cudart64_12.dll" "Windows cudart runtime DLL"
    require_file "$WINDOWS_CUDA_ROOT/cuda_nvrtc/nvrtc/bin/nvrtc64_120_0.dll" "Windows NVRTC runtime DLL"
    require_file "$WINDOWS_CUDA_ROOT/cuda_nvrtc/nvrtc/bin/nvrtc-builtins64_129.dll" "Windows NVRTC builtins DLL"
    require_file "$WINDOWS_CUDA_INCLUDE_ROOT/cuda.h" "Windows CUDA header set"
    require_file "$WINDOWS_CUDA_INCLUDE_ROOT/cuda_runtime_api.h" "Windows CUDA runtime header set"
    require_file "$WINDOWS_CUDA_INCLUDE_ROOT/nvrtc.h" "Windows NVRTC header set"

    if not test -f "$LLVM_MINGW_ROOT/x86_64-w64-mingw32/lib/libLIBCMT.a"
        ln -sf libmsvcrt.a "$LLVM_MINGW_ROOT/x86_64-w64-mingw32/lib/libLIBCMT.a"
        if test $status -ne 0
            echo "Failed to create libLIBCMT.a shim." >&2
            exit 1
        end
    end

    if not test -f "$LLVM_MINGW_ROOT/x86_64-w64-mingw32/lib/libOLDNAMES.a"
        "$LLVM_MINGW_ROOT/bin/llvm-ar" rc "$LLVM_MINGW_ROOT/x86_64-w64-mingw32/lib/libOLDNAMES.a"
        if test $status -ne 0
            echo "Failed to create libOLDNAMES.a shim." >&2
            exit 1
        end
    end

    clear_stale_windows_cmake_cache "$native_build_dir"

    echo "windows: cmake configure"
    run_quiet "windows-cmake-configure" cmake -S "$SCRIPT_DIR/native" \
        -B "$native_build_dir" \
        -DCMAKE_BUILD_TYPE="$configuration" \
        -DCMAKE_TOOLCHAIN_FILE="$SCRIPT_DIR/native/toolchains/llvm-mingw-windows-x64.cmake" \
        -DROPTIX_LLVM_MINGW_ROOT="$LLVM_MINGW_ROOT" \
        -DROPTIX_WINDOWS_CUDA_ROOT="$WINDOWS_CUDA_ROOT"

    echo "windows: cmake build"
    run_quiet "windows-cmake-build" cmake --build "$native_build_dir" --config "$configuration" -j4

    set all_runtime_dlls \
        "$WINDOWS_CUDA_ROOT/cuda_cudart/cudart/bin/cudart64_12.dll" \
        "$WINDOWS_CUDA_ROOT/cuda_nvrtc/nvrtc/bin/nvrtc64_120_0.dll" \
        "$WINDOWS_CUDA_ROOT/cuda_nvrtc/nvrtc/bin/nvrtc-builtins64_129.dll" \
        "$LLVM_MINGW_ROOT/x86_64-w64-mingw32/bin/libc++.dll" \
        "$LLVM_MINGW_ROOT/x86_64-w64-mingw32/bin/libunwind.dll"

    set -lx PATH "/usr/bin:/bin:$PATH"
    set -lx HOME "$WINE_HOME_DIR"
    set -lx WINEPREFIX "$WINE_PREFIX_DIR"
    set -lx WINEDLLPATH "$NVIDIA_LIBS_DLL_ROOT:$WINE_NVOPTIX_DLL_ROOT"
    set -lx WINEDEBUG -all
    set -lx WINEDLLOVERRIDES "nvcuda=n,b;nvoptix=n,b;nvapi=n,b;nvapi64=n,b;nvml=n,b"
    set -lx DOTNET_SKIP_FIRST_TIME_EXPERIENCE 1
    set -lx DOTNET_NOLOGO 1
    set -lx DOTNET_GENERATE_ASPNET_CERTIFICATE false
    set -lx DOTNET_CLI_TELEMETRY_OPTOUT 1
    set -lx DOTNET_CLI_HOME "$DOTNET_CLI_HOME_DIR"
    set -lx DOTNET_SYSTEM_GLOBALIZATION_USENLS 1
    set -lx DOTNET_SYSTEM_GLOBALIZATION_INVARIANT 1
    set -lx NUGET_CERT_REVOCATION_MODE offline
    set -lx MSBUILDDISABLENODEREUSE 1

    rm -rf "$windows_msbuild_root/obj" "$windows_publish_dir"

    echo "windows: dotnet restore"
    run_quiet "windows-dotnet-restore" "$wine_runner" "$WINDOWS_DOTNET_EXE" restore -r win-x64 \
        /p:RestoreIgnoreFailedSources=true \
        /p:NuGetAudit=false \
        /p:BaseIntermediateOutputPath="$windows_msbuild_obj_root" \
        /p:MSBuildProjectExtensionsPath="$windows_msbuild_obj_root" \
        /p:IntermediateOutputPath="$windows_msbuild_obj_root$configuration/$TARGET_FRAMEWORK/win-x64/"

    echo "windows: dotnet publish"
    run_quiet "windows-dotnet-publish" "$wine_runner" "$WINDOWS_DOTNET_EXE" publish -c "$configuration" -r win-x64 --no-restore -v:q -nologo \
        /p:RestoreIgnoreFailedSources=true \
        /p:NuGetAudit=false \
        /p:BaseIntermediateOutputPath="$windows_msbuild_obj_root" \
        /p:MSBuildProjectExtensionsPath="$windows_msbuild_obj_root" \
        /p:IntermediateOutputPath="$windows_msbuild_obj_root$configuration/$TARGET_FRAMEWORK/win-x64/" \
        $BUILD_SCRIPT_ARGS

    copy_files_to_dir "$windows_publish_dir" "$windows_native_library_path" $all_runtime_dlls

    if test "$trace_nvcuda" = yes
        echo "windows: run with nvcuda trace"
        run_windows_app "$windows_publish_dir/RayOptix.exe" yes
        exit $status
    end

    if test "$run_after_build" = yes
        echo "windows: run"
        run_windows_app "$windows_publish_dir/RayOptix.exe" "$trace_nvcuda"
        exit $status
    end
end

set BUILD_PLATFORM (resolve_choice BUILD_PLATFORM "Platform" linux windows)
set BUILD_CONFIGURATION_KEY (resolve_choice BUILD_CONFIGURATION "Configuration" debug release)
set BUILD_CONFIGURATION (to_msbuild_configuration "$BUILD_CONFIGURATION_KEY")
set RUN_AFTER_BUILD (resolve_choice RUN_AFTER_BUILD "Run after build" yes no)
set TRACE_NVCUDA no

if test "$BUILD_PLATFORM" = windows
    set TRACE_NVCUDA (resolve_choice TRACE_NVCUDA "Trace nvcuda" no yes)
end

switch "$BUILD_PLATFORM"
    case linux
        build_linux "$BUILD_CONFIGURATION" "$RUN_AFTER_BUILD"
    case windows
        build_windows "$BUILD_CONFIGURATION" "$RUN_AFTER_BUILD" "$TRACE_NVCUDA"
end
