internal sealed class OptixMesh(float[] vertices, float[] normals, ushort[] indices) {

    public readonly float[] Vertices = vertices;
    public readonly float[] Normals = normals;
    public readonly ushort[] Indices = indices;
}

internal sealed class OptixGeometry(float[] vertices, float[] normals, ushort[] indices) {

    public readonly float[] Vertices = vertices;
    public readonly float[] Normals = normals;
    public readonly ushort[] Indices = indices;
}

internal readonly struct OptixCamera(float positionX, float positionY, float positionZ, float targetX, float targetY, float targetZ, float fovY) {

    public readonly float PositionX = positionX;
    public readonly float PositionY = positionY;
    public readonly float PositionZ = positionZ;
    public readonly float TargetX = targetX;
    public readonly float TargetY = targetY;
    public readonly float TargetZ = targetZ;
    public readonly float FovY = fovY;
}

internal readonly struct OptixRenderSettings(
    int samplesPerPixel,
    int maxBounces,
    int minBounces,
    int russianRouletteStartBounce,
    int enableAccumulation,
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
    float ambientIntensity,
    float albedoR,
    float albedoG,
    float albedoB) {

    public readonly int SamplesPerPixel = samplesPerPixel;
    public readonly int MaxBounces = maxBounces;
    public readonly int MinBounces = minBounces;
    public readonly int RussianRouletteStartBounce = russianRouletteStartBounce;
    public readonly int EnableAccumulation = enableAccumulation;
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
    public readonly float AlbedoR = albedoR;
    public readonly float AlbedoG = albedoG;
    public readonly float AlbedoB = albedoB;
}
