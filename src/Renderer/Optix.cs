using System.Numerics;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;
using System.Diagnostics;
using Raylib_cs;
using static Raylib_cs.Raylib;

internal sealed class OptixRenderer(CameraData cameraData) : Renderer {

    private const int MaxErrorLength = 2048;

    private static class Settings {

        public static class Quality {
            public const int SamplesPerPixel = 4;
            public const int MaxBounces = 4;
            public const int MinBounces = 1;
            public const int RussianRouletteStartBounce = 2;
            public const bool EnableAccumulation = true;
            public static bool EnableDenoiser = true;
            public static int DenoiserIntervalFrames = 1;
            public const int MinDenoiserIntervalFrames = 1;
            public const int MaxDenoiserIntervalFrames = 32;
            public const bool ResetAccumulationOnResize = true;
        }

        public static class Lighting {
            public static bool EnableSky = true;
            public static bool EnableSunLight = true;
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
            public static bool EnableHardShadows = true;
        }

        public static class Presentation {
            public const float Exposure = 1.0f;
            public const float Gamma = 2.2f;
        }

        public static class Debug {
            public static bool EnableNormalDebug = false;
            public static bool LogNativeErrors = true;
        }
    }

    private readonly List<DrawCall> drawCalls = [];

    private IntPtr nativeHandle;
    private Texture2D texture;
    private int renderWidth;
    private int renderHeight;
    private int outputWidth;
    private int outputHeight;
    private uint frameIndex;
    private OptixCamera? previousCamera;
    private int previousSceneSignature;
    private string? initError;
    private bool initAttempted;
    private string? lastLoggedError;
    private OptixFrameStats lastFrameStats;
    private double lastInteropMs;
    private double lastTextureUploadMs;

    public override string name => initError is null ? "OptiX" : "OptiX (Unavailable)";

    public override void Init() {

        (renderWidth, renderHeight) = GetRenderDimensions();
        (outputWidth, outputHeight) = GetOutputDimensions();

        var image = GenImageColor(outputWidth, outputHeight, Color.Black);
        texture = LoadTextureFromImage(image);
        UnloadImage(image);
    }

    public override void Begin() {

        drawCalls.Clear();
    }

    public override void DrawMesh(MeshData meshData, MaterialData materialData, Matrix4x4 matrix) {

        drawCalls.Add(new DrawCall(meshData, materialData, matrix));
    }

    public override void DrawModel(ModelData modelData, Vector3 position, Vector3 rotation, Vector3 scale) {
        
        throw new NotImplementedException();
    }

    public override void End() {

        HandleDebugInput();

        EnsureTextureSize();

        if (initError is not null || nativeHandle == IntPtr.Zero) {
            EnsureNativeInitialized();
        }

        if (initError is not null || nativeHandle == IntPtr.Zero) {

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
        var settings = BuildSettings(frameIndex);
        var sceneSignature = ComputeSceneSignature();
        ResetAccumulationIfNeeded(camera, sceneSignature, settings);
        var scene = BuildSceneGeometry();

        var interopStopwatch = Stopwatch.StartNew();
        if (!OptixNative.Render(nativeHandle,
                renderWidth,
                renderHeight,
                outputWidth,
                outputHeight,
                camera,
                settings,
                scene.Vertices,
                scene.Vertices.Length,
                scene.Normals,
                scene.Normals.Length,
                scene.Indices,
                scene.Indices.Length,
                scene.TriangleMaterialIndices,
                scene.TriangleMaterialIndices.Length,
                scene.Materials,
                scene.Materials.Length,
                frameIndex++,
                texture.Id,
                ref lastFrameStats,
                error,
                error.Capacity)) {

            interopStopwatch.Stop();
            lastInteropMs = interopStopwatch.Elapsed.TotalMilliseconds;

            initError = error.ToString();
            LogErrorOnce(initError);
            DrawUnavailable();
            return;
        }
        interopStopwatch.Stop();
        lastInteropMs = interopStopwatch.Elapsed.TotalMilliseconds;
        lastTextureUploadMs = 0.0;

        DrawTexturePro(
            texture,
            new Rectangle(0, 0, texture.Width, texture.Height),
            new Rectangle(0, 0, GetScreenWidth(), GetScreenHeight()),
            Vector2.Zero,
            0,
            Color.White);

        DrawDebugState();
    }

    public override void Shutdown() {

        if (nativeHandle != IntPtr.Zero) {

            if (texture.Id != 0) {

                OptixNative.ReleaseOutputTexture(nativeHandle, texture.Id);
            }

            OptixNative.Destroy(nativeHandle);
            nativeHandle = IntPtr.Zero;
        }

        if (texture.Id != 0) {
            UnloadTexture(texture);
        }
    }

    private void EnsureTextureSize() {

        var (newRenderWidth, newRenderHeight) = GetRenderDimensions();
        var (newOutputWidth, newOutputHeight) = GetOutputDimensions();

        if (newRenderWidth == renderWidth &&
            newRenderHeight == renderHeight &&
            newOutputWidth == outputWidth &&
            newOutputHeight == outputHeight) {

            return;
        }

        if (texture.Id != 0) {

            if (nativeHandle != IntPtr.Zero) {

                OptixNative.ReleaseOutputTexture(nativeHandle, texture.Id);
            }

            UnloadTexture(texture);
        }

        renderWidth = newRenderWidth;
        renderHeight = newRenderHeight;
        outputWidth = newOutputWidth;
        outputHeight = newOutputHeight;

        var image = GenImageColor(outputWidth, outputHeight, Color.Black);
        texture = LoadTextureFromImage(image);
        UnloadImage(image);

        if (Settings.Quality.ResetAccumulationOnResize) {

            frameIndex = 0;
            previousSceneSignature = 0;
        }

        if (nativeHandle != IntPtr.Zero) {

            var error = new StringBuilder(MaxErrorLength);
            if (!OptixNative.Resize(nativeHandle, renderWidth, renderHeight, outputWidth, outputHeight, error, error.Capacity)) {

                initError = error.ToString();
                LogErrorOnce(initError);
            }
        }
    }

    private static (int Width, int Height) GetRenderDimensions() {

        var scale = Math.Clamp(
            RenderSettings.RenderScale,
            RenderSettings.MinRenderScale,
            RenderSettings.MaxRenderScale);

        var width = Math.Max(1, (int)MathF.Round(GetScreenWidth() * scale));
        var height = Math.Max(1, (int)MathF.Round(GetScreenHeight() * scale));
        return (width, height);
    }

    private static (int Width, int Height) GetOutputDimensions() {

        return (Math.Max(1, GetScreenWidth()), Math.Max(1, GetScreenHeight()));
    }

    private void EnsureNativeInitialized() {

        if (initAttempted) {

            return;
        }

        initAttempted = true;

        var error = new StringBuilder(MaxErrorLength);
        try {
            if (!OptixNative.Create(renderWidth, renderHeight, outputWidth, outputHeight, ref nativeHandle, error, error.Capacity)) {

                initError = error.ToString();
                LogErrorOnce(initError);
            }
        } catch (DllNotFoundException exception) {

            initError = $"Native OptiX library could not be loaded: {exception.Message}";
            LogErrorOnce(initError);
        } catch (BadImageFormatException exception) {

            initError = $"Native OptiX library is not compatible with this runtime: {exception.Message}";
            LogErrorOnce(initError);
        } catch (EntryPointNotFoundException exception) {

            initError = $"Native OptiX entry point is missing: {exception.Message}";
            LogErrorOnce(initError);
        }
    }

    private void DrawUnavailable() {

        DrawText("OptiX backend unavailable", 10, 56, 24, Color.Red);
    }

    private void HandleDebugInput() {

        var stateChanged = false;

        if (IsKeyPressed(KeyboardKey.F1)) {

            Settings.Debug.EnableNormalDebug = !Settings.Debug.EnableNormalDebug;
            stateChanged = true;
        }

        if (IsKeyPressed(KeyboardKey.F2)) {

            Settings.Lighting.EnableSunLight = !Settings.Lighting.EnableSunLight;
            stateChanged = true;
        }

        if (IsKeyPressed(KeyboardKey.F3)) {

            Settings.Shadows.EnableHardShadows = !Settings.Shadows.EnableHardShadows;
            stateChanged = true;
        }

        if (IsKeyPressed(KeyboardKey.F4)) {

            Settings.Quality.EnableDenoiser = !Settings.Quality.EnableDenoiser;
            stateChanged = true;
        }

        if (IsKeyPressed(KeyboardKey.F7)) {
            Settings.Quality.DenoiserIntervalFrames = Math.Max(
                Settings.Quality.MinDenoiserIntervalFrames,
                Settings.Quality.DenoiserIntervalFrames - 1);
            stateChanged = true;
        }

        if (IsKeyPressed(KeyboardKey.F8)) {
            Settings.Quality.DenoiserIntervalFrames = Math.Min(
                Settings.Quality.MaxDenoiserIntervalFrames,
                Settings.Quality.DenoiserIntervalFrames + 1);
            stateChanged = true;
        }

        if (IsKeyPressed(KeyboardKey.F5)) {
            RenderSettings.RenderScale = MathF.Max(
                RenderSettings.MinRenderScale,
                RenderSettings.RenderScale - RenderSettings.RenderScaleStep);
            stateChanged = true;
        }

        if (IsKeyPressed(KeyboardKey.F6)) {
            RenderSettings.RenderScale = MathF.Min(
                RenderSettings.MaxRenderScale,
                RenderSettings.RenderScale + RenderSettings.RenderScaleStep);
            stateChanged = true;
        }

        if (stateChanged) {

            frameIndex = 0;
        }
    }

    private void DrawDebugState() {

        DrawText($"F1 Normal Debug: {(Settings.Debug.EnableNormalDebug ? "ON" : "OFF")}", 10, 56, 20, Color.DarkBlue);
        DrawText($"F2 Sun Light: {(Settings.Lighting.EnableSunLight ? "ON" : "OFF")}", 10, 80, 20, Color.DarkBlue);
        DrawText($"F3 Hard Shadows: {(Settings.Shadows.EnableHardShadows ? "ON" : "OFF")}", 10, 104, 20, Color.DarkBlue);
        DrawText($"F4 Denoiser: {(Settings.Quality.EnableDenoiser ? "ON" : "OFF")}", 10, 128, 20, Color.DarkBlue);
        DrawText($"F5/F6 Render Scale: {RenderSettings.RenderScale:0.00}x", 10, 152, 20, Color.DarkBlue);
        DrawText($"F7/F8 Denoiser Interval: {Settings.Quality.DenoiserIntervalFrames}", 10, 176, 20, Color.DarkBlue);
        DrawText($"Interop: {lastInteropMs:0.0} ms", 10, 200, 20, Color.Maroon);
        DrawText($"Native Total: {lastFrameStats.TotalMs:0.0} ms", 10, 224, 20, Color.Maroon);
        DrawText($"Upload/Launch: {lastFrameStats.UploadSceneMs:0.0} / {lastFrameStats.LaunchMs:0.0} ms", 10, 248, 20, Color.Maroon);
        DrawText($"Denoise/Present: {lastFrameStats.DenoiseMs:0.0} / {lastFrameStats.ReadbackMs:0.0} ms", 10, 272, 20, Color.Maroon);
        DrawText($"ToneMap/CSharp: {lastFrameStats.ToneMapMs:0.0} / {lastTextureUploadMs:0.0} ms", 10, 296, 20, Color.Maroon);
        DrawText($"Denoised Frame: {(lastFrameStats.DenoisedThisFrame != 0 ? "YES" : "NO")}", 10, 320, 20, Color.Maroon);
    }

    private void LogErrorOnce(string? error) {

        if (!Settings.Debug.LogNativeErrors || string.IsNullOrWhiteSpace(error) || error == lastLoggedError) {

            return;
        }

        lastLoggedError = error;
        Console.WriteLine($"[OptiX] {error}");
    }

    private OptixScene BuildSceneGeometry() {

        var vertices = new List<float>();
        var normals = new List<float>();
        var indices = new List<ushort>();
        var triangleMaterialIndices = new List<uint>();
        var materials = new List<OptixMaterial>();

        foreach (var drawCall in drawCalls) {

            var geometry = drawCall.meshData.CreateOptixGeometry(drawCall.matrix);
            var vertexOffset = vertices.Count / 3;
            var material = drawCall.materialData.OptixMaterialData
                           ?? throw new InvalidOperationException("OptiX material data has not been built.");
            var materialIndex = (uint)materials.Count;

            vertices.AddRange(geometry.Vertices);
            normals.AddRange(geometry.Normals);
            materials.Add(material);

            for (var index = 0; index < geometry.Indices.Length; index++) {

                indices.Add((ushort)(geometry.Indices[index] + vertexOffset));
            }

            for (var triangleIndex = 0; triangleIndex < geometry.Indices.Length / 3; triangleIndex++) {

                triangleMaterialIndices.Add(materialIndex);
            }
        }

        return new OptixScene(
            vertices.ToArray(),
            normals.ToArray(),
            indices.ToArray(),
            triangleMaterialIndices.ToArray(),
            materials.ToArray());
    }

    private static OptixRenderSettings BuildSettings(uint frameIndex) {

        return new OptixRenderSettings(
            Settings.Quality.SamplesPerPixel,
            Settings.Quality.MaxBounces,
            Settings.Quality.MinBounces,
            Settings.Quality.RussianRouletteStartBounce,
            BoolToInt(Settings.Quality.EnableAccumulation),
            BoolToInt(Settings.Quality.EnableDenoiser),
            Settings.Quality.DenoiserIntervalFrames,
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
            Settings.Lighting.AmbientIntensity);
    }

    private static int BoolToInt(bool value) {

        return value ? 1 : 0;
    }

    private void ResetAccumulationIfNeeded(OptixCamera camera, int sceneSignature, OptixRenderSettings settings) {

        if (settings.EnableAccumulation == 0) {

            frameIndex = 0;
            previousCamera = camera;
            previousSceneSignature = sceneSignature;
            return;
        }

        if (previousCamera is not OptixCamera lastCamera ||
            !AreEqual(lastCamera, camera) ||
            previousSceneSignature != sceneSignature) {

            frameIndex = 0;
        }

        previousCamera = camera;
        previousSceneSignature = sceneSignature;
    }

    private int ComputeSceneSignature() {

        var hash = new HashCode();

        foreach (var drawCall in drawCalls) {

            hash.Add(RuntimeHelpers.GetHashCode(drawCall.meshData));
            hash.Add(drawCall.materialData.Color.X);
            hash.Add(drawCall.materialData.Color.Y);
            hash.Add(drawCall.materialData.Color.Z);
            hash.Add(drawCall.materialData.Color.W);
            hash.Add(drawCall.materialData.Reflectivity);
            AddMatrixToHash(ref hash, drawCall.matrix);
        }

        return hash.ToHashCode();
    }

    private static bool AreEqual(OptixCamera left, OptixCamera right) {

        return left.PositionX == right.PositionX &&
               left.PositionY == right.PositionY &&
               left.PositionZ == right.PositionZ &&
               left.TargetX == right.TargetX &&
               left.TargetY == right.TargetY &&
               left.TargetZ == right.TargetZ &&
               left.FovY == right.FovY;
    }

    private static void AddMatrixToHash(ref HashCode hash, Matrix4x4 matrix) {

        hash.Add(matrix.M11);
        hash.Add(matrix.M12);
        hash.Add(matrix.M13);
        hash.Add(matrix.M14);
        hash.Add(matrix.M21);
        hash.Add(matrix.M22);
        hash.Add(matrix.M23);
        hash.Add(matrix.M24);
        hash.Add(matrix.M31);
        hash.Add(matrix.M32);
        hash.Add(matrix.M33);
        hash.Add(matrix.M34);
        hash.Add(matrix.M41);
        hash.Add(matrix.M42);
        hash.Add(matrix.M43);
        hash.Add(matrix.M44);
    }

    private readonly record struct DrawCall(MeshData meshData, MaterialData materialData, Matrix4x4 matrix);

    private static class OptixNative {

        private const string LibraryName = "RayOptixNative";

        [DllImport(LibraryName, EntryPoint = "roptixCreate", CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        [return: MarshalAs(UnmanagedType.I1)]
        public static extern bool Create(int renderWidth, int renderHeight, int outputWidth, int outputHeight, ref IntPtr handle, StringBuilder error, int errorCapacity);

        [DllImport(LibraryName, EntryPoint = "roptixDestroy", CallingConvention = CallingConvention.Cdecl)]
        public static extern void Destroy(IntPtr handle);

        [DllImport(LibraryName, EntryPoint = "roptixReleaseOutputTexture", CallingConvention = CallingConvention.Cdecl)]
        public static extern void ReleaseOutputTexture(IntPtr handle, uint textureId);

        [DllImport(LibraryName, EntryPoint = "roptixResize", CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        [return: MarshalAs(UnmanagedType.I1)]
        public static extern bool Resize(IntPtr handle, int renderWidth, int renderHeight, int outputWidth, int outputHeight, StringBuilder error, int errorCapacity);

        [DllImport(LibraryName, EntryPoint = "roptixRender", CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        [return: MarshalAs(UnmanagedType.I1)]
        public static extern bool Render(
            IntPtr handle,
            int renderWidth,
            int renderHeight,
            int outputWidth,
            int outputHeight,
            OptixCamera camera,
            OptixRenderSettings settings,
            float[] vertices,
            int vertexFloatCount,
            float[] normals,
            int normalFloatCount,
            ushort[] indices,
            int indexCount,
            uint[] triangleMaterialIndices,
            int triangleMaterialIndexCount,
            OptixMaterial[] materials,
            int materialCount,
            uint frameIndex,
            uint outputTextureId,
            ref OptixFrameStats stats,
            StringBuilder error,
            int errorCapacity);

    }
}
