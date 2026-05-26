#include <cuda.h>
#include <cuda_gl_interop.h>
#include <cuda_runtime.h>
#include <nvrtc.h>
#include <optix.h>
#include <optix_function_table_definition.h>
#include <optix_stubs.h>
#include <optix_stack_size.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <memory>
#include <chrono>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <GL/gl.h>
#elif defined(__linux__)
#include <GL/gl.h>
#endif

#include "native_api.h"
#include "native_device_sources.h"

namespace {

struct Float3 {
    float x;
    float y;
    float z;
};

struct LaunchParams {
    float4* beauty;
    float4* accumulation;
    float* depth;
    float4* worldPosition;
    float4* normals;
    float* roughness;
    float4* diffuseAlbedo;
    float4* specularAlbedo;
    CUdeviceptr materials;
    CUdeviceptr texturePixels;
    CUdeviceptr textureMetadata;
    unsigned int textureCount;
    unsigned int imageWidth;
    unsigned int imageHeight;
    Float3 cameraPosition;
    Float3 cameraForward;
    Float3 cameraRight;
    Float3 cameraUp;
    float tanHalfFovY;
    NativeRenderSettings settings;
    OptixTraversableHandle iasHandle;
    unsigned int frameIndex;
    float currentViewProjection[16];
};

struct MissData {
    Float3 skyTop;
    Float3 skyBottom;
};

struct HitGroupData {
    CUdeviceptr vertices;
    CUdeviceptr normals;
    CUdeviceptr texCoords;
    CUdeviceptr indices;
    CUdeviceptr materials;
};

struct MeshGpuData {
    CUdeviceptr vertexBuffer = 0;
    CUdeviceptr normalBuffer = 0;
    CUdeviceptr texCoordBuffer = 0;
    CUdeviceptr indexBuffer = 0;
    CUdeviceptr gasOutputBuffer = 0;
    OptixTraversableHandle gasHandle = 0;
    size_t vertexCapacity = 0;
    size_t normalCapacity = 0;
    size_t texCoordCapacity = 0;
    size_t indexCapacity = 0;
    size_t gasOutputCapacity = 0;
    unsigned int vertexCount = 0;
    unsigned int triangleCount = 0;
};

struct TriangleIndices {
    uint32_t x;
    uint32_t y;
    uint32_t z;
};

static_assert(sizeof(TriangleIndices) == 12, "TriangleIndices must stay tightly packed.");

template <typename T>
struct alignas(OPTIX_SBT_RECORD_ALIGNMENT) SbtRecord {
    char header[OPTIX_SBT_RECORD_HEADER_SIZE];
    T data;
};

struct RayGenData {
};

struct EmptyData {
};

using RayGenRecord = SbtRecord<RayGenData>;
using MissRecord = SbtRecord<MissData>;
using HitGroupRecord = SbtRecord<HitGroupData>;

class OptixError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

#if defined(_WIN32)
std::string GetBootstrapLogPath() {
    char modulePath[MAX_PATH]{};
    const DWORD length = GetModuleFileNameA(nullptr, modulePath, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return "rayoptix_native_boot.txt";
    }

    std::string path(modulePath, modulePath + length);
    const size_t slash = path.find_last_of("\\/");
    if (slash == std::string::npos) {
        return "rayoptix_native_boot.txt";
    }

    path.resize(slash + 1);
    path += "rayoptix_native_boot.txt";
    return path;
}

std::string DescribeLastWindowsError(const char* libraryName) {
    const DWORD errorCode = GetLastError();
    char* messageBuffer = nullptr;
    const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
    const DWORD length = FormatMessageA(
        flags,
        nullptr,
        errorCode,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPSTR>(&messageBuffer),
        0,
        nullptr);

    std::string message;
    if (length != 0 && messageBuffer != nullptr) {
        message.assign(messageBuffer, messageBuffer + length);
        while (!message.empty() && (message.back() == '\r' || message.back() == '\n' || message.back() == ' ' || message.back() == '\t')) {
            message.pop_back();
        }
    } else {
        message = "unknown Windows loader error";
    }

    if (messageBuffer != nullptr) {
        LocalFree(messageBuffer);
    }

    std::ostringstream stream;
    stream << libraryName << " could not be loaded (Win32 error " << static_cast<unsigned long>(errorCode) << ": " << message << ")";
    return stream.str();
}

void EnsureWindowsOptixRuntimeAvailable() {
    if (LoadLibraryA("nvcuda.dll") == nullptr) {
        throw OptixError(DescribeLastWindowsError("nvcuda.dll"));
    }

    if (LoadLibraryA("nvoptix.dll") == nullptr) {
        throw OptixError(DescribeLastWindowsError("nvoptix.dll"));
    }
}

void AppendBootstrapLog(const char* message) {
    OutputDebugStringA(message);
    OutputDebugStringA("\n");

    const std::string logPath = GetBootstrapLogPath();
    if (FILE* file = std::fopen(logPath.c_str(), "a")) {
        std::fprintf(file, "%s\n", message);
        std::fclose(file);
    }
}
#endif

inline void CheckCuda(cudaError_t result, const char* call) {
    if (result != cudaSuccess) {
        throw OptixError(std::string(call) + " failed: " + cudaGetErrorString(result));
    }
}

inline void CheckOptix(OptixResult result, const char* call) {
    if (result != OPTIX_SUCCESS) {
        std::ostringstream stream;
        stream << call << " failed with OptiX code " << static_cast<int>(result);
        throw OptixError(stream.str());
    }
}

inline void CheckNvrtc(nvrtcResult result, const char* call) {
    if (result != NVRTC_SUCCESS) {
        throw OptixError(std::string(call) + " failed: " + nvrtcGetErrorString(result));
    }
}

inline void CheckCudaDriver(CUresult result, const char* call) {
    if (result != CUDA_SUCCESS) {
        const char* errorName = nullptr;
        const char* errorString = nullptr;
        cuGetErrorName(result, &errorName);
        cuGetErrorString(result, &errorString);
        throw OptixError(std::string(call) + " failed: " +
            (errorName != nullptr ? errorName : "unknown") + " / " +
            (errorString != nullptr ? errorString : "unknown"));
    }
}

inline void IgnoreCuda(cudaError_t result) {
    (void)result;
}

inline void IgnoreCudaDriver(CUresult result) {
    (void)result;
}


class NativeRenderer {
public:
    NativeRenderer(int renderWidth, int renderHeight, int outputWidth, int outputHeight) {
        InitializeOptix();
        CreateDenoiser();
        CreatePipeline();
        CreateSbt();
        Resize(renderWidth, renderHeight, outputWidth, outputHeight);
    }

    ~NativeRenderer() {
        Destroy();
    }

    void ReleaseOutputTextureForInterop(unsigned int textureId) {
        ReleaseOutputTexture(textureId);
    }

    void Resize(int renderWidth, int renderHeight, int outputWidth, int outputHeight) {
        if (renderWidth <= 0 || renderHeight <= 0 || outputWidth <= 0 || outputHeight <= 0) {
            throw OptixError("Invalid render size.");
        }

        if (renderWidth == width_ &&
            renderHeight == height_ &&
            outputWidth == outputWidth_ &&
            outputHeight == outputHeight_ &&
            beautyBuffer_ != 0) {
            return;
        }

        width_ = renderWidth;
        height_ = renderHeight;
        outputWidth_ = outputWidth;
        outputHeight_ = outputHeight;

        if (beautyBuffer_ != 0) {
            CheckCuda(cudaFree(reinterpret_cast<void*>(beautyBuffer_)), "cudaFree(beautyBuffer)");
            beautyBuffer_ = 0;
        }

        const auto beautySize = static_cast<size_t>(width_) * static_cast<size_t>(height_) * sizeof(float4);
        CheckCuda(cudaMalloc(reinterpret_cast<void**>(&beautyBuffer_), beautySize), "cudaMalloc(beautyBuffer)");

        if (denoisedBeautyBuffer_ != 0) {
            CheckCuda(cudaFree(reinterpret_cast<void*>(denoisedBeautyBuffer_)), "cudaFree(denoisedBeautyBuffer)");
            denoisedBeautyBuffer_ = 0;
        }

        CheckCuda(cudaMalloc(reinterpret_cast<void**>(&denoisedBeautyBuffer_), beautySize), "cudaMalloc(denoisedBeautyBuffer)");

        if (accumulationBuffer_ != 0) {
            CheckCuda(cudaFree(reinterpret_cast<void*>(accumulationBuffer_)), "cudaFree(accumulationBuffer)");
            accumulationBuffer_ = 0;
        }

        const auto accumulationSize = static_cast<size_t>(width_) * static_cast<size_t>(height_) * sizeof(float4);
        CheckCuda(cudaMalloc(reinterpret_cast<void**>(&accumulationBuffer_), accumulationSize), "cudaMalloc(accumulationBuffer)");
        CheckCuda(cudaMemset(reinterpret_cast<void*>(accumulationBuffer_), 0, accumulationSize), "cudaMemset(accumulationBuffer)");
        hasValidDenoisedBeauty_ = false;
        presentFrameIndex_ = 0;

        RecreateGuideBuffers();

        SetupDenoiser();
    }



private:
    void InitializeOptix() {
#if defined(_WIN32)
        AppendBootstrapLog("[OptiX] bootstrap: preflight");
        EnsureWindowsOptixRuntimeAvailable();
        AppendBootstrapLog("[OptiX] bootstrap: preflight-ok");
#endif
        int deviceCount = 0;
#if defined(_WIN32)
        AppendBootstrapLog("[OptiX] bootstrap: cuInit");
#endif
        CheckCudaDriver(cuInit(0), "cuInit");
#if defined(_WIN32)
        AppendBootstrapLog("[OptiX] bootstrap: cuDeviceGetCount");
#endif
        CheckCudaDriver(cuDeviceGetCount(&deviceCount), "cuDeviceGetCount");
        if (deviceCount <= 0) {
            throw OptixError("No CUDA-capable NVIDIA device is available for OptiX.");
        }

#if defined(_WIN32)
        AppendBootstrapLog("[OptiX] bootstrap: cuDeviceGet");
#endif
        CheckCudaDriver(cuDeviceGet(&cudaDevice_, 0), "cuDeviceGet");
#if defined(_WIN32)
        AppendBootstrapLog("[OptiX] bootstrap: cuCtxCreate");
#endif
#if defined(_WIN32)
        CheckCudaDriver(cuCtxCreate(&cudaContext_, 0, cudaDevice_), "cuCtxCreate");
#else
        CUctxCreateParams ctxCreateParams{};
        CheckCudaDriver(cuCtxCreate(&cudaContext_, &ctxCreateParams, 0, cudaDevice_), "cuCtxCreate");
#endif
#if defined(_WIN32)
        AppendBootstrapLog("[OptiX] bootstrap: optixInit");
#endif
        CheckOptix(optixInit(), "optixInit");

        OptixDeviceContextOptions options{};
        options.logCallbackLevel = 4;
        options.logCallbackFunction = &LogCallback;

#if defined(_WIN32)
        AppendBootstrapLog("[OptiX] bootstrap: optixDeviceContextCreate");
#endif
        CheckOptix(optixDeviceContextCreate(cudaContext_, &options, &context_), "optixDeviceContextCreate");
#if defined(_WIN32)
        AppendBootstrapLog("[OptiX] bootstrap: cuStreamCreate");
#endif
        CheckCudaDriver(cuStreamCreate(reinterpret_cast<CUstream*>(&stream_), CU_STREAM_DEFAULT), "cuStreamCreate");
#if defined(_WIN32)
        AppendBootstrapLog("[OptiX] bootstrap: init-ok");
#endif
    }

    void CreateDenoiser() {
        OptixDenoiserOptions denoiserOptions{};
        denoiserOptions.guideAlbedo = 1;
        denoiserOptions.guideNormal = 1;
        denoiserOptions.denoiseAlpha = OPTIX_DENOISER_ALPHA_MODE_COPY;

        CheckOptix(
            optixDenoiserCreate(context_, OPTIX_DENOISER_MODEL_KIND_HDR, &denoiserOptions, &denoiser_),
            "optixDenoiserCreate");
    }

    void SetupDenoiser() {
        if (denoiser_ == nullptr) {
            return;
        }

        CheckOptix(
            optixDenoiserComputeMemoryResources(denoiser_, width_, height_, &denoiserSizes_),
            "optixDenoiserComputeMemoryResources");

        EnsureBuffer(denoiserStateBuffer_, denoiserStateCapacity_, denoiserSizes_.stateSizeInBytes);
        EnsureBuffer(denoiserScratchBuffer_, denoiserScratchCapacity_, denoiserSizes_.withoutOverlapScratchSizeInBytes);

        CheckOptix(
            optixDenoiserSetup(
                denoiser_,
                stream_,
                width_,
                height_,
                denoiserStateBuffer_,
                denoiserSizes_.stateSizeInBytes,
                denoiserScratchBuffer_,
                denoiserSizes_.withoutOverlapScratchSizeInBytes),
            "optixDenoiserSetup");

        CheckCuda(cudaStreamSynchronize(stream_), "cudaStreamSynchronize(denoiserSetup)");
    }

    CUdeviceptr DenoiseBeauty() {
        OptixImage2D inputLayer{};
        inputLayer.data = beautyBuffer_;
        inputLayer.width = static_cast<unsigned int>(width_);
        inputLayer.height = static_cast<unsigned int>(height_);
        inputLayer.rowStrideInBytes = static_cast<unsigned int>(width_ * static_cast<int>(sizeof(float4)));
        inputLayer.pixelStrideInBytes = sizeof(float4);
        inputLayer.format = OPTIX_PIXEL_FORMAT_FLOAT4;

        OptixImage2D outputLayer = inputLayer;
        outputLayer.data = denoisedBeautyBuffer_;

        OptixDenoiserLayer denoiserLayer{};
        denoiserLayer.input = inputLayer;
        denoiserLayer.output = outputLayer;

        OptixDenoiserGuideLayer guideLayer{};
        guideLayer.albedo.data = diffuseAlbedoGuideBuffer_;
        guideLayer.albedo.width = static_cast<unsigned int>(width_);
        guideLayer.albedo.height = static_cast<unsigned int>(height_);
        guideLayer.albedo.rowStrideInBytes = static_cast<unsigned int>(width_ * static_cast<int>(sizeof(float4)));
        guideLayer.albedo.pixelStrideInBytes = sizeof(float4);
        guideLayer.albedo.format = OPTIX_PIXEL_FORMAT_FLOAT4;

        guideLayer.normal.data = normalGuideBuffer_;
        guideLayer.normal.width = static_cast<unsigned int>(width_);
        guideLayer.normal.height = static_cast<unsigned int>(height_);
        guideLayer.normal.rowStrideInBytes = static_cast<unsigned int>(width_ * static_cast<int>(sizeof(float4)));
        guideLayer.normal.pixelStrideInBytes = sizeof(float4);
        guideLayer.normal.format = OPTIX_PIXEL_FORMAT_FLOAT4;

        OptixDenoiserParams denoiserParams{};
        denoiserParams.hdrIntensity = 0;
        denoiserParams.blendFactor = 0.0f;

        CheckOptix(
            optixDenoiserInvoke(
                denoiser_,
                stream_,
                &denoiserParams,
                denoiserStateBuffer_,
                denoiserSizes_.stateSizeInBytes,
                &guideLayer,
                &denoiserLayer,
                1,
                0,
                0,
                denoiserScratchBuffer_,
                denoiserSizes_.withoutOverlapScratchSizeInBytes),
            "optixDenoiserInvoke");

        CheckCuda(cudaStreamSynchronize(stream_), "cudaStreamSynchronize(denoiser)");
        return denoisedBeautyBuffer_;
    }

    void EnsureOutputTextureInterop(unsigned int textureId) {
        if (textureInteropResource_ != nullptr && registeredOutputTextureId_ == textureId) {
            return;
        }

        ReleaseOutputTexture();

        CheckCuda(
            cudaGraphicsGLRegisterImage(
                &textureInteropResource_,
                textureId,
                GL_TEXTURE_2D,
                cudaGraphicsRegisterFlagsSurfaceLoadStore),
            "cudaGraphicsGLRegisterImage");

        registeredOutputTextureId_ = textureId;
    }

    void ReleaseOutputTexture() {
        if (textureInteropResource_ == nullptr) {
            registeredOutputTextureId_ = 0;
            return;
        }

        IgnoreCuda(cudaGraphicsUnregisterResource(textureInteropResource_));
        textureInteropResource_ = nullptr;
        registeredOutputTextureId_ = 0;
    }

    void ReleaseOutputTexture(unsigned int textureId) {
        if (textureId != 0 && textureId != registeredOutputTextureId_) {
            return;
        }

        ReleaseOutputTexture();
    }

    void PresentToOutputTexture(CUdeviceptr hdrSource, const NativeRenderSettings& settings) {
        if (textureInteropResource_ == nullptr) {
            throw OptixError("Output texture interop is not initialized.");
        }

        CheckCuda(cudaGraphicsMapResources(1, &textureInteropResource_, stream_), "cudaGraphicsMapResources");

        cudaArray_t outputArray = nullptr;
        CheckCuda(cudaGraphicsSubResourceGetMappedArray(&outputArray, textureInteropResource_, 0, 0), "cudaGraphicsSubResourceGetMappedArray");

        cudaResourceDesc resourceDesc{};
        resourceDesc.resType = cudaResourceTypeArray;
        resourceDesc.res.array.array = outputArray;

        cudaSurfaceObject_t surfaceObject = 0;
        CheckCuda(cudaCreateSurfaceObject(&surfaceObject, &resourceDesc), "cudaCreateSurfaceObject");

        float inverseGamma = 1.0f / std::max(settings.gamma, 0.001f);
        float exposure = std::max(settings.exposure, 0.001f);
        unsigned int sourceWidth = static_cast<unsigned int>(width_);
        unsigned int sourceHeight = static_cast<unsigned int>(height_);
        unsigned int outputWidth = static_cast<unsigned int>(outputWidth_);
        unsigned int outputHeight = static_cast<unsigned int>(outputHeight_);
        unsigned long long surfaceHandle = static_cast<unsigned long long>(surfaceObject);

        void* kernelArgs[] = {
            &hdrSource,
            &surfaceHandle,
            &sourceWidth,
            &sourceHeight,
            &outputWidth,
            &outputHeight,
            &exposure,
            &inverseGamma
        };

        constexpr unsigned int blockSizeX = 16;
        constexpr unsigned int blockSizeY = 16;
        const unsigned int gridSizeX = (outputWidth + blockSizeX - 1) / blockSizeX;
        const unsigned int gridSizeY = (outputHeight + blockSizeY - 1) / blockSizeY;

        CheckCudaDriver(
            cuLaunchKernel(
                toneMapKernel_,
                gridSizeX,
                gridSizeY,
                1,
                blockSizeX,
                blockSizeY,
                1,
                0,
                stream_,
                kernelArgs,
                nullptr),
            "cuLaunchKernel(ToneMapToSurface)");

        CheckCuda(cudaDestroySurfaceObject(surfaceObject), "cudaDestroySurfaceObject");
        CheckCuda(cudaGraphicsUnmapResources(1, &textureInteropResource_, stream_), "cudaGraphicsUnmapResources");
        CheckCuda(cudaStreamSynchronize(stream_), "cudaStreamSynchronize(present)");
    }

    void RecreateGuideBuffers() {
        SafeCudaFree(depthBuffer_);
        SafeCudaFree(worldPositionBuffer_);
        SafeCudaFree(normalGuideBuffer_);
        SafeCudaFree(roughnessGuideBuffer_);
        SafeCudaFree(diffuseAlbedoGuideBuffer_);
        SafeCudaFree(specularAlbedoGuideBuffer_);
        const auto depthSize = static_cast<size_t>(width_) * static_cast<size_t>(height_) * sizeof(float);
        const auto worldPositionSize = static_cast<size_t>(width_) * static_cast<size_t>(height_) * sizeof(float4);
        const auto normalSize = static_cast<size_t>(width_) * static_cast<size_t>(height_) * sizeof(float4);
        const auto roughnessSize = static_cast<size_t>(width_) * static_cast<size_t>(height_) * sizeof(float);
        const auto albedoSize = static_cast<size_t>(width_) * static_cast<size_t>(height_) * sizeof(float4);
        CheckCuda(cudaMalloc(reinterpret_cast<void**>(&depthBuffer_), depthSize), "cudaMalloc(depthBuffer)");
        CheckCuda(cudaMalloc(reinterpret_cast<void**>(&worldPositionBuffer_), worldPositionSize), "cudaMalloc(worldPositionBuffer)");
        CheckCuda(cudaMalloc(reinterpret_cast<void**>(&normalGuideBuffer_), normalSize), "cudaMalloc(normalGuideBuffer)");
        CheckCuda(cudaMalloc(reinterpret_cast<void**>(&roughnessGuideBuffer_), roughnessSize), "cudaMalloc(roughnessGuideBuffer)");
        CheckCuda(cudaMalloc(reinterpret_cast<void**>(&diffuseAlbedoGuideBuffer_), albedoSize), "cudaMalloc(diffuseAlbedoGuideBuffer)");
        CheckCuda(cudaMalloc(reinterpret_cast<void**>(&specularAlbedoGuideBuffer_), albedoSize), "cudaMalloc(specularAlbedoGuideBuffer)");
    }
    void BuildViewProjectionMatrix(const NativeCamera& camera, float* current) {
        const auto eye = Float3{camera.positionX, camera.positionY, camera.positionZ};
        const auto target = Float3{camera.targetX, camera.targetY, camera.targetZ};
        const auto forward = Normalize(Subtract(target, eye));
        const auto right = Normalize(Cross(forward, Float3{0.0f, 1.0f, 0.0f}));
        const auto up = Normalize(Cross(right, forward));

        const auto aspect = static_cast<float>(width_) / static_cast<float>(height_);
        const auto tanHalfFov = std::tan(camera.fovY * 0.5f * 3.14159265359f / 180.0f);
        const auto nearPlane = 0.01f;
        const auto farPlane = 1000.0f;

        float view[16] = {
            right.x, right.y, right.z, -(right.x * eye.x + right.y * eye.y + right.z * eye.z),
            up.x, up.y, up.z, -(up.x * eye.x + up.y * eye.y + up.z * eye.z),
            -forward.x, -forward.y, -forward.z, (forward.x * eye.x + forward.y * eye.y + forward.z * eye.z),
            0.0f, 0.0f, 0.0f, 1.0f
        };

        float projection[16] = {
            1.0f / (aspect * tanHalfFov), 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f / tanHalfFov, 0.0f, 0.0f,
            0.0f, 0.0f, farPlane / (nearPlane - farPlane), (farPlane * nearPlane) / (nearPlane - farPlane),
            0.0f, 0.0f, -1.0f, 0.0f
        };

        MultiplyMatrices(projection, view, current);
        std::memcpy(previousViewProjection_, current, sizeof(previousViewProjection_));
    }

    void CreatePipeline() {
        const auto ptx = CompileOptixPtx();
        CreateCudaModule(CompilePresentPtx());

        OptixModuleCompileOptions moduleCompileOptions{};
        moduleCompileOptions.maxRegisterCount = OPTIX_COMPILE_DEFAULT_MAX_REGISTER_COUNT;
        moduleCompileOptions.optLevel = OPTIX_COMPILE_OPTIMIZATION_DEFAULT;
        moduleCompileOptions.debugLevel = OPTIX_COMPILE_DEBUG_LEVEL_MINIMAL;

        OptixPipelineCompileOptions pipelineCompileOptions{};
        pipelineCompileOptions.usesMotionBlur = 0;
        pipelineCompileOptions.traversableGraphFlags = OPTIX_TRAVERSABLE_GRAPH_FLAG_ALLOW_SINGLE_LEVEL_INSTANCING;
        pipelineCompileOptions.numPayloadValues = 2;
        pipelineCompileOptions.numAttributeValues = 2;
        pipelineCompileOptions.exceptionFlags = OPTIX_EXCEPTION_FLAG_NONE;
        pipelineCompileOptions.pipelineLaunchParamsVariableName = "params";
        pipelineCompileOptions.pipelineLaunchParamsSizeInBytes = sizeof(LaunchParams);
        pipelineCompileOptions.usesPrimitiveTypeFlags = OPTIX_PRIMITIVE_TYPE_FLAGS_TRIANGLE;

        char log[4096];
        size_t logSize = sizeof(log);
        CheckOptix(optixModuleCreate(
            context_,
            &moduleCompileOptions,
            &pipelineCompileOptions,
            ptx.c_str(),
            ptx.size(),
            log,
            &logSize,
            &module_), "optixModuleCreate");

        OptixProgramGroupOptions programGroupOptions{};

        OptixProgramGroupDesc raygenDesc{};
        raygenDesc.kind = OPTIX_PROGRAM_GROUP_KIND_RAYGEN;
        raygenDesc.raygen.module = module_;
        raygenDesc.raygen.entryFunctionName = "__raygen__rg";
        logSize = sizeof(log);
        CheckOptix(optixProgramGroupCreate(context_, &raygenDesc, 1, &programGroupOptions, log, &logSize, &raygenProgramGroup_), "optixProgramGroupCreate(raygen)");

        OptixProgramGroupDesc missDesc{};
        missDesc.kind = OPTIX_PROGRAM_GROUP_KIND_MISS;
        missDesc.miss.module = module_;
        missDesc.miss.entryFunctionName = "__miss__ms";
        logSize = sizeof(log);
        CheckOptix(optixProgramGroupCreate(context_, &missDesc, 1, &programGroupOptions, log, &logSize, &missProgramGroup_), "optixProgramGroupCreate(miss)");

        OptixProgramGroupDesc hitDesc{};
        hitDesc.kind = OPTIX_PROGRAM_GROUP_KIND_HITGROUP;
        hitDesc.hitgroup.moduleCH = module_;
        hitDesc.hitgroup.entryFunctionNameCH = "__closesthit__ch";
        logSize = sizeof(log);
        CheckOptix(optixProgramGroupCreate(context_, &hitDesc, 1, &programGroupOptions, log, &logSize, &hitgroupProgramGroup_), "optixProgramGroupCreate(hitgroup)");

        std::array<OptixProgramGroup, 3> programGroups = {
            raygenProgramGroup_,
            missProgramGroup_,
            hitgroupProgramGroup_,
        };

        OptixPipelineLinkOptions linkOptions{};
        linkOptions.maxTraceDepth = 1;

        logSize = sizeof(log);
        CheckOptix(optixPipelineCreate(
            context_,
            &pipelineCompileOptions,
            &linkOptions,
            programGroups.data(),
            static_cast<unsigned int>(programGroups.size()),
            log,
            &logSize,
            &pipeline_), "optixPipelineCreate");

        OptixStackSizes stackSizes{};
        CheckOptix(optixUtilAccumulateStackSizes(raygenProgramGroup_, &stackSizes, pipeline_), "optixUtilAccumulateStackSizes(raygen)");
        CheckOptix(optixUtilAccumulateStackSizes(missProgramGroup_, &stackSizes, pipeline_), "optixUtilAccumulateStackSizes(miss)");
        CheckOptix(optixUtilAccumulateStackSizes(hitgroupProgramGroup_, &stackSizes, pipeline_), "optixUtilAccumulateStackSizes(hitgroup)");

        unsigned int directCallableStackTraversal = 0;
        unsigned int directCallableStackState = 0;
        unsigned int continuationStack = 0;
        CheckOptix(optixUtilComputeStackSizes(&stackSizes, 1, 0, 0, &directCallableStackTraversal, &directCallableStackState, &continuationStack), "optixUtilComputeStackSizes");
        CheckOptix(optixPipelineSetStackSize(pipeline_, directCallableStackTraversal, directCallableStackState, continuationStack, 2), "optixPipelineSetStackSize");
    }

    void CreateCudaModule(const std::string& ptx) {
        CheckCudaDriver(cuModuleLoadDataEx(&cudaModule_, ptx.c_str(), 0, nullptr, nullptr), "cuModuleLoadDataEx");
        CheckCudaDriver(cuModuleGetFunction(&toneMapKernel_, cudaModule_, "ToneMapToSurface"), "cuModuleGetFunction(ToneMapToSurface)");
    }

    void CreateSbt() {
        RayGenRecord raygenRecord{};
        CheckOptix(optixSbtRecordPackHeader(raygenProgramGroup_, &raygenRecord), "optixSbtRecordPackHeader(raygen)");
        CheckCuda(cudaMalloc(reinterpret_cast<void**>(&raygenRecordBuffer_), sizeof(raygenRecord)), "cudaMalloc(raygenRecord)");
        CheckCuda(cudaMemcpy(reinterpret_cast<void*>(raygenRecordBuffer_), &raygenRecord, sizeof(raygenRecord), cudaMemcpyHostToDevice), "cudaMemcpy(raygenRecord)");

        MissRecord missRecord{};
        missRecord.data.skyTop = Float3{0.55f, 0.72f, 0.95f};
        missRecord.data.skyBottom = Float3{0.95f, 0.97f, 1.0f};
        CheckOptix(optixSbtRecordPackHeader(missProgramGroup_, &missRecord), "optixSbtRecordPackHeader(miss)");
        CheckCuda(cudaMalloc(reinterpret_cast<void**>(&missRecordBuffer_), sizeof(missRecord)), "cudaMalloc(missRecord)");
        CheckCuda(cudaMemcpy(reinterpret_cast<void*>(missRecordBuffer_), &missRecord, sizeof(missRecord), cudaMemcpyHostToDevice), "cudaMemcpy(missRecord)");

        CheckCuda(cudaMalloc(reinterpret_cast<void**>(&launchParamsBuffer_), sizeof(LaunchParams)), "cudaMalloc(launchParamsBuffer)");

        sbt_.raygenRecord = raygenRecordBuffer_;
        sbt_.missRecordBase = missRecordBuffer_;
        sbt_.missRecordStrideInBytes = sizeof(MissRecord);
        sbt_.missRecordCount = 1;
        sbt_.hitgroupRecordBase = 0;
        sbt_.hitgroupRecordStrideInBytes = sizeof(HitGroupRecord);
        sbt_.hitgroupRecordCount = 0;
    }

public:
    int UploadMesh(
        int meshId,
        const float* vertices,
        unsigned int vertexCount,
        const float* normals,
        const float* texCoords,
        const uint32_t* indices,
        unsigned int triangleCount) {
        const auto vertexBytes = static_cast<size_t>(vertexCount) * sizeof(float3);
        const auto normalBytes = static_cast<size_t>(vertexCount) * sizeof(float3);
        const auto texCoordBytes = static_cast<size_t>(vertexCount) * sizeof(float2);
        const auto indexBytes = static_cast<size_t>(triangleCount) * sizeof(TriangleIndices);

        if (static_cast<size_t>(meshId) >= meshes_.size()) {
            meshes_.resize(static_cast<size_t>(meshId) + 1);
        }

        auto& mesh = meshes_[static_cast<size_t>(meshId)];
        EnsureBuffer(mesh.vertexBuffer, mesh.vertexCapacity, vertexBytes);
        EnsureBuffer(mesh.normalBuffer, mesh.normalCapacity, normalBytes);
        EnsureBuffer(mesh.texCoordBuffer, mesh.texCoordCapacity, texCoordBytes);
        EnsureBuffer(mesh.indexBuffer, mesh.indexCapacity, indexBytes);

        CheckCuda(cudaMemcpy(reinterpret_cast<void*>(mesh.vertexBuffer), vertices, vertexBytes, cudaMemcpyHostToDevice), "cudaMemcpy(meshVertices)");
        CheckCuda(cudaMemcpy(reinterpret_cast<void*>(mesh.normalBuffer), normals, normalBytes, cudaMemcpyHostToDevice), "cudaMemcpy(meshNormals)");
        CheckCuda(cudaMemcpy(reinterpret_cast<void*>(mesh.texCoordBuffer), texCoords, texCoordBytes, cudaMemcpyHostToDevice), "cudaMemcpy(meshTexCoords)");
        CheckCuda(cudaMemcpy(reinterpret_cast<void*>(mesh.indexBuffer), indices, indexBytes, cudaMemcpyHostToDevice), "cudaMemcpy(meshIndices)");

        mesh.vertexCount = vertexCount;
        mesh.triangleCount = triangleCount;

        const uint32_t flags[] = {OPTIX_GEOMETRY_FLAG_NONE};
        OptixBuildInput buildInput{};
        buildInput.type = OPTIX_BUILD_INPUT_TYPE_TRIANGLES;
        buildInput.triangleArray.vertexFormat = OPTIX_VERTEX_FORMAT_FLOAT3;
        buildInput.triangleArray.vertexStrideInBytes = sizeof(float3);
        buildInput.triangleArray.numVertices = vertexCount;
        buildInput.triangleArray.vertexBuffers = &mesh.vertexBuffer;
        buildInput.triangleArray.indexFormat = OPTIX_INDICES_FORMAT_UNSIGNED_INT3;
        buildInput.triangleArray.indexStrideInBytes = sizeof(TriangleIndices);
        buildInput.triangleArray.numIndexTriplets = triangleCount;
        buildInput.triangleArray.indexBuffer = mesh.indexBuffer;
        buildInput.triangleArray.flags = flags;
        buildInput.triangleArray.numSbtRecords = 1;

        OptixAccelBuildOptions accelOptions{};
        accelOptions.buildFlags = OPTIX_BUILD_FLAG_ALLOW_COMPACTION | OPTIX_BUILD_FLAG_ALLOW_UPDATE;
        accelOptions.operation = OPTIX_BUILD_OPERATION_BUILD;

        OptixAccelBufferSizes sizes{};
        CheckOptix(optixAccelComputeMemoryUsage(context_, &accelOptions, &buildInput, 1, &sizes), "optixAccelComputeMemoryUsage(mesh)");

        EnsureBuffer(gasScratchBuffer_, gasScratchCapacity_, sizes.tempSizeInBytes);
        EnsureBuffer(mesh.gasOutputBuffer, mesh.gasOutputCapacity, sizes.outputSizeInBytes);

        OptixTraversableHandle newHandle = 0;
        CheckOptix(optixAccelBuild(
            context_, stream_, &accelOptions, &buildInput, 1,
            gasScratchBuffer_, sizes.tempSizeInBytes,
            mesh.gasOutputBuffer, sizes.outputSizeInBytes,
            &newHandle, nullptr, 0), "optixAccelBuild(mesh)");
        mesh.gasHandle = newHandle;

        EnsureMeshHitRecord(meshId);

        return meshId;
    }

    void UpdateMeshVertices(
        int meshId,
        const float* vertices,
        unsigned int vertexCount) {
        if (static_cast<size_t>(meshId) >= meshes_.size()) {
            throw OptixError("Mesh ID not found for update.");
        }

        auto& mesh = meshes_[static_cast<size_t>(meshId)];
        const auto vertexBytes = static_cast<size_t>(vertexCount) * sizeof(float3);
        EnsureBuffer(mesh.vertexBuffer, mesh.vertexCapacity, vertexBytes);

        CheckCuda(cudaMemcpy(reinterpret_cast<void*>(mesh.vertexBuffer), vertices, vertexBytes, cudaMemcpyHostToDevice), "cudaMemcpy(updateVertices)");

        mesh.vertexCount = vertexCount;

        const uint32_t flags[] = {OPTIX_GEOMETRY_FLAG_NONE};
        OptixBuildInput buildInput{};
        buildInput.type = OPTIX_BUILD_INPUT_TYPE_TRIANGLES;
        buildInput.triangleArray.vertexFormat = OPTIX_VERTEX_FORMAT_FLOAT3;
        buildInput.triangleArray.vertexStrideInBytes = sizeof(float3);
        buildInput.triangleArray.numVertices = vertexCount;
        buildInput.triangleArray.vertexBuffers = &mesh.vertexBuffer;
        buildInput.triangleArray.indexFormat = OPTIX_INDICES_FORMAT_UNSIGNED_INT3;
        buildInput.triangleArray.indexStrideInBytes = sizeof(TriangleIndices);
        buildInput.triangleArray.numIndexTriplets = mesh.triangleCount;
        buildInput.triangleArray.indexBuffer = mesh.indexBuffer;
        buildInput.triangleArray.flags = flags;
        buildInput.triangleArray.numSbtRecords = 1;

        OptixAccelBuildOptions accelOptions{};
        accelOptions.buildFlags = OPTIX_BUILD_FLAG_ALLOW_COMPACTION | OPTIX_BUILD_FLAG_ALLOW_UPDATE;
        accelOptions.operation = OPTIX_BUILD_OPERATION_UPDATE;

        OptixAccelBufferSizes sizes{};
        CheckOptix(optixAccelComputeMemoryUsage(context_, &accelOptions, &buildInput, 1, &sizes), "optixAccelComputeMemoryUsage(meshUpdate)");

        EnsureBuffer(gasScratchBuffer_, gasScratchCapacity_, sizes.tempSizeInBytes);

        CheckOptix(optixAccelBuild(
            context_, stream_, &accelOptions, &buildInput, 1,
            gasScratchBuffer_, sizes.tempSizeInBytes,
            mesh.gasOutputBuffer, mesh.gasOutputCapacity,
            &mesh.gasHandle, nullptr, 0), "optixAccelBuild(meshUpdate)");
    }

    void EnsureMeshHitRecord(int meshId) {
        if (meshId < 0) return;

        HitGroupRecord hitRecord{};
        CheckOptix(optixSbtRecordPackHeader(hitgroupProgramGroup_, &hitRecord), "optixSbtRecordPackHeader(hitgroup-mesh)");

        auto& mesh = meshes_[static_cast<size_t>(meshId)];
        hitRecord.data.vertices = mesh.vertexBuffer;
        hitRecord.data.normals = mesh.normalBuffer;
        hitRecord.data.texCoords = mesh.texCoordBuffer;
        hitRecord.data.indices = mesh.indexBuffer;
        hitRecord.data.materials = materialBuffer_;

        const auto recordSize = sizeof(HitGroupRecord);
        const auto requiredSize = static_cast<size_t>(meshId + 1) * recordSize;
        if (hitgroupRecordCapacity_ < requiredSize) {
            auto newBuffer = CUdeviceptr{};
            CheckCuda(cudaMalloc(reinterpret_cast<void**>(&newBuffer), requiredSize), "cudaMalloc(hitgroupRecords)");
            if (hitgroupRecordBuffer_ != 0) {
                CheckCuda(cudaMemcpy(reinterpret_cast<void*>(newBuffer), reinterpret_cast<void*>(hitgroupRecordBuffer_), hitgroupRecordCapacity_, cudaMemcpyDeviceToDevice), "cudaMemcpy(hitgroupRecords)");
                CheckCuda(cudaFree(reinterpret_cast<void*>(hitgroupRecordBuffer_)), "cudaFree(hitgroupRecords)");
            }
            hitgroupRecordBuffer_ = newBuffer;
            hitgroupRecordCapacity_ = requiredSize;
        }

        const auto offset = static_cast<size_t>(meshId) * recordSize;
        CheckCuda(cudaMemcpy(
            reinterpret_cast<void*>(hitgroupRecordBuffer_ + offset),
            &hitRecord, recordSize, cudaMemcpyHostToDevice), "cudaMemcpy(hitgroupRecord-mesh)");

        sbt_.hitgroupRecordBase = hitgroupRecordBuffer_;
        sbt_.hitgroupRecordCount = std::max(sbt_.hitgroupRecordCount, static_cast<unsigned int>(meshId + 1));
    }

    void RenderInstances(
        const NativeCamera& camera,
        const NativeRenderSettings& settings,
        const int* meshIds,
        int instanceCount,
        const int* materialIndices,
        const float* transforms,
        const float* materialParameters,
        int materialFloatCount,
        const int32_t* materialAlbedoTextureIndices,
        const uint8_t* texturePixels,
        int texturePixelByteCount,
        const int32_t* textureMetadata,
        int textureMetadataCount,
        unsigned int frameIndex,
        unsigned int outputTextureId,
        NativeFrameStats* stats) {
        const auto totalStart = std::chrono::steady_clock::now();
        NativeFrameStats localStats{};

        if (instanceCount <= 0 || materialFloatCount <= 0 || (materialFloatCount % 5) != 0) {
            throw OptixError("Scene buffers are invalid.");
        }
        if (outputTextureId == 0) {
            throw OptixError("Output texture is invalid.");
        }

        EnsureOutputTextureInterop(outputTextureId);

        const auto uploadStart = std::chrono::steady_clock::now();

        const auto materialCount = static_cast<unsigned int>(materialFloatCount / 5);
        const auto materialBytes = static_cast<size_t>(materialCount) * sizeof(NativeMaterial);
        std::vector<NativeMaterial> materials(materialCount);
        for (unsigned int materialIndex = 0; materialIndex < materialCount; ++materialIndex) {
            const auto parameterOffset = materialIndex * 5;
            materials[materialIndex] = NativeMaterial{
                materialParameters[parameterOffset],
                materialParameters[parameterOffset + 1],
                materialParameters[parameterOffset + 2],
                materialParameters[parameterOffset + 3],
                materialParameters[parameterOffset + 4],
                materialAlbedoTextureIndices[materialIndex]
            };
        }

        const auto texturePixelBytes = static_cast<size_t>(texturePixelByteCount);
        const auto textureCount = textureMetadataCount / 3;
        const auto textureMetadataBytes = static_cast<size_t>(textureCount) * 3 * sizeof(int32_t);

        EnsureBuffer(materialBuffer_, materialBufferCapacity_, materialBytes);
        EnsureBuffer(texturePixelBuffer_, texturePixelBufferCapacity_, texturePixelBytes);
        EnsureBuffer(textureMetadataBuffer_, textureMetadataBufferCapacity_, textureMetadataBytes);

        CheckCuda(cudaMemcpy(reinterpret_cast<void*>(materialBuffer_), materials.data(), materialBytes, cudaMemcpyHostToDevice), "cudaMemcpy(materials)");
        if (texturePixelBytes > 0) {
            CheckCuda(cudaMemcpy(reinterpret_cast<void*>(texturePixelBuffer_), texturePixels, texturePixelBytes, cudaMemcpyHostToDevice), "cudaMemcpy(texturePixels)");
        }
        if (textureMetadataBytes > 0) {
            CheckCuda(cudaMemcpy(reinterpret_cast<void*>(textureMetadataBuffer_), textureMetadata, textureMetadataBytes, cudaMemcpyHostToDevice), "cudaMemcpy(textureMetadata)");
        }

        localStats.uploadSceneMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - uploadStart).count();

        const auto iasStart = std::chrono::steady_clock::now();

        std::vector<OptixInstance> optixInstances(static_cast<size_t>(instanceCount));
        for (int i = 0; i < instanceCount; ++i) {
            auto& inst = optixInstances[static_cast<size_t>(i)];

            const auto* tf = &transforms[i * 16];
            for (int row = 0; row < 3; ++row) {
                for (int col = 0; col < 4; ++col) {
                    inst.transform[row * 4 + col] = tf[row * 4 + col];
                }
            }

            inst.instanceId = static_cast<unsigned int>(materialIndices[i]);

            const auto meshId = meshIds[i];
            if (static_cast<size_t>(meshId) >= meshes_.size() || meshes_[static_cast<size_t>(meshId)].gasHandle == 0) {
                throw OptixError("Instance references unknown mesh.");
            }

            inst.sbtOffset = static_cast<unsigned int>(meshId);
            inst.visibilityMask = 1;
            inst.flags = OPTIX_INSTANCE_FLAG_NONE;
            inst.traversableHandle = meshes_[static_cast<size_t>(meshId)].gasHandle;
        }

        const auto instancesBytes = static_cast<size_t>(instanceCount) * sizeof(OptixInstance);
        EnsureBuffer(iasInstanceBuffer_, iasInstanceCapacity_, instancesBytes);
        CheckCuda(cudaMemcpy(reinterpret_cast<void*>(iasInstanceBuffer_), optixInstances.data(), instancesBytes, cudaMemcpyHostToDevice), "cudaMemcpy(instances)");

        OptixAccelBuildOptions iasOptions{};
        iasOptions.buildFlags = OPTIX_BUILD_FLAG_ALLOW_COMPACTION;
        iasOptions.operation = OPTIX_BUILD_OPERATION_BUILD;

        OptixBuildInput iasInput{};
        iasInput.type = OPTIX_BUILD_INPUT_TYPE_INSTANCES;
        iasInput.instanceArray.instances = iasInstanceBuffer_;
        iasInput.instanceArray.numInstances = static_cast<unsigned int>(instanceCount);

        OptixAccelBufferSizes iasSizes{};
        CheckOptix(optixAccelComputeMemoryUsage(context_, &iasOptions, &iasInput, 1, &iasSizes), "optixAccelComputeMemoryUsage(IAS)");

        EnsureBuffer(iasScratchBuffer_, iasScratchCapacity_, iasSizes.tempSizeInBytes);
        EnsureBuffer(iasOutputBuffer_, iasOutputCapacity_, iasSizes.outputSizeInBytes);

        OptixTraversableHandle iasHandle = 0;
        CheckOptix(optixAccelBuild(
            context_, stream_, &iasOptions, &iasInput, 1,
            iasScratchBuffer_, iasSizes.tempSizeInBytes,
            iasOutputBuffer_, iasSizes.outputSizeInBytes,
            &iasHandle, nullptr, 0), "optixAccelBuild(IAS)");

        localStats.uploadSceneMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - iasStart).count();

        LaunchParams params{};
        params.beauty = reinterpret_cast<float4*>(beautyBuffer_);
        params.accumulation = reinterpret_cast<float4*>(accumulationBuffer_);
        params.depth = reinterpret_cast<float*>(depthBuffer_);
        params.worldPosition = reinterpret_cast<float4*>(worldPositionBuffer_);
        params.normals = reinterpret_cast<float4*>(normalGuideBuffer_);
        params.roughness = reinterpret_cast<float*>(roughnessGuideBuffer_);
        params.diffuseAlbedo = reinterpret_cast<float4*>(diffuseAlbedoGuideBuffer_);
        params.specularAlbedo = reinterpret_cast<float4*>(specularAlbedoGuideBuffer_);
        params.materials = materialBuffer_;
        params.texturePixels = texturePixelBuffer_;
        params.textureMetadata = textureMetadataBuffer_;
        params.textureCount = static_cast<unsigned int>(textureCount);
        params.imageWidth = static_cast<unsigned int>(width_);
        params.imageHeight = static_cast<unsigned int>(height_);
        params.cameraPosition = Float3{camera.positionX, camera.positionY, camera.positionZ};
        const Float3 target = Float3{camera.targetX, camera.targetY, camera.targetZ};
        params.cameraForward = Normalize(Subtract(target, params.cameraPosition));
        params.cameraRight = Normalize(Cross(params.cameraForward, Float3{0.0f, 1.0f, 0.0f}));
        params.cameraUp = Normalize(Cross(params.cameraRight, params.cameraForward));
        params.tanHalfFovY = std::tan(camera.fovY * 0.5f * 3.14159265359f / 180.0f);
        params.settings = settings;
        params.iasHandle = iasHandle;
        params.frameIndex = frameIndex;
        BuildViewProjectionMatrix(camera, params.currentViewProjection);

        CheckCuda(cudaMemcpy(reinterpret_cast<void*>(launchParamsBuffer_), &params, sizeof(params), cudaMemcpyHostToDevice), "cudaMemcpy(launchParams)");

        const auto launchStart = std::chrono::steady_clock::now();
        CheckOptix(optixLaunch(
            pipeline_,
            stream_,
            launchParamsBuffer_,
            sizeof(LaunchParams),
            &sbt_,
            static_cast<unsigned int>(width_),
            static_cast<unsigned int>(height_),
            1), "optixLaunch");

        CheckCuda(cudaStreamSynchronize(stream_), "cudaStreamSynchronize");
        localStats.launchMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - launchStart).count();

        const auto denoiseStart = std::chrono::steady_clock::now();
        const int denoiserInterval = std::max(settings.denoiserIntervalFrames, 1);
        const unsigned int denoiserFrameIndex = presentFrameIndex_++;
        const bool shouldDenoise = settings.enableDenoiser != 0 &&
            (denoiserFrameIndex == 0u || (denoiserFrameIndex % static_cast<unsigned int>(denoiserInterval)) == 0u);
        CUdeviceptr presentationSource = beautyBuffer_;
        if (shouldDenoise) {
            presentationSource = DenoiseBeauty();
            hasValidDenoisedBeauty_ = true;
            localStats.denoisedThisFrame = 1;
        }
        localStats.denoiseMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - denoiseStart).count();

        const auto toneMapStart = std::chrono::steady_clock::now();
        PresentToOutputTexture(presentationSource, settings);
        localStats.toneMapMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - toneMapStart).count();
        localStats.readbackMs = 0.0;
        localStats.totalMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - totalStart).count();

        if (stats != nullptr) {
            *stats = localStats;
        }
    }

private:
    std::string CompileOptixPtx() {
        nvrtcProgram program = nullptr;
        CheckNvrtc(nvrtcCreateProgram(&program, kOptixDeviceSource, "rayoptix_optix_kernels.cu", 0, nullptr, nullptr), "nvrtcCreateProgram");

        cudaDeviceProp properties{};
        CheckCuda(cudaGetDeviceProperties(&properties, 0), "cudaGetDeviceProperties");

        std::string architecture = "--gpu-architecture=compute_" + std::to_string(properties.major) + std::to_string(properties.minor);
        std::string optixInclude = std::string("--include-path=") + ROPTIX_OPTIX_INCLUDE_DIR;
        std::string cudaInclude = std::string("--include-path=") + ROPTIX_CUDA_INCLUDE_DIR;

        std::vector<const char*> options = {
            "--std=c++17",
            "--use_fast_math",
            "--device-as-default-execution-space",
            architecture.c_str(),
            optixInclude.c_str(),
            cudaInclude.c_str(),
        };

        const auto compileResult = nvrtcCompileProgram(program, static_cast<int>(options.size()), options.data());

        size_t logSize = 0;
        CheckNvrtc(nvrtcGetProgramLogSize(program, &logSize), "nvrtcGetProgramLogSize");
        if (logSize > 1) {
            std::string log(logSize, '\0');
            CheckNvrtc(nvrtcGetProgramLog(program, log.data()), "nvrtcGetProgramLog");
            if (compileResult != NVRTC_SUCCESS) {
                nvrtcDestroyProgram(&program);
                throw OptixError("NVRTC compile log:\n" + log);
            }
        }

        CheckNvrtc(compileResult, "nvrtcCompileProgram");

        size_t ptxSize = 0;
        CheckNvrtc(nvrtcGetPTXSize(program, &ptxSize), "nvrtcGetPTXSize");
        std::string ptx(ptxSize, '\0');
        CheckNvrtc(nvrtcGetPTX(program, ptx.data()), "nvrtcGetPTX");
        CheckNvrtc(nvrtcDestroyProgram(&program), "nvrtcDestroyProgram");
        return ptx;
    }

    std::string CompilePresentPtx() {
        nvrtcProgram program = nullptr;
        CheckNvrtc(nvrtcCreateProgram(&program, kPresentDeviceSource, "rayoptix_present_kernels.cu", 0, nullptr, nullptr), "nvrtcCreateProgram");

        cudaDeviceProp properties{};
        CheckCuda(cudaGetDeviceProperties(&properties, 0), "cudaGetDeviceProperties");

        std::string architecture = "--gpu-architecture=compute_" + std::to_string(properties.major) + std::to_string(properties.minor);
        std::string cudaInclude = std::string("--include-path=") + ROPTIX_CUDA_INCLUDE_DIR;

        std::vector<const char*> options = {
            "--std=c++17",
            "--use_fast_math",
            "--device-as-default-execution-space",
            architecture.c_str(),
            cudaInclude.c_str(),
        };

        const auto compileResult = nvrtcCompileProgram(program, static_cast<int>(options.size()), options.data());

        size_t logSize = 0;
        CheckNvrtc(nvrtcGetProgramLogSize(program, &logSize), "nvrtcGetProgramLogSize");
        if (logSize > 1) {
            std::string log(logSize, '\0');
            CheckNvrtc(nvrtcGetProgramLog(program, log.data()), "nvrtcGetProgramLog");
            if (compileResult != NVRTC_SUCCESS) {
                nvrtcDestroyProgram(&program);
                throw OptixError("NVRTC compile log:\n" + log);
            }
        }

        CheckNvrtc(compileResult, "nvrtcCompileProgram");

        size_t ptxSize = 0;
        CheckNvrtc(nvrtcGetPTXSize(program, &ptxSize), "nvrtcGetPTXSize");
        std::string ptx(ptxSize, '\0');
        CheckNvrtc(nvrtcGetPTX(program, ptx.data()), "nvrtcGetPTX");
        CheckNvrtc(nvrtcDestroyProgram(&program), "nvrtcDestroyProgram");
        return ptx;
    }

    void Destroy() {
        ReleaseOutputTexture();
        SafeCudaFree(beautyBuffer_);
        SafeCudaFree(denoisedBeautyBuffer_);
        SafeCudaFree(accumulationBuffer_);
        SafeCudaFree(depthBuffer_);
        SafeCudaFree(worldPositionBuffer_);
        SafeCudaFree(normalGuideBuffer_);
        SafeCudaFree(roughnessGuideBuffer_);
        SafeCudaFree(diffuseAlbedoGuideBuffer_);
        SafeCudaFree(specularAlbedoGuideBuffer_);
        SafeCudaFree(launchParamsBuffer_);
        SafeCudaFree(raygenRecordBuffer_);
        SafeCudaFree(missRecordBuffer_);
        SafeCudaFree(hitgroupRecordBuffer_);
        for (auto& mesh : meshes_) {
            SafeCudaFree(mesh.vertexBuffer);
            SafeCudaFree(mesh.normalBuffer);
            SafeCudaFree(mesh.texCoordBuffer);
            SafeCudaFree(mesh.indexBuffer);
            SafeCudaFree(mesh.gasOutputBuffer);
        }
        meshes_.clear();
        SafeCudaFree(materialBuffer_);
        SafeCudaFree(texturePixelBuffer_);
        SafeCudaFree(textureMetadataBuffer_);
        SafeCudaFree(denoiserStateBuffer_);
        SafeCudaFree(denoiserScratchBuffer_);
        SafeCudaFree(gasScratchBuffer_);
        SafeCudaFree(iasInstanceBuffer_);
        SafeCudaFree(iasScratchBuffer_);
        SafeCudaFree(iasOutputBuffer_);
        if (pipeline_ != nullptr) {
            optixPipelineDestroy(pipeline_);
            pipeline_ = nullptr;
        }
        if (raygenProgramGroup_ != nullptr) {
            optixProgramGroupDestroy(raygenProgramGroup_);
            raygenProgramGroup_ = nullptr;
        }
        if (missProgramGroup_ != nullptr) {
            optixProgramGroupDestroy(missProgramGroup_);
            missProgramGroup_ = nullptr;
        }
        if (hitgroupProgramGroup_ != nullptr) {
            optixProgramGroupDestroy(hitgroupProgramGroup_);
            hitgroupProgramGroup_ = nullptr;
        }
        if (module_ != nullptr) {
            optixModuleDestroy(module_);
            module_ = nullptr;
        }
        if (cudaModule_ != nullptr) {
            IgnoreCudaDriver(cuModuleUnload(cudaModule_));
            cudaModule_ = nullptr;
        }
        if (denoiser_ != nullptr) {
            optixDenoiserDestroy(denoiser_);
            denoiser_ = nullptr;
        }
        if (stream_ != nullptr) {
            IgnoreCudaDriver(cuStreamDestroy(reinterpret_cast<CUstream>(stream_)));
            stream_ = nullptr;
        }
        if (context_ != nullptr) {
            optixDeviceContextDestroy(context_);
            context_ = nullptr;
        }
        if (cudaContext_ != nullptr) {
            IgnoreCudaDriver(cuCtxDestroy(cudaContext_));
            cudaContext_ = nullptr;
        }
    }

    static Float3 Subtract(Float3 left, Float3 right) {
        return Float3{left.x - right.x, left.y - right.y, left.z - right.z};
    }

    static Float3 Cross(Float3 left, Float3 right) {
        return Float3{
            left.y * right.z - left.z * right.y,
            left.z * right.x - left.x * right.z,
            left.x * right.y - left.y * right.x,
        };
    }

    static Float3 Normalize(Float3 value) {
        const auto length = std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
        const auto inverse = length > 1e-8f ? 1.0f / static_cast<float>(length) : 0.0f;
        return Float3{value.x * inverse, value.y * inverse, value.z * inverse};
    }

    static void MultiplyMatrices(const float* left, const float* right, float* out) {
        for (int row = 0; row < 4; ++row) {
            for (int column = 0; column < 4; ++column) {
                out[row * 4 + column] =
                    left[row * 4 + 0] * right[0 * 4 + column] +
                    left[row * 4 + 1] * right[1 * 4 + column] +
                    left[row * 4 + 2] * right[2 * 4 + column] +
                    left[row * 4 + 3] * right[3 * 4 + column];
            }
        }
    }

    static void LogCallback(unsigned int level, const char* tag, const char* message, void*) {
        (void)level;
        (void)tag;
        (void)message;
    }

    static void EnsureBuffer(CUdeviceptr& buffer, size_t& capacity, size_t requiredSize) {
        if (requiredSize == 0) {
            return;
        }

        if (capacity >= requiredSize && buffer != 0) {
            return;
        }

        if (buffer != 0) {
            CheckCuda(cudaFree(reinterpret_cast<void*>(buffer)), "cudaFree(buffer)");
        }

        CheckCuda(cudaMalloc(reinterpret_cast<void**>(&buffer), requiredSize), "cudaMalloc(buffer)");
        capacity = requiredSize;
    }

    static void SafeCudaFree(CUdeviceptr& buffer) {
        if (buffer != 0) {
            IgnoreCuda(cudaFree(reinterpret_cast<void*>(buffer)));
            buffer = 0;
        }
    }

    static void ToneMapToPixels(
        const float4* hdrPixels,
        int sourceWidth,
        int sourceHeight,
        int outputWidth,
        int outputHeight,
        const NativeRenderSettings& settings,
        uint8_t* outputPixels) {
        const float exposure = std::max(settings.exposure, 0.001f);
        const float gamma = std::max(settings.gamma, 0.001f);
        constexpr int lutSize = 4096;
        const float inverseGamma = 1.0f / gamma;

        std::array<uint8_t, lutSize + 1> tonemapLut{};
        for (int index = 0; index <= lutSize; ++index) {
            const float normalized = static_cast<float>(index) / static_cast<float>(lutSize);
            const float corrected = std::pow(normalized, inverseGamma);
            tonemapLut[static_cast<size_t>(index)] = static_cast<uint8_t>(std::clamp(corrected * 255.0f, 0.0f, 255.0f));
        }

        std::vector<int> sourceXLookup(static_cast<size_t>(outputWidth));
        std::vector<int> sourceRowLookup(static_cast<size_t>(outputHeight));

        for (int x = 0; x < outputWidth; ++x) {
            sourceXLookup[static_cast<size_t>(x)] = std::clamp(x * sourceWidth / std::max(outputWidth, 1), 0, std::max(sourceWidth - 1, 0));
        }

        for (int y = 0; y < outputHeight; ++y) {
            const int sourceY = std::clamp(y * sourceHeight / std::max(outputHeight, 1), 0, std::max(sourceHeight - 1, 0));
            sourceRowLookup[static_cast<size_t>(y)] = sourceY * sourceWidth;
        }

        const auto tonemapChannel = [&](float value) -> uint8_t {
            const float exposed = std::clamp(value * exposure, 0.0f, 1.0f);
            const int lutIndex = std::clamp(static_cast<int>(exposed * static_cast<float>(lutSize) + 0.5f), 0, lutSize);
            return tonemapLut[static_cast<size_t>(lutIndex)];
        };

        const unsigned int workerCount = std::max(1u, std::min(
            std::thread::hardware_concurrency() == 0 ? 1u : std::thread::hardware_concurrency(),
            static_cast<unsigned int>(std::max(outputHeight, 1))));

        std::vector<std::thread> workers;
        workers.reserve(workerCount > 0 ? workerCount - 1 : 0);

        const auto processRows = [&](int rowStart, int rowEnd) {
            for (int y = rowStart; y < rowEnd; ++y) {
                const int sourceRow = sourceRowLookup[static_cast<size_t>(y)];
                const size_t outputRow = static_cast<size_t>(y * outputWidth) * 4;

                for (int x = 0; x < outputWidth; ++x) {
                    const auto& source = hdrPixels[sourceRow + sourceXLookup[static_cast<size_t>(x)]];
                    const size_t outputIndex = outputRow + static_cast<size_t>(x) * 4;

                    outputPixels[outputIndex + 0] = tonemapChannel(source.x);
                    outputPixels[outputIndex + 1] = tonemapChannel(source.y);
                    outputPixels[outputIndex + 2] = tonemapChannel(source.z);
                    outputPixels[outputIndex + 3] = 255;
                }
            }
        };

        const int rowsPerWorker = std::max(1, outputHeight / static_cast<int>(workerCount));
        int rowStart = 0;

        for (unsigned int workerIndex = 1; workerIndex < workerCount; ++workerIndex) {
            const int rowEnd = std::min(outputHeight, rowStart + rowsPerWorker);
            workers.emplace_back(processRows, rowStart, rowEnd);
            rowStart = rowEnd;
        }

        processRows(rowStart, outputHeight);

        for (auto& worker : workers) {
            worker.join();
        }
    }

    OptixDeviceContext context_ = nullptr;
    CUcontext cudaContext_ = nullptr;
    CUdevice cudaDevice_ = 0;
    cudaStream_t stream_ = nullptr;
    OptixDenoiser denoiser_ = nullptr;
    OptixModule module_ = nullptr;
    CUmodule cudaModule_ = nullptr;
    CUfunction toneMapKernel_ = nullptr;
    OptixPipeline pipeline_ = nullptr;
    OptixProgramGroup raygenProgramGroup_ = nullptr;
    OptixProgramGroup missProgramGroup_ = nullptr;
    OptixProgramGroup hitgroupProgramGroup_ = nullptr;
    OptixShaderBindingTable sbt_{};

    int width_ = 0;
    int height_ = 0;
    int outputWidth_ = 0;
    int outputHeight_ = 0;

    CUdeviceptr beautyBuffer_ = 0;
    CUdeviceptr denoisedBeautyBuffer_ = 0;
    CUdeviceptr accumulationBuffer_ = 0;
    CUdeviceptr depthBuffer_ = 0;
    CUdeviceptr worldPositionBuffer_ = 0;
    CUdeviceptr normalGuideBuffer_ = 0;
    CUdeviceptr roughnessGuideBuffer_ = 0;
    CUdeviceptr diffuseAlbedoGuideBuffer_ = 0;
    CUdeviceptr specularAlbedoGuideBuffer_ = 0;
    CUdeviceptr launchParamsBuffer_ = 0;
    CUdeviceptr raygenRecordBuffer_ = 0;
    CUdeviceptr missRecordBuffer_ = 0;
    CUdeviceptr hitgroupRecordBuffer_ = 0;

    CUdeviceptr materialBuffer_ = 0;
    CUdeviceptr texturePixelBuffer_ = 0;
    CUdeviceptr textureMetadataBuffer_ = 0;
    CUdeviceptr denoiserStateBuffer_ = 0;
    CUdeviceptr denoiserScratchBuffer_ = 0;
    CUdeviceptr gasScratchBuffer_ = 0;
    CUdeviceptr iasInstanceBuffer_ = 0;
    CUdeviceptr iasScratchBuffer_ = 0;
    CUdeviceptr iasOutputBuffer_ = 0;
    cudaGraphicsResource_t textureInteropResource_ = nullptr;
    unsigned int registeredOutputTextureId_ = 0;

    size_t materialBufferCapacity_ = 0;
    size_t texturePixelBufferCapacity_ = 0;
    size_t textureMetadataBufferCapacity_ = 0;
    size_t denoiserStateCapacity_ = 0;
    size_t denoiserScratchCapacity_ = 0;
    size_t gasScratchCapacity_ = 0;
    size_t iasInstanceCapacity_ = 0;
    size_t iasScratchCapacity_ = 0;
    size_t iasOutputCapacity_ = 0;
    size_t hitgroupRecordCapacity_ = 0;

    OptixDenoiserSizes denoiserSizes_{};
    bool hasValidDenoisedBeauty_ = false;
    unsigned int presentFrameIndex_ = 0;
    float previousViewProjection_[16] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };

    std::vector<MeshGpuData> meshes_;
};

void CopyError(char* error, int errorCapacity, const std::string& message) {
    if (error == nullptr || errorCapacity <= 0) {
        return;
    }

    std::snprintf(error, static_cast<size_t>(errorCapacity), "%s", message.c_str());
}

} // namespace

bool CreateRendererHandle(int renderWidth, int renderHeight, int outputWidth, int outputHeight, void** handle, char* error, int errorCapacity) {
    try {
        if (handle == nullptr) {
            throw OptixError("Handle pointer is null.");
        }

        auto renderer = std::make_unique<NativeRenderer>(renderWidth, renderHeight, outputWidth, outputHeight);
        *handle = renderer.release();
        CopyError(error, errorCapacity, "");
        return true;
    } catch (const std::exception& exception) {
        CopyError(error, errorCapacity, exception.what());
        return false;
    }
}

void DestroyRendererHandle(void* handle) {
    delete static_cast<NativeRenderer*>(handle);
}

void ReleaseRendererOutputTexture(void* handle, unsigned int textureId) {
    if (handle == nullptr) {
        return;
    }

    static_cast<NativeRenderer*>(handle)->ReleaseOutputTextureForInterop(textureId);
}

bool ResizeRendererHandle(void* handle, int renderWidth, int renderHeight, int outputWidth, int outputHeight, char* error, int errorCapacity) {
    try {
        if (handle == nullptr) {
            throw OptixError("Renderer handle is null.");
        }

        static_cast<NativeRenderer*>(handle)->Resize(renderWidth, renderHeight, outputWidth, outputHeight);
        CopyError(error, errorCapacity, "");
        return true;
    } catch (const std::exception& exception) {
        CopyError(error, errorCapacity, exception.what());
        return false;
    }
}

bool UploadMeshRendererHandle(
    void* handle,
    int meshId,
    const float* vertices,
    int vertexFloatCount,
    const float* normals,
    int normalFloatCount,
    const float* texCoords,
    int texCoordFloatCount,
    const uint32_t* indices,
    int indexCount,
    char* error,
    int errorCapacity) {
    try {
        if (handle == nullptr) {
            throw OptixError("Renderer handle is null.");
        }

        auto* renderer = static_cast<NativeRenderer*>(handle);
        const auto vertexCount = static_cast<unsigned int>(vertexFloatCount / 3);
        const auto triangleCount = static_cast<unsigned int>(indexCount / 3);
        renderer->UploadMesh(
            meshId,
            vertices, vertexCount,
            normals,
            texCoords,
            indices, triangleCount);
        CopyError(error, errorCapacity, "");
        return true;
    } catch (const std::exception& exception) {
        CopyError(error, errorCapacity, exception.what());
        return false;
    }
}

bool UpdateMeshVerticesRendererHandle(
    void* handle,
    int meshId,
    const float* vertices,
    int vertexFloatCount,
    char* error,
    int errorCapacity) {
    try {
        if (handle == nullptr) {
            throw OptixError("Renderer handle is null.");
        }

        auto* renderer = static_cast<NativeRenderer*>(handle);
        const auto vertexCount = static_cast<unsigned int>(vertexFloatCount / 3);
        renderer->UpdateMeshVertices(meshId, vertices, vertexCount);
        CopyError(error, errorCapacity, "");
        return true;
    } catch (const std::exception& exception) {
        CopyError(error, errorCapacity, exception.what());
        return false;
    }
}

bool RenderInstancesRendererHandle(
    void* handle,
    int renderWidth,
    int renderHeight,
    int outputWidth,
    int outputHeight,
    NativeCamera camera,
    NativeRenderSettings settings,
    const int* meshIds,
    int instanceCount,
    const int* materialIndices,
    const float* transforms,
    const float* materialParameters,
    int materialFloatCount,
    const int32_t* materialAlbedoTextureIndices,
    int materialTextureIndexCount,
    const uint8_t* texturePixels,
    int texturePixelByteCount,
    const int32_t* textureMetadata,
    int textureMetadataCount,
    unsigned int frameIndex,
    unsigned int outputTextureId,
    NativeFrameStats* stats,
    char* error,
    int errorCapacity) {
    try {
        if (handle == nullptr) {
            throw OptixError("Renderer handle is null.");
        }

        auto* renderer = static_cast<NativeRenderer*>(handle);
        renderer->Resize(renderWidth, renderHeight, outputWidth, outputHeight);
        renderer->RenderInstances(
            camera,
            settings,
            meshIds,
            instanceCount,
            materialIndices,
            transforms,
            materialParameters,
            materialFloatCount,
            materialAlbedoTextureIndices,
            texturePixels,
            texturePixelByteCount,
            textureMetadata,
            textureMetadataCount,
            frameIndex,
            outputTextureId,
            stats);
        CopyError(error, errorCapacity, "");
        return true;
    } catch (const std::exception& exception) {
        CopyError(error, errorCapacity, exception.what());
        return false;
    }
}
