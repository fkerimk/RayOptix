using System.Runtime.InteropServices;

internal sealed class OptixGeometry(float[] vertices, float[] normals, float[] texCoords, uint[] indices) {

    public readonly float[] Vertices = vertices;
    public readonly float[] Normals = normals;
    public readonly float[] TexCoords = texCoords;
    public readonly uint[] Indices = indices;
}



[StructLayout(LayoutKind.Sequential)]
internal struct OptixFrameStats {

    public double TotalMs;
    public double UploadSceneMs;
    public double LaunchMs;
    public double DenoiseMs;
    public double ReadbackMs;
    public double ToneMapMs;
    public int DenoisedThisFrame;
}

[StructLayout(LayoutKind.Sequential)]
internal readonly struct OptixRenderSettings(
    int samplesPerPixel,
    int maxBounces,
    int minBounces,
    int russianRouletteStartBounce,
    int enableAccumulation,
    int enableDenoiser,
    int denoiserIntervalFrames,
    int enableSky,
    int enableSunLight,
    int enableHardShadows,
    int enableNormalDebug,
    float exposure,
    float gamma,
    float skyBottomR,
    float skyBottomG,
    float skyBottomB,
    float skyTopR,
    float skyTopG,
    float skyTopB,
    float sunDirectionX,
    float sunDirectionY,
    float sunDirectionZ,
    float sunIntensity,
    float sunAngularRadius,
    float ambientIntensity) {

    public readonly int SamplesPerPixel = samplesPerPixel;
    public readonly int MaxBounces = maxBounces;
    public readonly int MinBounces = minBounces;
    public readonly int RussianRouletteStartBounce = russianRouletteStartBounce;
    public readonly int EnableAccumulation = enableAccumulation;
    public readonly int EnableDenoiser = enableDenoiser;
    public readonly int DenoiserIntervalFrames = denoiserIntervalFrames;
    public readonly int EnableSky = enableSky;
    public readonly int EnableSunLight = enableSunLight;
    public readonly int EnableHardShadows = enableHardShadows;
    public readonly int EnableNormalDebug = enableNormalDebug;
    public readonly float Exposure = exposure;
    public readonly float Gamma = gamma;
    public readonly float SkyBottomR = skyBottomR;
    public readonly float SkyBottomG = skyBottomG;
    public readonly float SkyBottomB = skyBottomB;
    public readonly float SkyTopR = skyTopR;
    public readonly float SkyTopG = skyTopG;
    public readonly float SkyTopB = skyTopB;
    public readonly float SunDirectionX = sunDirectionX;
    public readonly float SunDirectionY = sunDirectionY;
    public readonly float SunDirectionZ = sunDirectionZ;
    public readonly float SunIntensity = sunIntensity;
    public readonly float SunAngularRadius = sunAngularRadius;
    public readonly float AmbientIntensity = ambientIntensity;
}
