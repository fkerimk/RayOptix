#include "native_api.h"

extern "C" bool roptixCreate(int renderWidth, int renderHeight, int outputWidth, int outputHeight, void** handle, char* error, int errorCapacity) {
    return CreateRendererHandle(renderWidth, renderHeight, outputWidth, outputHeight, handle, error, errorCapacity);
}

extern "C" void roptixDestroy(void* handle) {
    DestroyRendererHandle(handle);
}

extern "C" void roptixReleaseOutputTexture(void* handle, unsigned int textureId) {
    ReleaseRendererOutputTexture(handle, textureId);
}

extern "C" bool roptixResize(void* handle, int renderWidth, int renderHeight, int outputWidth, int outputHeight, char* error, int errorCapacity) {
    return ResizeRendererHandle(handle, renderWidth, renderHeight, outputWidth, outputHeight, error, errorCapacity);
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
    return RenderRendererHandle(
        handle,
        renderWidth,
        renderHeight,
        outputWidth,
        outputHeight,
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
        stats,
        error,
        errorCapacity);
}
