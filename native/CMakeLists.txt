cmake_minimum_required(VERSION 3.24)
project(RayOptixNative LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if (WIN32 AND NOT CMAKE_HOST_WIN32)
    if (NOT DEFINED ROPTIX_WINDOWS_CUDA_ROOT)
        if (DEFINED ENV{ROPTIX_WINDOWS_CUDA_ROOT})
            set(ROPTIX_WINDOWS_CUDA_ROOT "$ENV{ROPTIX_WINDOWS_CUDA_ROOT}")
        else()
            message(FATAL_ERROR "ROPTIX_WINDOWS_CUDA_ROOT must point to the extracted Windows CUDA toolkit root when cross-compiling from Linux.")
        endif()
    endif()

    set(ROPTIX_CUDA_INCLUDE_DIRS
        "${ROPTIX_WINDOWS_CUDA_ROOT}/cuda_cudart/cudart/include"
        "${ROPTIX_WINDOWS_CUDA_ROOT}/cuda_nvrtc/nvrtc_dev/include"
        "${ROPTIX_WINDOWS_CUDA_ROOT}/cuda_cccl/thrust/include"
    )
    set(CUDA_INCLUDE_DIR "${ROPTIX_WINDOWS_CUDA_ROOT}/cuda_cudart/cudart/include")
    set(ROPTIX_CUDA_LIBRARIES
        "${ROPTIX_WINDOWS_CUDA_ROOT}/cuda_cudart/cudart/lib/x64/cudart.lib"
        "${ROPTIX_WINDOWS_CUDA_ROOT}/cuda_cudart/cudart/lib/x64/cuda.lib"
        "${ROPTIX_WINDOWS_CUDA_ROOT}/cuda_nvrtc/nvrtc_dev/lib/x64/nvrtc.lib"
    )
else()
    find_package(CUDAToolkit REQUIRED)
    set(ROPTIX_CUDA_INCLUDE_DIRS ${CUDAToolkit_INCLUDE_DIRS})
    list(GET CUDAToolkit_INCLUDE_DIRS 0 CUDA_INCLUDE_DIR)
    set(ROPTIX_CUDA_LIBRARIES
        CUDA::cudart
        CUDA::cuda_driver
        CUDA::nvrtc
    )
endif()

if (WIN32)
    set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()

# Read CUDA kernel sources for NVRTC runtime embedding
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/src/optix_kernels.cu" ROPTIX_OPTIX_KERNEL_SOURCE)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/src/present_kernels.cu" ROPTIX_PRESENT_KERNEL_SOURCE)

# Generate native_device_sources.h from template
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/native_device_sources.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/native_device_sources.h"
    @ONLY
)

add_library(RayOptixNative SHARED
    src/interop.cpp
    src/native_exports.cpp
    src/optix_kernels.cu
    src/present_kernels.cu
)

# Mark .cu files as header-only (not compiled by host compiler, used by NVRTC at runtime)
set_source_files_properties(
    src/optix_kernels.cu
    src/present_kernels.cu
    PROPERTIES HEADER_FILE_ONLY TRUE
)
set_target_properties(RayOptixNative PROPERTIES PREFIX "")

if (WIN32)
    target_sources(RayOptixNative PRIVATE
        src/windows_msvc_compat.cpp
    )
endif()

target_include_directories(RayOptixNative PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../external/optix-dev/include
    ${ROPTIX_CUDA_INCLUDE_DIRS}
    ${CMAKE_CURRENT_BINARY_DIR}/generated
)

target_compile_definitions(RayOptixNative PRIVATE
    ROPTIX_OPTIX_INCLUDE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../external/optix-dev/include"
    ROPTIX_CUDA_INCLUDE_DIR="${CUDA_INCLUDE_DIR}"
)

target_link_libraries(RayOptixNative PRIVATE
    ${ROPTIX_CUDA_LIBRARIES}
)

if (WIN32)
    target_link_libraries(RayOptixNative PRIVATE
        opengl32
        cfgmgr32
    )
else()
    target_link_libraries(RayOptixNative PRIVATE
        dl
    )
endif()
