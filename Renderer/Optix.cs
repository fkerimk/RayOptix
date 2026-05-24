using System.Numerics;
using System.Runtime.InteropServices;
using System.Text;
using Raylib_cs;
using static Raylib_cs.Raylib;

internal sealed class OptixRenderer(CameraData cameraData) : Renderer {

    private const int MaxErrorLength = 2048;

    private static class Settings {

        public static class Quality {
            public const int SamplesPerPixel = 16;
            public const int MaxBounces = 4;
            public const int MinBounces = 1;
            public const int RussianRouletteStartBounce = 2;
            public const bool EnableAccumulation = false;
            public const bool ResetAccumulationOnResize = true;
        }

        public static class Lighting {
            public const bool EnableSky = true;
            public const bool EnableSunLight = true;
            public const float AmbientIntensity = 0.08f;
            public const float SunDirectionX = -0.6f;
            public const float SunDirectionY = -1.0f;
            public const float SunDirectionZ = -0.35f;
            public const float SunIntensity = 1.75f;
            public const float SunAngularRadius = 0.0f;
            public const float SkyBottomR = 0.95f;
            public const float SkyBottomG = 0.97f;
            public const float SkyBottomB = 1.00f;
            public const float SkyTopR = 0.55f;
            public const float SkyTopG = 0.72f;
            public const float SkyTopB = 0.95f;
        }

        public static class Shadows {
            public const bool EnableHardShadows = true;
        }

        public static class Materials {
            public const float DefaultAlbedoR = 0.88f;
            public const float DefaultAlbedoG = 0.52f;
            public const float DefaultAlbedoB = 0.18f;
        }

        public static class Presentation {
            public const float Exposure = 1.0f;
            public const float Gamma = 2.2f;
        }

        public static class Debug {
            public const bool EnableNormalDebug = false;
            public const bool LogNativeErrors = true;
        }
    }

    private readonly List<DrawCall> drawCalls = [];

    private IntPtr nativeHandle;
    private Texture2D texture;
    private byte[]? pixels;
    private int textureWidth;
    private int textureHeight;
    private uint frameIndex;
    private string? initError;
    private bool initAttempted;
    private string? lastLoggedError;

    public override string Name => initError is null ? "OptiX" : "OptiX (Unavailable)";

    public override void Init() {

        textureWidth = GetScreenWidth();
        textureHeight = GetScreenHeight();

        var image = GenImageColor(textureWidth, textureHeight, Color.Black);
        texture = LoadTextureFromImage(image);
        UnloadImage(image);

        pixels = new byte[textureWidth * textureHeight * 4];
    }

    public override void Begin() {

        drawCalls.Clear();
    }

    public override void DrawMesh(MeshData meshData, MaterialData materialData, Matrix4x4 matrix) {

        drawCalls.Add(new DrawCall(meshData, matrix));
    }

    public override void End() {

        EnsureTextureSize();

        if (initError is not null || nativeHandle == IntPtr.Zero || pixels is null) {
            EnsureNativeInitialized();
        }

        if (initError is not null || nativeHandle == IntPtr.Zero || pixels is null) {

            DrawUnavailable();
            return;
        }

        var error = new StringBuilder(MaxErrorLength);
        var camera = cameraData.OptixCameraData ?? new OptixCamera(
            cameraData.Position.X,
            cameraData.Position.Y,
            cameraData.Position.Z,
            cameraData.Target.X,
            cameraData.Target.Y,
            cameraData.Target.Z,
            cameraData.Fov);
        var scene = BuildSceneGeometry();

        if (!OptixNative.Render(nativeHandle,
                textureWidth,
                textureHeight,
                camera,
                BuildSettings(),
                scene.Vertices,
                scene.Vertices.Length,
                scene.Normals,
                scene.Normals.Length,
                scene.Indices,
                scene.Indices.Length,
                frameIndex++,
                pixels,
                pixels.Length,
                error,
                error.Capacity)) {

            initError = error.ToString();
            LogErrorOnce(initError);
            DrawUnavailable();
            return;
        }

        unsafe {

            fixed (byte* pixelPtr = pixels) {

                UpdateTexture(texture, pixelPtr);
            }
        }

        DrawTexturePro(
            texture,
            new Rectangle(0, 0, texture.Width, texture.Height),
            new Rectangle(0, 0, GetScreenWidth(), GetScreenHeight()),
            Vector2.Zero,
            0,
            Color.White);
    }

    public override void Shutdown() {

        if (nativeHandle != IntPtr.Zero) {

            OptixNative.Destroy(nativeHandle);
            nativeHandle = IntPtr.Zero;
        }

        if (texture.Id != 0) {

            UnloadTexture(texture);
        }
    }

    private void EnsureTextureSize() {

        var width = GetScreenWidth();
        var height = GetScreenHeight();

        if (width == textureWidth && height == textureHeight) {

            return;
        }

        if (texture.Id != 0) {

            UnloadTexture(texture);
        }

        textureWidth = width;
        textureHeight = height;
        pixels = new byte[textureWidth * textureHeight * 4];

        var image = GenImageColor(textureWidth, textureHeight, Color.Black);
        texture = LoadTextureFromImage(image);
        UnloadImage(image);

        if (Settings.Quality.ResetAccumulationOnResize) {

            frameIndex = 0;
        }

        if (nativeHandle != IntPtr.Zero) {

            var error = new StringBuilder(MaxErrorLength);
            if (!OptixNative.Resize(nativeHandle, textureWidth, textureHeight, error, error.Capacity)) {

                initError = error.ToString();
                LogErrorOnce(initError);
            }
        }
    }

    private void EnsureNativeInitialized() {

        if (initAttempted) {

            return;
        }

        initAttempted = true;

        var error = new StringBuilder(MaxErrorLength);
        if (!OptixNative.Create(textureWidth, textureHeight, ref nativeHandle, error, error.Capacity)) {

            initError = error.ToString();
            LogErrorOnce(initError);
        }
    }

    private void DrawUnavailable() {

        DrawText("OptiX backend unavailable", 10, 56, 24, Color.Red);
    }

    private void LogErrorOnce(string? error) {

        if (!Settings.Debug.LogNativeErrors || string.IsNullOrWhiteSpace(error) || error == lastLoggedError) {

            return;
        }

        lastLoggedError = error;
        Console.WriteLine($"[OptiX] {error}");
    }

    private OptixGeometry BuildSceneGeometry() {

        var vertices = new List<float>();
        var normals = new List<float>();
        var indices = new List<ushort>();

        foreach (var drawCall in drawCalls) {

            var geometry = drawCall.MeshData.CreateOptixGeometry(drawCall.Matrix);
            var vertexOffset = vertices.Count / 3;

            vertices.AddRange(geometry.Vertices);
            normals.AddRange(geometry.Normals);

            for (var index = 0; index < geometry.Indices.Length; index++) {

                indices.Add((ushort)(geometry.Indices[index] + vertexOffset));
            }
        }

        return new OptixGeometry(vertices.ToArray(), normals.ToArray(), indices.ToArray());
    }

    private static OptixRenderSettings BuildSettings() {

        return new OptixRenderSettings(
            Settings.Quality.SamplesPerPixel,
            Settings.Quality.MaxBounces,
            Settings.Quality.MinBounces,
            Settings.Quality.RussianRouletteStartBounce,
            BoolToInt(Settings.Quality.EnableAccumulation),
            BoolToInt(Settings.Lighting.EnableSky),
            BoolToInt(Settings.Lighting.EnableSunLight),
            BoolToInt(Settings.Shadows.EnableHardShadows),
            BoolToInt(Settings.Debug.EnableNormalDebug),
            Settings.Presentation.Exposure,
            Settings.Presentation.Gamma,
            Settings.Lighting.SkyBottomR,
            Settings.Lighting.SkyBottomG,
            Settings.Lighting.SkyBottomB,
            Settings.Lighting.SkyTopR,
            Settings.Lighting.SkyTopG,
            Settings.Lighting.SkyTopB,
            Settings.Lighting.SunDirectionX,
            Settings.Lighting.SunDirectionY,
            Settings.Lighting.SunDirectionZ,
            Settings.Lighting.SunIntensity,
            Settings.Lighting.SunAngularRadius,
            Settings.Lighting.AmbientIntensity,
            Settings.Materials.DefaultAlbedoR,
            Settings.Materials.DefaultAlbedoG,
            Settings.Materials.DefaultAlbedoB);
    }

    private static int BoolToInt(bool value) {

        return value ? 1 : 0;
    }

    private readonly record struct DrawCall(MeshData MeshData, Matrix4x4 Matrix);

    private static class OptixNative {

        private const string LibraryName = "RayOptixNative";

        [DllImport(LibraryName, EntryPoint = "roptixCreate", CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        [return: MarshalAs(UnmanagedType.I1)]
        public static extern bool Create(int width, int height, ref IntPtr handle, StringBuilder error, int errorCapacity);

        [DllImport(LibraryName, EntryPoint = "roptixDestroy", CallingConvention = CallingConvention.Cdecl)]
        public static extern void Destroy(IntPtr handle);

        [DllImport(LibraryName, EntryPoint = "roptixResize", CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        [return: MarshalAs(UnmanagedType.I1)]
        public static extern bool Resize(IntPtr handle, int width, int height, StringBuilder error, int errorCapacity);

        [DllImport(LibraryName, EntryPoint = "roptixRender", CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        [return: MarshalAs(UnmanagedType.I1)]
        public static extern bool Render(
            IntPtr handle,
            int width,
            int height,
            OptixCamera camera,
            OptixRenderSettings settings,
            float[] vertices,
            int vertexFloatCount,
            float[] normals,
            int normalFloatCount,
            ushort[] indices,
            int indexCount,
            uint frameIndex,
            byte[] outputPixels,
            int outputLength,
            StringBuilder error,
            int errorCapacity);
    }
}
