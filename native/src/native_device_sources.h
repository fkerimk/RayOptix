#pragma once

inline constexpr const char* kOptixDeviceSource = R"(
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
    float reflectivity;
    int albedoTextureIndex;
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
    unsigned long long texturePixels;
    unsigned long long textureMetadata;
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
    unsigned long long vertices;
    unsigned long long normals;
    unsigned long long texCoords;
    unsigned long long indices;
    unsigned long long triangleMaterialIndices;
    unsigned long long materials;
};

struct TriangleIndices {
    unsigned int x;
    unsigned int y;
    unsigned int z;
};

struct Payload {
    float3 hitPosition;
    float3 hitNormal;
    float2 texCoord;
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

static __forceinline__ __device__ float WrapUv(float value) {
    return value - floorf(value);
}

static __forceinline__ __device__ float3 SampleAlbedo(const NativeMaterial& material, float2 texCoord) {
    const float3 baseColor = make_float3(material.albedoR, material.albedoG, material.albedoB);

    if (material.albedoTextureIndex < 0 || static_cast<unsigned int>(material.albedoTextureIndex) >= params.textureCount) {
        return baseColor;
    }

    const int* textureMetadata = reinterpret_cast<const int*>(params.textureMetadata);
    const uchar4* texturePixels = reinterpret_cast<const uchar4*>(params.texturePixels);
    const int metadataIndex = material.albedoTextureIndex * 3;
    const int pixelOffset = textureMetadata[metadataIndex];
    const int textureWidth = textureMetadata[metadataIndex + 1];
    const int textureHeight = textureMetadata[metadataIndex + 2];

    if (textureWidth <= 0 || textureHeight <= 0) {
        return baseColor;
    }

    const float wrappedU = WrapUv(texCoord.x);
    const float wrappedV = WrapUv(texCoord.y);
    const int x = min(max(static_cast<int>(wrappedU * static_cast<float>(textureWidth)), 0), textureWidth - 1);
    const int y = min(max(static_cast<int>(wrappedV * static_cast<float>(textureHeight)), 0), textureHeight - 1);
    const uchar4 pixel = texturePixels[pixelOffset + y * textureWidth + x];
    const float3 textureColor = make_float3(
        static_cast<float>(pixel.x) / 255.0f,
        static_cast<float>(pixel.y) / 255.0f,
        static_cast<float>(pixel.z) / 255.0f);

    return baseColor * textureColor;
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
        params.iasHandle,
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
    payload->texCoord = make_float2(0.0f, 0.0f);
    payload->materialIndex = -1;
}

extern "C" __global__ void __closesthit__ch() {
    const HitGroupData* hitData = reinterpret_cast<const HitGroupData*>(optixGetSbtDataPointer());
    const float3* vertices = reinterpret_cast<const float3*>(hitData->vertices);
    const float3* normals = reinterpret_cast<const float3*>(hitData->normals);
    const float2* texCoords = reinterpret_cast<const float2*>(hitData->texCoords);
    const TriangleIndices* indices = reinterpret_cast<const TriangleIndices*>(hitData->indices);

    const unsigned int primitiveIndex = optixGetPrimitiveIndex();
    const TriangleIndices triangle = indices[primitiveIndex];
    const float2 barycentrics = optixGetTriangleBarycentrics();
    const float b0 = 1.0f - barycentrics.x - barycentrics.y;
    const float b1 = barycentrics.x;
    const float b2 = barycentrics.y;

    const float3 v0 = vertices[triangle.x];
    const float3 v1 = vertices[triangle.y];
    const float3 v2 = vertices[triangle.z];
    const float3 n0 = normals[triangle.x];
    const float3 n1 = normals[triangle.y];
    const float3 n2 = normals[triangle.z];
    const float2 uv0 = texCoords[triangle.x];
    const float2 uv1 = texCoords[triangle.y];
    const float2 uv2 = texCoords[triangle.z];

    const float3 geometricNormal = Normalize(Cross(v1 - v0, v2 - v0));
    float3 shadingNormal = Normalize(n0 * b0 + n1 * b1 + n2 * b2);
    if (Dot(shadingNormal, geometricNormal) < 0.0f) {
        shadingNormal = -shadingNormal;
    }

    float3 facingNormal = optixIsFrontFaceHit() ? shadingNormal : -shadingNormal;
    facingNormal = optixTransformNormalFromObjectToWorldSpace(facingNormal);

    const float3 origin = optixGetWorldRayOrigin();
    const float3 direction = optixGetWorldRayDirection();
    const float distance = optixGetRayTmax();

    Payload* payload = GetPayloadPointer();
    payload->hit = 1;
    payload->hitNormal = Normalize(facingNormal);
    payload->hitPosition = origin + direction * distance;
    payload->texCoord = make_float2(
        uv0.x * b0 + uv1.x * b1 + uv2.x * b2,
        uv0.y * b0 + uv1.y * b1 + uv2.y * b2);
    payload->materialIndex = static_cast<int>(optixGetInstanceId());
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
            payload.texCoord = make_float2(0.0f, 0.0f);
            payload.materialIndex = -1;

            unsigned int payloadLow = static_cast<unsigned int>(reinterpret_cast<unsigned long long>(&payload));
            unsigned int payloadHigh = static_cast<unsigned int>(reinterpret_cast<unsigned long long>(&payload) >> 32);

            optixTrace(
                params.iasHandle,
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
                    const float3 primaryAlbedo = SampleAlbedo(primaryMaterial, payload.texCoord);
                    params.normals[pixelIndex] = make_float4(payload.hitNormal.x, payload.hitNormal.y, payload.hitNormal.z, 0.0f);
                    params.roughness[pixelIndex] = 1.0f - primaryReflectivity;
                    params.diffuseAlbedo[pixelIndex] = make_float4(primaryAlbedo.x, primaryAlbedo.y, primaryAlbedo.z, 1.0f);
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
            const float3 albedo = SampleAlbedo(material, payload.texCoord);
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
            if (reflectivity > 0.0f) {
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

inline constexpr const char* kPresentDeviceSource = R"(
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
