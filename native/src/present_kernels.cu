#include <cuda_runtime.h>

static __forceinline__ __device__ unsigned char ToneMapChannel(float value, float exposure, float inverseGamma) {
    const float exposed = fminf(fmaxf(value * exposure, 0.0f), 1.0f);
    const float corrected = powf(exposed, inverseGamma);
    return static_cast<unsigned char>(fminf(fmaxf(corrected * 255.0f, 0.0f), 255.0f));
}

extern "C" __global__ void ToneMapToSurface(
    const float4 *hdrPixels,
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
