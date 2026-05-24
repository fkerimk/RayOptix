#include <cuda.h>
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
#include <cstring>
#include <exception>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct Float3 {
    float x;
    float y;
    float z;
};

struct NativeCamera {
    float positionX;
    float positionY;
    float positionZ;
    float targetX;
    float targetY;
    float targetZ;
    float fovY;
};

struct NativeRenderSettings {
    int samplesPerPixel;
    int maxBounces;
    int minBounces;
    int russianRouletteStartBounce;
    int enableAccumulation;
    int enableDenoiser;
    int enableSky;
    int enableSunLight;
    int enableHardShadows;
    int enableNormalDebug;
    float exposure;
    float gamma;
    float skyBottomR;
    float skyBottomG;
    float skyBottomB;
    float skyTopR;
    float skyTopG;
    float skyTopB;
    float sunDirectionX;
    float sunDirectionY;
    float sunDirectionZ;
    float sunIntensity;
    float sunAngularRadius;
    float ambientIntensity;
};

struct NativeMaterial {
    float albedoR;
    float albedoG;
    float albedoB;
    float opacity;
    int reflective;
    float reflectivity;
};

struct LaunchParams {
    float4* beauty;
    float4* accumulation;
    CUdeviceptr materials;
    unsigned int imageWidth;
    unsigned int imageHeight;
    Float3 cameraPosition;
    Float3 cameraForward;
    Float3 cameraRight;
    Float3 cameraUp;
    float tanHalfFovY;
    NativeRenderSettings settings;
    OptixTraversableHandle handle;
    unsigned int frameIndex;
};

struct MissData {
    Float3 skyTop;
    Float3 skyBottom;
};

struct HitGroupData {
    CUdeviceptr vertices;
    CUdeviceptr normals;
    CUdeviceptr indices;
    CUdeviceptr triangleMaterialIndices;
    CUdeviceptr materials;
};

struct TriangleIndices {
    uint16_t x;
    uint16_t y;
    uint16_t z;
};

static_assert(sizeof(TriangleIndices) == 6, "TriangleIndices must stay tightly packed.");

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

const char* kOptixDeviceSource = R"(
#include <optix.h>
#include <optix_device.h>
#include <vector_types.h>
#include <vector_functions.h>

struct Float3 {
    float x;
    float y;
    float z;
};

struct NativeRenderSettings {
    int samplesPerPixel;
    int maxBounces;
    int minBounces;
    int russianRouletteStartBounce;
    int enableAccumulation;
    int enableDenoiser;
    int enableSky;
    int enableSunLight;
    int enableHardShadows;
    int enableNormalDebug;
    float exposure;
    float gamma;
    float skyBottomR;
    float skyBottomG;
    float skyBottomB;
    float skyTopR;
    float skyTopG;
    float skyTopB;
    float sunDirectionX;
    float sunDirectionY;
    float sunDirectionZ;
    float sunIntensity;
    float sunAngularRadius;
    float ambientIntensity;
};

struct NativeMaterial {
    float albedoR;
    float albedoG;
    float albedoB;
    float opacity;
    int reflective;
    float reflectivity;
};

struct LaunchParams {
    float4* beauty;
    float4* accumulation;
    unsigned long long materials;
    unsigned int imageWidth;
    unsigned int imageHeight;
    Float3 cameraPosition;
    Float3 cameraForward;
    Float3 cameraRight;
    Float3 cameraUp;
    float tanHalfFovY;
    NativeRenderSettings settings;
    OptixTraversableHandle handle;
    unsigned int frameIndex;
};

struct MissData {
    Float3 skyTop;
    Float3 skyBottom;
};

struct HitGroupData {
    unsigned long long vertices;
    unsigned long long normals;
    unsigned long long indices;
    unsigned long long triangleMaterialIndices;
    unsigned long long materials;
};

struct TriangleIndices {
    unsigned short x;
    unsigned short y;
    unsigned short z;
};

struct Payload {
    float3 hitPosition;
    float3 hitNormal;
    int materialIndex;
    int hit;
};

struct RayGenData {
};

extern "C" {
__constant__ LaunchParams params;
}

static __forceinline__ __device__ float3 ToFloat3(Float3 value) {
    return make_float3(value.x, value.y, value.z);
}

static __forceinline__ __device__ Float3 FromFloat3(float3 value) {
    Float3 out;
    out.x = value.x;
    out.y = value.y;
    out.z = value.z;
    return out;
}

static __forceinline__ __device__ float3 operator+(float3 a, float3 b) {
    return make_float3(a.x + b.x, a.y + b.y, a.z + b.z);
}

static __forceinline__ __device__ float3 operator-(float3 a, float3 b) {
    return make_float3(a.x - b.x, a.y - b.y, a.z - b.z);
}

static __forceinline__ __device__ float3 operator-(float3 a) {
    return make_float3(-a.x, -a.y, -a.z);
}

static __forceinline__ __device__ float3 operator*(float3 a, float b) {
    return make_float3(a.x * b, a.y * b, a.z * b);
}

static __forceinline__ __device__ float3 operator*(float b, float3 a) {
    return a * b;
}

static __forceinline__ __device__ float3 operator*(float3 a, float3 b) {
    return make_float3(a.x * b.x, a.y * b.y, a.z * b.z);
}

static __forceinline__ __device__ float3 operator/(float3 a, float b) {
    return make_float3(a.x / b, a.y / b, a.z / b);
}

static __forceinline__ __device__ float Dot(float3 a, float3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

static __forceinline__ __device__ float3 Cross(float3 a, float3 b) {
    return make_float3(
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x);
}

static __forceinline__ __device__ float Length(float3 value) {
    return sqrtf(Dot(value, value));
}

static __forceinline__ __device__ float3 Normalize(float3 value) {
    return value / fmaxf(Length(value), 1e-8f);
}

static __forceinline__ __device__ unsigned int Tea(unsigned int value0, unsigned int value1) {
    unsigned int sum = 0;
    for (unsigned int round = 0; round < 4; ++round) {
        sum += 0x9e3779b9u;
        value0 += ((value1 << 4) + 0xa341316cu) ^ (value1 + sum) ^ ((value1 >> 5) + 0xc8013ea4u);
        value1 += ((value0 << 4) + 0xad90777du) ^ (value0 + sum) ^ ((value0 >> 5) + 0x7e95761eu);
    }
    return value0;
}

static __forceinline__ __device__ float Random(unsigned int& seed) {
    seed = 1664525u * seed + 1013904223u;
    return (seed & 0x00ffffff) / 16777216.0f;
}

static __forceinline__ __device__ Payload* GetPayloadPointer() {
    const unsigned long long low = optixGetPayload_0();
    const unsigned long long high = optixGetPayload_1();
    return reinterpret_cast<Payload*>((high << 32) | low);
}

static __forceinline__ __device__ float MaxComponent(float3 value) {
    return fmaxf(value.x, fmaxf(value.y, value.z));
}

static __forceinline__ __device__ float3 Lerp(float3 a, float3 b, float t) {
    return a * (1.0f - t) + b * t;
}

static __forceinline__ __device__ float3 BuildTangent(float3 normal) {
    return Normalize(fabsf(normal.x) > 0.1f ? Cross(make_float3(0.0f, 1.0f, 0.0f), normal) : Cross(make_float3(1.0f, 0.0f, 0.0f), normal));
}

static __forceinline__ __device__ float3 SampleCone(float3 direction, float angleRadians, unsigned int& seed) {
    if (angleRadians <= 1e-6f) {
        return Normalize(direction);
    }

    const float u1 = Random(seed);
    const float u2 = Random(seed);
    const float cosThetaMax = cosf(angleRadians);
    const float cosTheta = 1.0f - u1 * (1.0f - cosThetaMax);
    const float sinTheta = sqrtf(fmaxf(0.0f, 1.0f - cosTheta * cosTheta));
    const float phi = 6.28318530718f * u2;

    const float3 tangent = BuildTangent(direction);
    const float3 bitangent = Cross(direction, tangent);

    return Normalize(
        tangent * (sinTheta * cosf(phi)) +
        bitangent * (sinTheta * sinf(phi)) +
        direction * cosTheta);
}

static __forceinline__ __device__ float3 SampleHemisphere(float3 normal, unsigned int& seed) {
    const float u1 = Random(seed);
    const float u2 = Random(seed);
    const float radius = sqrtf(u1);
    const float phi = 6.28318530718f * u2;

    const float3 tangent = BuildTangent(normal);
    const float3 bitangent = Cross(normal, tangent);

    return Normalize(
        tangent * (radius * cosf(phi)) +
        bitangent * (radius * sinf(phi)) +
        normal * sqrtf(fmaxf(0.0f, 1.0f - u1)));
}

static __forceinline__ __device__ float3 Reflect(float3 incident, float3 normal) {
    return incident - normal * (2.0f * Dot(incident, normal));
}

static __forceinline__ __device__ float3 GetSkyColor(float3 direction) {
    if (params.settings.enableSky == 0) {
        return make_float3(0.0f, 0.0f, 0.0f);
    }

    const float t = 0.5f * (direction.y + 1.0f);
    const float3 skyBottom = make_float3(params.settings.skyBottomR, params.settings.skyBottomG, params.settings.skyBottomB);
    const float3 skyTop = make_float3(params.settings.skyTopR, params.settings.skyTopG, params.settings.skyTopB);
    return Lerp(skyBottom, skyTop, t);
}

static __forceinline__ __device__ int TraceOcclusion(float3 origin, float3 direction, float maxDistance) {
    Payload payload;
    payload.hit = 0;
    payload.hitPosition = make_float3(0.0f, 0.0f, 0.0f);
    payload.hitNormal = make_float3(0.0f, 0.0f, 0.0f);
    payload.materialIndex = -1;

    unsigned int payloadLow = static_cast<unsigned int>(reinterpret_cast<unsigned long long>(&payload));
    unsigned int payloadHigh = static_cast<unsigned int>(reinterpret_cast<unsigned long long>(&payload) >> 32);

    optixTrace(
        params.handle,
        origin,
        direction,
        0.001f,
        maxDistance,
        0.0f,
        OptixVisibilityMask(255),
        OPTIX_RAY_FLAG_DISABLE_ANYHIT,
        0,
        1,
        0,
        payloadLow,
        payloadHigh);

    return payload.hit;
}

extern "C" __global__ void __miss__ms() {
    Payload* payload = GetPayloadPointer();
    payload->hit = 0;
    payload->hitNormal = make_float3(0.0f, 0.0f, 0.0f);
    payload->hitPosition = make_float3(0.0f, 0.0f, 0.0f);
    payload->materialIndex = -1;
}

extern "C" __global__ void __closesthit__ch() {
    const HitGroupData* hitData = reinterpret_cast<const HitGroupData*>(optixGetSbtDataPointer());
    const float3* vertices = reinterpret_cast<const float3*>(hitData->vertices);
    const float3* normals = reinterpret_cast<const float3*>(hitData->normals);
    const TriangleIndices* indices = reinterpret_cast<const TriangleIndices*>(hitData->indices);
    const unsigned int* triangleMaterialIndices = reinterpret_cast<const unsigned int*>(hitData->triangleMaterialIndices);

    const unsigned int primitiveIndex = optixGetPrimitiveIndex();
    const TriangleIndices triangle = indices[primitiveIndex];
    const float2 barycentrics = optixGetTriangleBarycentrics();
    const float b0 = 1.0f - barycentrics.x - barycentrics.y;
    const float b1 = barycentrics.x;
    const float b2 = barycentrics.y;

    const float3 v0 = vertices[triangle.x];
    const float3 v1 = vertices[triangle.y];
    const float3 v2 = vertices[triangle.z];

    const float3 geometricNormal = Normalize(Cross(v1 - v0, v2 - v0));
    const float3 facingNormal = optixIsFrontFaceHit() ? geometricNormal : -geometricNormal;

    const float3 origin = optixGetWorldRayOrigin();
    const float3 direction = optixGetWorldRayDirection();
    const float distance = optixGetRayTmax();

    Payload* payload = GetPayloadPointer();
    payload->hit = 1;
    payload->hitNormal = Normalize(facingNormal);
    payload->hitPosition = origin + direction * distance;
    payload->materialIndex = static_cast<int>(triangleMaterialIndices[primitiveIndex]);
}

extern "C" __global__ void __raygen__rg() {
    const uint3 launchIndex = optixGetLaunchIndex();
    const uint3 launchDimensions = optixGetLaunchDimensions();

    const unsigned int pixelIndex = launchIndex.y * params.imageWidth + launchIndex.x;
    const unsigned int seedFrame = params.settings.enableAccumulation != 0 ? (params.frameIndex + 1u) : 1u;
    unsigned int seed = Tea(pixelIndex, seedFrame);

    const float aspect = static_cast<float>(params.imageWidth) / static_cast<float>(params.imageHeight);
    const float3 eye = ToFloat3(params.cameraPosition);
    const float3 forward = Normalize(ToFloat3(params.cameraForward));
    const float3 right = Normalize(ToFloat3(params.cameraRight));
    const float3 up = Normalize(ToFloat3(params.cameraUp));

    float3 accumulated = make_float3(0.0f, 0.0f, 0.0f);

    for (int sampleIndex = 0; sampleIndex < params.settings.samplesPerPixel; ++sampleIndex) {
        const float jitterX = Random(seed);
        const float jitterY = Random(seed);

        const float screenX = ((static_cast<float>(launchIndex.x) + jitterX) / static_cast<float>(launchDimensions.x)) * 2.0f - 1.0f;
        const float screenY = ((static_cast<float>(launchIndex.y) + jitterY) / static_cast<float>(launchDimensions.y)) * 2.0f - 1.0f;

        float3 origin = eye;
        float3 direction = Normalize(
            forward +
            right * (screenX * aspect * params.tanHalfFovY) -
            up * (screenY * params.tanHalfFovY));

        float3 throughput = make_float3(1.0f, 1.0f, 1.0f);
        float3 radiance = make_float3(0.0f, 0.0f, 0.0f);

        for (int bounce = 0; bounce < params.settings.maxBounces; ++bounce) {
            Payload payload;
            payload.hit = 0;
            payload.hitPosition = make_float3(0.0f, 0.0f, 0.0f);
            payload.hitNormal = make_float3(0.0f, 0.0f, 0.0f);
            payload.materialIndex = -1;

            unsigned int payloadLow = static_cast<unsigned int>(reinterpret_cast<unsigned long long>(&payload));
            unsigned int payloadHigh = static_cast<unsigned int>(reinterpret_cast<unsigned long long>(&payload) >> 32);

            optixTrace(
                params.handle,
                origin,
                direction,
                0.001f,
                1e16f,
                0.0f,
                OptixVisibilityMask(255),
                OPTIX_RAY_FLAG_DISABLE_ANYHIT,
                0,
                1,
                0,
                payloadLow,
                payloadHigh);

            if (payload.hit == 0) {
                radiance = radiance + throughput * GetSkyColor(direction);
                break;
            }

            const float3 normal = Dot(payload.hitNormal, direction) < 0.0f ? payload.hitNormal : -payload.hitNormal;
            const NativeMaterial* materials = reinterpret_cast<const NativeMaterial*>(params.materials);
            const NativeMaterial material = materials[payload.materialIndex];
            const float3 albedo = make_float3(material.albedoR, material.albedoG, material.albedoB);
            const float opacity = fminf(fmaxf(material.opacity, 0.0f), 1.0f);

            if (params.settings.enableNormalDebug != 0) {
                radiance = radiance + (normal * 0.5f + make_float3(0.5f, 0.5f, 0.5f));
                break;
            }

            if (opacity < 1.0f && Random(seed) > opacity) {
                throughput = throughput * albedo;
                origin = payload.hitPosition + direction * 0.001f;
                continue;
            }

            radiance = radiance + throughput * albedo * params.settings.ambientIntensity;

            if (params.settings.enableSunLight != 0) {
                const float3 sunDirection = Normalize(make_float3(
                    params.settings.sunDirectionX,
                    params.settings.sunDirectionY,
                    params.settings.sunDirectionZ));
                const float3 toSun = SampleCone(-sunDirection, fmaxf(params.settings.sunAngularRadius, 0.0f), seed);
                float visibility = 1.0f;

                if (params.settings.enableHardShadows != 0) {
                    visibility = TraceOcclusion(payload.hitPosition + normal * 0.001f, toSun, 1e16f) == 0 ? 1.0f : 0.0f;
                }

                if (visibility > 0.0f) {
                    const float ndotl = fmaxf(0.0f, Dot(normal, toSun));
                    radiance = radiance + throughput * albedo * (params.settings.sunIntensity * ndotl * visibility);
                }
            }

            throughput = throughput * albedo;
            origin = payload.hitPosition + normal * 0.001f;
            const float reflectivity = fminf(fmaxf(material.reflectivity, 0.0f), 1.0f);
            if (material.reflective != 0) {
                const float3 reflectedDirection = Normalize(Reflect(direction, normal));
                const float3 diffuseDirection = SampleHemisphere(normal, seed);
                direction = Normalize(Lerp(diffuseDirection, reflectedDirection, reflectivity));
            } else {
                direction = SampleHemisphere(normal, seed);
            }

            if (bounce + 1 >= params.settings.russianRouletteStartBounce) {
                const float survivalProbability = fminf(MaxComponent(throughput), 0.95f);
                if (bounce + 1 >= params.settings.minBounces && Random(seed) > survivalProbability) {
                    break;
                }
                throughput = throughput / fmaxf(survivalProbability, 1e-4f);
            }
        }

        accumulated = accumulated + radiance;
    }

    const float3 sampleRadiance = accumulated / static_cast<float>(params.settings.samplesPerPixel);

    if (params.settings.enableAccumulation != 0) {
        const float4 previous = params.frameIndex == 0
            ? make_float4(0.0f, 0.0f, 0.0f, 0.0f)
            : params.accumulation[pixelIndex];
        const float3 sum = make_float3(previous.x, previous.y, previous.z) + sampleRadiance;
        params.accumulation[pixelIndex] = make_float4(sum.x, sum.y, sum.z, 0.0f);
        const float3 averaged = sum / static_cast<float>(params.frameIndex + 1u);
        params.beauty[pixelIndex] = make_float4(averaged.x, averaged.y, averaged.z, 1.0f);
        return;
    }

    params.beauty[pixelIndex] = make_float4(sampleRadiance.x, sampleRadiance.y, sampleRadiance.z, 1.0f);
}
)";

class NativeRenderer {
public:
    explicit NativeRenderer(int width, int height) {
        InitializeOptix();
        CreateDenoiser();
        CreatePipeline();
        CreateSbt();
        Resize(width, height);
    }

    ~NativeRenderer() {
        Destroy();
    }

    void Resize(int width, int height) {
        if (width <= 0 || height <= 0) {
            throw OptixError("Invalid render size.");
        }

        if (width == width_ && height == height_ && beautyBuffer_ != 0) {
            return;
        }

        width_ = width;
        height_ = height;

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

        SetupDenoiser();
    }

    void Render(
        const NativeCamera& camera,
        const NativeRenderSettings& settings,
        const float* vertices,
        int vertexFloatCount,
        const float* normals,
        int normalFloatCount,
        const uint16_t* indices,
        int indexCount,
        const uint32_t* triangleMaterialIndices,
        int triangleMaterialIndexCount,
        const NativeMaterial* materials,
        int materialCount,
        unsigned int frameIndex,
        uint8_t* outputPixels,
        int outputLength) {

        if (vertexFloatCount <= 0 || normalFloatCount != vertexFloatCount || indexCount <= 0 || (indexCount % 3) != 0) {
            throw OptixError("Scene buffers are invalid.");
        }

        const auto vertexCount = static_cast<unsigned int>(vertexFloatCount / 3);
        const auto triangleCount = static_cast<unsigned int>(indexCount / 3);
        if (triangleMaterialIndexCount != static_cast<int>(triangleCount) || materialCount <= 0) {
            throw OptixError("Scene material buffers are invalid.");
        }
        const auto requiredLength = width_ * height_ * 4;

        if (outputLength < requiredLength) {
            throw OptixError("Output buffer is too small.");
        }

        UploadScene(vertices, normals, vertexCount, indices, triangleCount, triangleMaterialIndices, materials, static_cast<unsigned int>(materialCount));

        LaunchParams params{};
        params.beauty = reinterpret_cast<float4*>(beautyBuffer_);
        params.accumulation = reinterpret_cast<float4*>(accumulationBuffer_);
        params.materials = materialBuffer_;
        params.imageWidth = static_cast<unsigned int>(width_);
        params.imageHeight = static_cast<unsigned int>(height_);
        params.cameraPosition = Float3{camera.positionX, camera.positionY, camera.positionZ};
        const Float3 target = Float3{camera.targetX, camera.targetY, camera.targetZ};
        params.cameraForward = Normalize(Subtract(target, params.cameraPosition));
        params.cameraRight = Normalize(Cross(params.cameraForward, Float3{0.0f, 1.0f, 0.0f}));
        params.cameraUp = Normalize(Cross(params.cameraRight, params.cameraForward));
        params.tanHalfFovY = std::tan(camera.fovY * 0.5f * 3.14159265359f / 180.0f);
        params.settings = settings;
        params.handle = gasHandle_;
        params.frameIndex = frameIndex;

        CheckCuda(cudaMemcpy(reinterpret_cast<void*>(launchParamsBuffer_), &params, sizeof(params), cudaMemcpyHostToDevice), "cudaMemcpy(launchParams)");

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

        const CUdeviceptr beautySource = settings.enableDenoiser != 0
            ? DenoiseBeauty()
            : beautyBuffer_;

        const auto beautySize = static_cast<size_t>(width_) * static_cast<size_t>(height_) * sizeof(float4);
        hostBeautyBuffer_.resize(static_cast<size_t>(width_) * static_cast<size_t>(height_));
        CheckCuda(
            cudaMemcpy(hostBeautyBuffer_.data(), reinterpret_cast<void*>(beautySource), beautySize, cudaMemcpyDeviceToHost),
            "cudaMemcpy(hostBeautyBuffer)");

        ToneMapToPixels(hostBeautyBuffer_.data(), static_cast<size_t>(width_) * static_cast<size_t>(height_), settings, outputPixels);
    }

private:
    void InitializeOptix() {
        int deviceCount = 0;
        CheckCuda(cudaGetDeviceCount(&deviceCount), "cudaGetDeviceCount");
        if (deviceCount <= 0) {
            throw OptixError("No CUDA-capable NVIDIA device is available for OptiX.");
        }

        CheckCuda(cudaSetDevice(0), "cudaSetDevice");
        CheckCuda(cudaFree(nullptr), "cudaFree(init)");
        CheckOptix(optixInit(), "optixInit");

        OptixDeviceContextOptions options{};
        options.logCallbackLevel = 4;
        options.logCallbackFunction = &LogCallback;

        CheckOptix(optixDeviceContextCreate(nullptr, &options, &context_), "optixDeviceContextCreate");
        CheckCuda(cudaStreamCreate(&stream_), "cudaStreamCreate");
    }

    void CreateDenoiser() {
        OptixDenoiserOptions denoiserOptions{};
        denoiserOptions.guideAlbedo = 0;
        denoiserOptions.guideNormal = 0;
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

    void CreatePipeline() {
        const auto ptx = CompilePtx();

        OptixModuleCompileOptions moduleCompileOptions{};
        moduleCompileOptions.maxRegisterCount = OPTIX_COMPILE_DEFAULT_MAX_REGISTER_COUNT;
        moduleCompileOptions.optLevel = OPTIX_COMPILE_OPTIMIZATION_DEFAULT;
        moduleCompileOptions.debugLevel = OPTIX_COMPILE_DEBUG_LEVEL_MINIMAL;

        OptixPipelineCompileOptions pipelineCompileOptions{};
        pipelineCompileOptions.usesMotionBlur = 0;
        pipelineCompileOptions.traversableGraphFlags = OPTIX_TRAVERSABLE_GRAPH_FLAG_ALLOW_SINGLE_GAS;
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
        CheckOptix(optixPipelineSetStackSize(pipeline_, directCallableStackTraversal, directCallableStackState, continuationStack, 1), "optixPipelineSetStackSize");
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

        HitGroupRecord hitRecord{};
        CheckOptix(optixSbtRecordPackHeader(hitgroupProgramGroup_, &hitRecord), "optixSbtRecordPackHeader(hitgroup)");
        CheckCuda(cudaMalloc(reinterpret_cast<void**>(&hitgroupRecordBuffer_), sizeof(hitRecord)), "cudaMalloc(hitgroupRecord)");
        CheckCuda(cudaMemcpy(reinterpret_cast<void*>(hitgroupRecordBuffer_), &hitRecord, sizeof(hitRecord), cudaMemcpyHostToDevice), "cudaMemcpy(hitgroupRecord)");

        CheckCuda(cudaMalloc(reinterpret_cast<void**>(&launchParamsBuffer_), sizeof(LaunchParams)), "cudaMalloc(launchParamsBuffer)");

        sbt_.raygenRecord = raygenRecordBuffer_;
        sbt_.missRecordBase = missRecordBuffer_;
        sbt_.missRecordStrideInBytes = sizeof(MissRecord);
        sbt_.missRecordCount = 1;
        sbt_.hitgroupRecordBase = hitgroupRecordBuffer_;
        sbt_.hitgroupRecordStrideInBytes = sizeof(HitGroupRecord);
        sbt_.hitgroupRecordCount = 1;
    }

    void UploadScene(
        const float* vertices,
        const float* normals,
        unsigned int vertexCount,
        const uint16_t* indices,
        unsigned int triangleCount,
        const uint32_t* triangleMaterialIndices,
        const NativeMaterial* materials,
        unsigned int materialCount) {
        const auto vertexBytes = static_cast<size_t>(vertexCount) * sizeof(float3);
        const auto normalBytes = static_cast<size_t>(vertexCount) * sizeof(float3);
        const auto indexBytes = static_cast<size_t>(triangleCount) * sizeof(TriangleIndices);
        const auto triangleMaterialIndexBytes = static_cast<size_t>(triangleCount) * sizeof(uint32_t);
        const auto materialBytes = static_cast<size_t>(materialCount) * sizeof(NativeMaterial);

        EnsureBuffer(vertexBuffer_, vertexBufferCapacity_, vertexBytes);
        EnsureBuffer(normalBuffer_, normalBufferCapacity_, normalBytes);
        EnsureBuffer(indexBuffer_, indexBufferCapacity_, indexBytes);
        EnsureBuffer(triangleMaterialIndexBuffer_, triangleMaterialIndexCapacity_, triangleMaterialIndexBytes);
        EnsureBuffer(materialBuffer_, materialBufferCapacity_, materialBytes);

        CheckCuda(cudaMemcpy(reinterpret_cast<void*>(vertexBuffer_), vertices, vertexBytes, cudaMemcpyHostToDevice), "cudaMemcpy(vertices)");
        CheckCuda(cudaMemcpy(reinterpret_cast<void*>(normalBuffer_), normals, normalBytes, cudaMemcpyHostToDevice), "cudaMemcpy(normals)");
        CheckCuda(cudaMemcpy(reinterpret_cast<void*>(indexBuffer_), indices, indexBytes, cudaMemcpyHostToDevice), "cudaMemcpy(indices)");
        CheckCuda(cudaMemcpy(reinterpret_cast<void*>(triangleMaterialIndexBuffer_), triangleMaterialIndices, triangleMaterialIndexBytes, cudaMemcpyHostToDevice), "cudaMemcpy(triangleMaterialIndices)");
        CheckCuda(cudaMemcpy(reinterpret_cast<void*>(materialBuffer_), materials, materialBytes, cudaMemcpyHostToDevice), "cudaMemcpy(materials)");

        const uint32_t flags[] = {OPTIX_GEOMETRY_FLAG_NONE};

        OptixBuildInput buildInput{};
        buildInput.type = OPTIX_BUILD_INPUT_TYPE_TRIANGLES;
        buildInput.triangleArray.vertexFormat = OPTIX_VERTEX_FORMAT_FLOAT3;
        buildInput.triangleArray.vertexStrideInBytes = sizeof(float3);
        buildInput.triangleArray.numVertices = vertexCount;
        buildInput.triangleArray.vertexBuffers = &vertexBuffer_;
        buildInput.triangleArray.indexFormat = OPTIX_INDICES_FORMAT_UNSIGNED_SHORT3;
        buildInput.triangleArray.indexStrideInBytes = sizeof(TriangleIndices);
        buildInput.triangleArray.numIndexTriplets = triangleCount;
        buildInput.triangleArray.indexBuffer = indexBuffer_;
        buildInput.triangleArray.flags = flags;
        buildInput.triangleArray.numSbtRecords = 1;

        OptixAccelBuildOptions accelOptions{};
        accelOptions.buildFlags = OPTIX_BUILD_FLAG_ALLOW_COMPACTION;
        accelOptions.operation = OPTIX_BUILD_OPERATION_BUILD;

        OptixAccelBufferSizes sizes{};
        CheckOptix(optixAccelComputeMemoryUsage(context_, &accelOptions, &buildInput, 1, &sizes), "optixAccelComputeMemoryUsage");

        EnsureBuffer(gasScratchBuffer_, gasScratchCapacity_, sizes.tempSizeInBytes);
        EnsureBuffer(gasOutputBuffer_, gasOutputCapacity_, sizes.outputSizeInBytes);

        CheckOptix(optixAccelBuild(
            context_,
            stream_,
            &accelOptions,
            &buildInput,
            1,
            gasScratchBuffer_,
            sizes.tempSizeInBytes,
            gasOutputBuffer_,
            sizes.outputSizeInBytes,
            &gasHandle_,
            nullptr,
            0), "optixAccelBuild");

        HitGroupRecord hitRecord{};
        CheckOptix(optixSbtRecordPackHeader(hitgroupProgramGroup_, &hitRecord), "optixSbtRecordPackHeader(hitgroup-update)");
        hitRecord.data.vertices = vertexBuffer_;
        hitRecord.data.normals = normalBuffer_;
        hitRecord.data.indices = indexBuffer_;
        hitRecord.data.triangleMaterialIndices = triangleMaterialIndexBuffer_;
        hitRecord.data.materials = materialBuffer_;
        CheckCuda(cudaMemcpy(reinterpret_cast<void*>(hitgroupRecordBuffer_), &hitRecord, sizeof(hitRecord), cudaMemcpyHostToDevice), "cudaMemcpy(hitgroupRecord-update)");
    }

    std::string CompilePtx() {
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

    void Destroy() {
        SafeCudaFree(beautyBuffer_);
        SafeCudaFree(denoisedBeautyBuffer_);
        SafeCudaFree(accumulationBuffer_);
        SafeCudaFree(launchParamsBuffer_);
        SafeCudaFree(raygenRecordBuffer_);
        SafeCudaFree(missRecordBuffer_);
        SafeCudaFree(hitgroupRecordBuffer_);
        SafeCudaFree(vertexBuffer_);
        SafeCudaFree(normalBuffer_);
        SafeCudaFree(indexBuffer_);
        SafeCudaFree(triangleMaterialIndexBuffer_);
        SafeCudaFree(materialBuffer_);
        SafeCudaFree(denoiserStateBuffer_);
        SafeCudaFree(denoiserScratchBuffer_);
        SafeCudaFree(gasScratchBuffer_);
        SafeCudaFree(gasOutputBuffer_);

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
        if (denoiser_ != nullptr) {
            optixDenoiserDestroy(denoiser_);
            denoiser_ = nullptr;
        }
        if (stream_ != nullptr) {
            cudaStreamDestroy(stream_);
            stream_ = nullptr;
        }
        if (context_ != nullptr) {
            optixDeviceContextDestroy(context_);
            context_ = nullptr;
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
            cudaFree(reinterpret_cast<void*>(buffer));
            buffer = 0;
        }
    }

    static void ToneMapToPixels(const float4* hdrPixels, size_t pixelCount, const NativeRenderSettings& settings, uint8_t* outputPixels) {
        const float exposure = std::max(settings.exposure, 0.001f);
        const float gamma = std::max(settings.gamma, 0.001f);

        for (size_t index = 0; index < pixelCount; ++index) {
            const size_t outputIndex = index * 4;

            auto tonemapChannel = [exposure, gamma](float value) -> uint8_t {
                const float exposed = std::clamp(value * exposure, 0.0f, 1.0f);
                const float corrected = std::pow(exposed, 1.0f / gamma);
                return static_cast<uint8_t>(std::clamp(corrected * 255.0f, 0.0f, 255.0f));
            };

            outputPixels[outputIndex + 0] = tonemapChannel(hdrPixels[index].x);
            outputPixels[outputIndex + 1] = tonemapChannel(hdrPixels[index].y);
            outputPixels[outputIndex + 2] = tonemapChannel(hdrPixels[index].z);
            outputPixels[outputIndex + 3] = 255;
        }
    }

    OptixDeviceContext context_ = nullptr;
    cudaStream_t stream_ = nullptr;
    OptixDenoiser denoiser_ = nullptr;
    OptixModule module_ = nullptr;
    OptixPipeline pipeline_ = nullptr;
    OptixProgramGroup raygenProgramGroup_ = nullptr;
    OptixProgramGroup missProgramGroup_ = nullptr;
    OptixProgramGroup hitgroupProgramGroup_ = nullptr;
    OptixShaderBindingTable sbt_{};

    int width_ = 0;
    int height_ = 0;

    CUdeviceptr beautyBuffer_ = 0;
    CUdeviceptr denoisedBeautyBuffer_ = 0;
    CUdeviceptr accumulationBuffer_ = 0;
    CUdeviceptr launchParamsBuffer_ = 0;
    CUdeviceptr raygenRecordBuffer_ = 0;
    CUdeviceptr missRecordBuffer_ = 0;
    CUdeviceptr hitgroupRecordBuffer_ = 0;

    CUdeviceptr vertexBuffer_ = 0;
    CUdeviceptr normalBuffer_ = 0;
    CUdeviceptr indexBuffer_ = 0;
    CUdeviceptr triangleMaterialIndexBuffer_ = 0;
    CUdeviceptr materialBuffer_ = 0;
    CUdeviceptr denoiserStateBuffer_ = 0;
    CUdeviceptr denoiserScratchBuffer_ = 0;
    CUdeviceptr gasScratchBuffer_ = 0;
    CUdeviceptr gasOutputBuffer_ = 0;

    size_t vertexBufferCapacity_ = 0;
    size_t normalBufferCapacity_ = 0;
    size_t indexBufferCapacity_ = 0;
    size_t triangleMaterialIndexCapacity_ = 0;
    size_t materialBufferCapacity_ = 0;
    size_t denoiserStateCapacity_ = 0;
    size_t denoiserScratchCapacity_ = 0;
    size_t gasScratchCapacity_ = 0;
    size_t gasOutputCapacity_ = 0;

    OptixDenoiserSizes denoiserSizes_{};
    OptixTraversableHandle gasHandle_ = 0;
    std::vector<float4> hostBeautyBuffer_;
};

void CopyError(char* error, int errorCapacity, const std::string& message) {
    if (error == nullptr || errorCapacity <= 0) {
        return;
    }

    std::snprintf(error, static_cast<size_t>(errorCapacity), "%s", message.c_str());
}

} // namespace

extern "C" bool roptixCreate(int width, int height, void** handle, char* error, int errorCapacity) {
    try {
        if (handle == nullptr) {
            throw OptixError("Handle pointer is null.");
        }

        auto renderer = std::make_unique<NativeRenderer>(width, height);
        *handle = renderer.release();
        CopyError(error, errorCapacity, "");
        return true;
    } catch (const std::exception& exception) {
        CopyError(error, errorCapacity, exception.what());
        return false;
    }
}

extern "C" void roptixDestroy(void* handle) {
    delete static_cast<NativeRenderer*>(handle);
}

extern "C" bool roptixResize(void* handle, int width, int height, char* error, int errorCapacity) {
    try {
        if (handle == nullptr) {
            throw OptixError("Renderer handle is null.");
        }

        static_cast<NativeRenderer*>(handle)->Resize(width, height);
        CopyError(error, errorCapacity, "");
        return true;
    } catch (const std::exception& exception) {
        CopyError(error, errorCapacity, exception.what());
        return false;
    }
}

extern "C" bool roptixRender(
    void* handle,
    int width,
    int height,
    NativeCamera camera,
    NativeRenderSettings settings,
    const float* vertices,
    int vertexFloatCount,
    const float* normals,
    int normalFloatCount,
    const uint16_t* indices,
    int indexCount,
    const uint32_t* triangleMaterialIndices,
    int triangleMaterialIndexCount,
    const NativeMaterial* materials,
    int materialCount,
    unsigned int frameIndex,
    uint8_t* outputPixels,
    int outputLength,
    char* error,
    int errorCapacity) {
    try {
        if (handle == nullptr) {
            throw OptixError("Renderer handle is null.");
        }

        auto* renderer = static_cast<NativeRenderer*>(handle);
        renderer->Resize(width, height);
        renderer->Render(
            camera,
            settings,
            vertices,
            vertexFloatCount,
            normals,
            normalFloatCount,
            indices,
            indexCount,
            triangleMaterialIndices,
            triangleMaterialIndexCount,
            materials,
            materialCount,
            frameIndex,
            outputPixels,
            outputLength);
        CopyError(error, errorCapacity, "");
        return true;
    } catch (const std::exception& exception) {
        CopyError(error, errorCapacity, exception.what());
        return false;
    }
}
