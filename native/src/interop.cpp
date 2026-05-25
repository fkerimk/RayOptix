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

#if defined(__linux__)
#include <GL/gl.h>
#endif

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
    int denoiserIntervalFrames;
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

struct NativeFrameStats {
    double totalMs;
    double uploadSceneMs;
    double launchMs;
    double denoiseMs;
    double readbackMs;
    double toneMapMs;
    int denoisedThisFrame;
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
    float currentViewProjection[16];
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

const char* kOptixDeviceSource = R"(
#include <cuda_runtime.h>
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
    int denoiserIntervalFrames;
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
    float* depth;
    float4* worldPosition;
    float4* normals;
    float* roughness;
    float4* diffuseAlbedo;
    float4* specularAlbedo;
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
    float currentViewProjection[16];
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

static __forceinline__ __device__ unsigned char ToneMapChannel(float value, float exposure, float inverseGamma) {
    const float exposed = fminf(fmaxf(value * exposure, 0.0f), 1.0f);
    const float corrected = powf(exposed, inverseGamma);
    return static_cast<unsigned char>(fminf(fmaxf(corrected * 255.0f, 0.0f), 255.0f));
}

static __forceinline__ __device__ float4 MulPoint(const float* matrix, float3 point) {
    return make_float4(
        matrix[0] * point.x + matrix[1] * point.y + matrix[2] * point.z + matrix[3],
        matrix[4] * point.x + matrix[5] * point.y + matrix[6] * point.z + matrix[7],
        matrix[8] * point.x + matrix[9] * point.y + matrix[10] * point.z + matrix[11],
        matrix[12] * point.x + matrix[13] * point.y + matrix[14] * point.z + matrix[15]);
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
        OPTIX_RAY_FLAG_DISABLE_ANYHIT | OPTIX_RAY_FLAG_CULL_BACK_FACING_TRIANGLES,
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
    bool wrotePrimary = false;

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
                OPTIX_RAY_FLAG_DISABLE_ANYHIT | OPTIX_RAY_FLAG_CULL_BACK_FACING_TRIANGLES,
                0,
                1,
                0,
                payloadLow,
                payloadHigh);

            if (!wrotePrimary) {
                wrotePrimary = true;
                if (payload.hit == 0) {
                    params.depth[pixelIndex] = 1.0f;
                    params.worldPosition[pixelIndex] = make_float4(0.0f, 0.0f, 0.0f, 0.0f);
                    params.normals[pixelIndex] = make_float4(0.0f, 0.0f, 0.0f, 0.0f);
                    params.roughness[pixelIndex] = 1.0f;
                    params.diffuseAlbedo[pixelIndex] = make_float4(0.0f, 0.0f, 0.0f, 0.0f);
                    params.specularAlbedo[pixelIndex] = make_float4(0.0f, 0.0f, 0.0f, 0.0f);
                } else {
                    const float4 clip = MulPoint(params.currentViewProjection, payload.hitPosition);
                    const float inverseW = fabsf(clip.w) > 1e-6f ? (1.0f / clip.w) : 0.0f;
                    const float ndcZ = clip.z * inverseW;
                    params.depth[pixelIndex] = fminf(fmaxf(ndcZ * 0.5f + 0.5f, 0.0f), 1.0f);
                    params.worldPosition[pixelIndex] = make_float4(payload.hitPosition.x, payload.hitPosition.y, payload.hitPosition.z, 1.0f);
                    const NativeMaterial* primaryMaterials = reinterpret_cast<const NativeMaterial*>(params.materials);
                    const NativeMaterial primaryMaterial = primaryMaterials[payload.materialIndex];
                    const float primaryReflectivity = fminf(fmaxf(primaryMaterial.reflectivity, 0.0f), 1.0f);
                    params.normals[pixelIndex] = make_float4(payload.hitNormal.x, payload.hitNormal.y, payload.hitNormal.z, 0.0f);
                    params.roughness[pixelIndex] = 1.0f - primaryReflectivity;
                    params.diffuseAlbedo[pixelIndex] = make_float4(primaryMaterial.albedoR, primaryMaterial.albedoG, primaryMaterial.albedoB, 1.0f);
                    params.specularAlbedo[pixelIndex] = make_float4(primaryReflectivity, primaryReflectivity, primaryReflectivity, 1.0f);
                }
            }

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

extern "C" __global__ void ToneMapToSurface(
    const float4* hdrPixels,
    cudaSurfaceObject_t outputSurface,
    int sourceWidth,
    int sourceHeight,
    int outputWidth,
    int outputHeight,
    float exposure,
    float inverseGamma) {
    const int x = static_cast<int>(blockIdx.x * blockDim.x + threadIdx.x);
    const int y = static_cast<int>(blockIdx.y * blockDim.y + threadIdx.y);

    if (x >= outputWidth || y >= outputHeight) {
        return;
    }

    const int sourceX = min(max(x * sourceWidth / max(outputWidth, 1), 0), max(sourceWidth - 1, 0));
    const int sourceY = min(max(y * sourceHeight / max(outputHeight, 1), 0), max(sourceHeight - 1, 0));
    const float4 source = hdrPixels[sourceY * sourceWidth + sourceX];

    const uchar4 outPixel = make_uchar4(
        ToneMapChannel(source.x, exposure, inverseGamma),
        ToneMapChannel(source.y, exposure, inverseGamma),
        ToneMapChannel(source.z, exposure, inverseGamma),
        255);

    surf2Dwrite(outPixel, outputSurface, x * static_cast<int>(sizeof(uchar4)), y);
}
)";

const char* kPresentDeviceSource = R"(
#include <cuda_runtime.h>

static __forceinline__ __device__ unsigned char ToneMapChannel(float value, float exposure, float inverseGamma) {
    const float exposed = fminf(fmaxf(value * exposure, 0.0f), 1.0f);
    const float corrected = powf(exposed, inverseGamma);
    return static_cast<unsigned char>(fminf(fmaxf(corrected * 255.0f, 0.0f), 255.0f));
}

extern "C" __global__ void ToneMapToSurface(
    const float4* hdrPixels,
    cudaSurfaceObject_t outputSurface,
    unsigned int sourceWidth,
    unsigned int sourceHeight,
    unsigned int outputWidth,
    unsigned int outputHeight,
    float exposure,
    float inverseGamma) {
    const unsigned int x = blockIdx.x * blockDim.x + threadIdx.x;
    const unsigned int y = blockIdx.y * blockDim.y + threadIdx.y;

    if (x >= outputWidth || y >= outputHeight) {
        return;
    }

    const unsigned int safeOutputWidth = max(outputWidth, 1u);
    const unsigned int safeOutputHeight = max(outputHeight, 1u);
    const unsigned int sourceX = min(x * sourceWidth / safeOutputWidth, max(sourceWidth, 1u) - 1u);
    const unsigned int sourceY = min(y * sourceHeight / safeOutputHeight, max(sourceHeight, 1u) - 1u);
    const float4 source = hdrPixels[sourceY * sourceWidth + sourceX];

    const uchar4 outPixel = make_uchar4(
        ToneMapChannel(source.x, exposure, inverseGamma),
        ToneMapChannel(source.y, exposure, inverseGamma),
        ToneMapChannel(source.z, exposure, inverseGamma),
        255);

    surf2Dwrite(outPixel, outputSurface, static_cast<int>(x * sizeof(uchar4)), static_cast<int>(y));
}
)";

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
        unsigned int outputTextureId,
        NativeFrameStats* stats) {
        const auto totalStart = std::chrono::steady_clock::now();
        NativeFrameStats localStats{};

        if (vertexFloatCount <= 0 || normalFloatCount != vertexFloatCount || indexCount <= 0 || (indexCount % 3) != 0) {
            throw OptixError("Scene buffers are invalid.");
        }

        const auto vertexCount = static_cast<unsigned int>(vertexFloatCount / 3);
        const auto triangleCount = static_cast<unsigned int>(indexCount / 3);
        if (triangleMaterialIndexCount != static_cast<int>(triangleCount) || materialCount <= 0) {
            throw OptixError("Scene material buffers are invalid.");
        }
        if (outputTextureId == 0) {
            throw OptixError("Output texture is invalid.");
        }

        EnsureOutputTextureInterop(outputTextureId);

        const auto uploadStart = std::chrono::steady_clock::now();
        UploadScene(vertices, normals, vertexCount, indices, triangleCount, triangleMaterialIndices, materials, static_cast<unsigned int>(materialCount));
        localStats.uploadSceneMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - uploadStart).count();

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
        if (cudaModule_ != nullptr) {
            IgnoreCudaDriver(cuModuleUnload(cudaModule_));
            cudaModule_ = nullptr;
        }
        if (denoiser_ != nullptr) {
            optixDenoiserDestroy(denoiser_);
            denoiser_ = nullptr;
        }
        if (stream_ != nullptr) {
            IgnoreCuda(cudaStreamDestroy(stream_));
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

    CUdeviceptr vertexBuffer_ = 0;
    CUdeviceptr normalBuffer_ = 0;
    CUdeviceptr indexBuffer_ = 0;
    CUdeviceptr triangleMaterialIndexBuffer_ = 0;
    CUdeviceptr materialBuffer_ = 0;
    CUdeviceptr denoiserStateBuffer_ = 0;
    CUdeviceptr denoiserScratchBuffer_ = 0;
    CUdeviceptr gasScratchBuffer_ = 0;
    CUdeviceptr gasOutputBuffer_ = 0;
    cudaGraphicsResource_t textureInteropResource_ = nullptr;
    unsigned int registeredOutputTextureId_ = 0;

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
    bool hasValidDenoisedBeauty_ = false;
    unsigned int presentFrameIndex_ = 0;
    float previousViewProjection_[16] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };
};

void CopyError(char* error, int errorCapacity, const std::string& message) {
    if (error == nullptr || errorCapacity <= 0) {
        return;
    }

    std::snprintf(error, static_cast<size_t>(errorCapacity), "%s", message.c_str());
}

} // namespace

extern "C" bool roptixCreate(int renderWidth, int renderHeight, int outputWidth, int outputHeight, void** handle, char* error, int errorCapacity) {
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

extern "C" void roptixDestroy(void* handle) {
    delete static_cast<NativeRenderer*>(handle);
}

extern "C" void roptixReleaseOutputTexture(void* handle, unsigned int textureId) {
    if (handle == nullptr) {
        return;
    }

    static_cast<NativeRenderer*>(handle)->ReleaseOutputTextureForInterop(textureId);
}

extern "C" bool roptixResize(void* handle, int renderWidth, int renderHeight, int outputWidth, int outputHeight, char* error, int errorCapacity) {
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

extern "C" bool roptixRender(
    void* handle,
    int renderWidth,
    int renderHeight,
    int outputWidth,
    int outputHeight,
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
            outputTextureId,
            stats);
        CopyError(error, errorCapacity, "");
        return true;
    } catch (const std::exception& exception) {
        CopyError(error, errorCapacity, exception.what());
        return false;
    }
}
