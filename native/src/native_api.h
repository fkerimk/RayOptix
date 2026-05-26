#pragma once

#include <cstdint>

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
    float reflectivity;
    int albedoTextureIndex;
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

bool CreateRendererHandle(int renderWidth, int renderHeight, int outputWidth, int outputHeight, void **handle,
                          char *error, int errorCapacity);

void DestroyRendererHandle(void *handle);

void ReleaseRendererOutputTexture(void *handle, unsigned int textureId);

bool ResizeRendererHandle(void *handle, int renderWidth, int renderHeight, int outputWidth, int outputHeight,
                          char *error, int errorCapacity);

bool UploadMeshRendererHandle(
    void *handle,
    int meshId,
    const float *vertices,
    int vertexFloatCount,
    const float *normals,
    int normalFloatCount,
    const float *texCoords,
    int texCoordFloatCount,
    const uint32_t *indices,
    int indexCount,
    char *error,
    int errorCapacity);

bool UpdateMeshVerticesRendererHandle(
    void *handle,
    int meshId,
    const float *vertices,
    int vertexFloatCount,
    char *error,
    int errorCapacity);

bool RenderInstancesRendererHandle(
    void *handle,
    int renderWidth,
    int renderHeight,
    int outputWidth,
    int outputHeight,
    const NativeCamera &camera,
    const NativeRenderSettings &settings,
    const int *meshIds,
    int instanceCount,
    const int *materialIndices,
    const float *transforms,
    const float *materialParameters,
    int materialFloatCount,
    const int32_t *materialAlbedoTextureIndices,
    const uint8_t *texturePixels,
    int texturePixelByteCount,
    const int32_t *textureMetadata,
    int textureMetadataCount,
    unsigned int frameIndex,
    unsigned int outputTextureId,
    NativeFrameStats *stats,
    char *error,
    int errorCapacity);
