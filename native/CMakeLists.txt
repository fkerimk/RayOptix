cmake_minimum_required(VERSION 3.24)
project(RayOptixNative LANGUAGES CXX)

find_package(CUDAToolkit REQUIRED)
list(GET CUDAToolkit_INCLUDE_DIRS 0 CUDA_INCLUDE_DIR)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

add_library(RayOptixNative SHARED
    src/interop.cpp
)

target_include_directories(RayOptixNative PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../external/optix-sdk/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../external/DLSS/include
    ${CUDAToolkit_INCLUDE_DIRS}
)

target_compile_definitions(RayOptixNative PRIVATE
    ROPTIX_OPTIX_INCLUDE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../external/optix-sdk/include"
    ROPTIX_CUDA_INCLUDE_DIR="${CUDA_INCLUDE_DIR}"
)

target_link_libraries(RayOptixNative PRIVATE
    CUDA::cudart
    CUDA::cuda_driver
    CUDA::nvrtc
    ${CMAKE_CURRENT_SOURCE_DIR}/../external/DLSS/lib/Linux_x86_64/libnvsdk_ngx.a
    dl
)
