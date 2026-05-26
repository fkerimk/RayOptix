#include "native_api.h"

extern "C" bool roptixCreate(int renderWidth, int renderHeight, int outputWidth, int outputHeight, void **handle,
                             char *error, int errorCapacity) {
    return CreateRendererHandle(renderWidth, renderHeight, outputWidth, outputHeight, handle, error, errorCapacity);
}

extern "C" void roptixDestroy(void *handle) {
    DestroyRendererHandle(handle);
}

extern "C" void roptixReleaseOutputTexture(void *handle, unsigned int textureId) {
    ReleaseRendererOutputTexture(handle, textureId);
}

extern "C" bool roptixResize(void *handle, int renderWidth, int renderHeight, int outputWidth, int outputHeight,
                             char *error, int errorCapacity) {
    return ResizeRendererHandle(handle, renderWidth, renderHeight, outputWidth, outputHeight, error, errorCapacity);
}

extern "C" bool roptixUploadMesh(
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
    int errorCapacity) {
    return UploadMeshRendererHandle(
        handle,
        meshId,
        vertices,
        vertexFloatCount,
        normals,
        normalFloatCount,
        texCoords,
        texCoordFloatCount,
        indices,
        indexCount,
        error,
        errorCapacity);
}

extern "C" bool roptixUpdateMeshVertices(
    void *handle,
    int meshId,
    const float *vertices,
    int vertexFloatCount,
    char *error,
    int errorCapacity) {
    return UpdateMeshVerticesRendererHandle(
        handle,
        meshId,
        vertices,
        vertexFloatCount,
        error,
        errorCapacity);
}

extern "C" bool roptixRenderInstances(
    void *handle,
    int renderWidth,
    int renderHeight,
    int outputWidth,
    int outputHeight,
    NativeCamera camera,
    NativeRenderSettings settings,
    const int *meshIds,
    int instanceCount,
    const int *materialIndices,
    const float *transforms,
    const float *materialParameters,
    int materialFloatCount,
    const int32_t *materialAlbedoTextureIndices,
    int materialTextureIndexCount,
    const uint8_t *texturePixels,
    int texturePixelByteCount,
    const int32_t *textureMetadata,
    int textureMetadataCount,
    unsigned int frameIndex,
    unsigned int outputTextureId,
    NativeFrameStats *stats,
    char *error,
    int errorCapacity) {
    return RenderInstancesRendererHandle(
        handle,
        renderWidth,
        renderHeight,
        outputWidth,
        outputHeight,
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
        stats,
        error,
        errorCapacity);
}
