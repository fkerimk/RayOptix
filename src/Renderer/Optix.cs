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
            public static bool EnableNormalDebug;
            public static bool LogNativeErrors = true;
        }
    }

    private readonly List<DrawCall> _drawCalls = [];

    private IntPtr _nativeHandle;
    private Texture2D _texture;
    private int _renderWidth;
    private int _renderHeight;
    private int _outputWidth;
    private int _outputHeight;
    private uint _frameIndex;
    private OptixCamera? _previousCamera;
    private int _previousSceneSignature;
    private string? _initError;
    private bool _initAttempted;
    private string? _lastLoggedError;
    private OptixFrameStats _lastFrameStats;
    private double _lastInteropMs;
    private double _lastTextureUploadMs;
    private int _lastSceneMaterialCount;
    private int _lastSceneTexturedMaterialCount;
    private int _lastSceneTextureCount;
    private Vector4 _lastSceneUvRange;

    private readonly Dictionary<MeshData, int> _uploadedMeshIds = new(ReferenceEqualityComparer.Instance);
    private int _nextMeshId = 1;

    public override string Name => _initError is null ? "OptiX" : "OptiX (Unavailable)";

    public override void Init() {

        (_renderWidth, _renderHeight) = GetRenderDimensions();
        (_outputWidth, _outputHeight) = GetOutputDimensions();

        var image = GenImageColor(_outputWidth, _outputHeight, Color.Black);
        _texture = LoadTextureFromImage(image);
        UnloadImage(image);
    }

    public override void Begin() {

        _drawCalls.Clear();
    }

    public override void DrawMesh(MeshData meshData, MaterialData materialData, Matrix4x4 matrix) {

        _drawCalls.Add(new DrawCall(meshData, materialData, matrix));
    }

    public override void DrawModel(ModelData modelData, Vector3 position, Vector3 rotation, Vector3 scale) {
        var transform = Util.TransformMatrix(
            modelData.Position + position,
            modelData.RotationDegrees + rotation,
            new Vector3(modelData.Scale.X * scale.X, modelData.Scale.Y * scale.Y, modelData.Scale.Z * scale.Z));

        foreach (var mesh in modelData.Meshes) {
            if (mesh.MaterialIndex < 0 || mesh.MaterialIndex >= modelData.Materials.Count) {
                continue;
            }

            DrawMesh(mesh, modelData.Materials[mesh.MaterialIndex], transform);
        }
    }

    public override void End() {

        HandleDebugInput();

        EnsureTextureSize();

        if (_initError is not null || _nativeHandle == IntPtr.Zero) {
            EnsureNativeInitialized();
        }

        if (_initError is not null || _nativeHandle == IntPtr.Zero) {

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
        var settings = BuildSettings(_frameIndex);
        var sceneSignature = ComputeSceneSignature();
        ResetAccumulationIfNeeded(camera, sceneSignature, settings);

        var interopStopwatch = Stopwatch.StartNew();

        var instanceMeshIds = new int[_drawCalls.Count];
        var instanceMaterialIndices = new int[_drawCalls.Count];
        var instanceTransforms = new float[_drawCalls.Count * 16];
        var materialParameters = new List<float>();
        var materialAlbedoTextureIndices = new List<int>();
        var materialLookup = new Dictionary<MaterialData, uint>(ReferenceEqualityComparer.Instance);
        var textureLookup = new Dictionary<TextureData, int>(ReferenceEqualityComparer.Instance);
        var texturePixels = new List<byte>();
        var textureMetadata = new List<int>();
        var animatedMeshes = new List<MeshData>();

        uint GetOrAddMaterial(MaterialData materialData) {
            if (materialLookup.TryGetValue(materialData, out var existingIndex)) {
                return existingIndex;
            }
            var materialIndex = (uint)materialAlbedoTextureIndices.Count;
            materialLookup[materialData] = materialIndex;
            materialParameters.Add(materialData.Color.X);
            materialParameters.Add(materialData.Color.Y);
            materialParameters.Add(materialData.Color.Z);
            materialParameters.Add(materialData.Color.W);
            materialParameters.Add(materialData.Reflectivity);
            materialAlbedoTextureIndices.Add(GetOrAddTexture(materialData.GetTexture(MaterialMapIndex.Albedo)));
            return materialIndex;
        }

        int GetOrAddTexture(TextureData? textureData) {
            textureData?.EnsureOptixPixels();
            if (textureData?.OptixPixels == null ||
                textureData.OptixPixels.Length == 0 ||
                textureData.OptixWidth <= 0 ||
                textureData.OptixHeight <= 0) {
                return -1;
            }
            if (textureLookup.TryGetValue(textureData, out var existingIndex)) {
                return existingIndex;
            }
            var textureIndex = textureMetadata.Count / 3;
            textureLookup[textureData] = textureIndex;
            textureMetadata.Add(texturePixels.Count / 4);
            textureMetadata.Add(textureData.OptixWidth);
            textureMetadata.Add(textureData.OptixHeight);
            texturePixels.AddRange(textureData.OptixPixels);
            return textureIndex;
        }

        var instanceIndex = 0;
        foreach (var drawCall in _drawCalls) {
            var meshData = drawCall.MeshData;

            if (!_uploadedMeshIds.TryGetValue(meshData, out var meshId)) {
                meshId = _nextMeshId++;
                var geometry = meshData.CreateOptixGeometry();
                if (!OptixNative.UploadMesh(_nativeHandle,
                        meshId,
                        geometry.Vertices, geometry.Vertices.Length,
                        geometry.Normals, geometry.Normals.Length,
                        geometry.TexCoords, geometry.TexCoords.Length,
                        geometry.Indices, geometry.Indices.Length,
                        error, error.Capacity)) {
                    _initError = error.ToString();
                    LogErrorOnce(_initError);
                    DrawUnavailable();
                    return;
                }
                _uploadedMeshIds[meshData] = meshId;
            }

            if (meshData.UsesSkinning || meshData.AnimatedVertices != null) {
                animatedMeshes.Add(meshData);
            }

            var materialIndex = (int)GetOrAddMaterial(drawCall.MaterialData);
            instanceMeshIds[instanceIndex] = meshId;
            instanceMaterialIndices[instanceIndex] = materialIndex;

            var m = drawCall.Matrix;
            instanceTransforms[instanceIndex * 16 + 0] = m.M11;
            instanceTransforms[instanceIndex * 16 + 1] = m.M12;
            instanceTransforms[instanceIndex * 16 + 2] = m.M13;
            instanceTransforms[instanceIndex * 16 + 3] = m.M14;
            instanceTransforms[instanceIndex * 16 + 4] = m.M21;
            instanceTransforms[instanceIndex * 16 + 5] = m.M22;
            instanceTransforms[instanceIndex * 16 + 6] = m.M23;
            instanceTransforms[instanceIndex * 16 + 7] = m.M24;
            instanceTransforms[instanceIndex * 16 + 8] = m.M31;
            instanceTransforms[instanceIndex * 16 + 9] = m.M32;
            instanceTransforms[instanceIndex * 16 + 10] = m.M33;
            instanceTransforms[instanceIndex * 16 + 11] = m.M34;
            instanceIndex++;
        }

        foreach (var mesh in animatedMeshes.Distinct()) {
            if (_uploadedMeshIds.TryGetValue(mesh, out var meshId)) {
                var geometry = mesh.CreateOptixGeometry();
                if (!OptixNative.UpdateMeshVertices(_nativeHandle,
                        meshId,
                        geometry.Vertices, geometry.Vertices.Length,
                        error, error.Capacity)) {
                    _initError = error.ToString();
                    LogErrorOnce(_initError);
                    DrawUnavailable();
                    return;
                }
            }
        }

        _lastSceneMaterialCount = materialAlbedoTextureIndices.Count;
        _lastSceneTexturedMaterialCount = materialAlbedoTextureIndices.Count(index => index >= 0);
        _lastSceneTextureCount = textureMetadata.Count / 3;

        if (!OptixNative.RenderInstances(_nativeHandle,
                _renderWidth,
                _renderHeight,
                _outputWidth,
                _outputHeight,
                camera,
                settings,
                instanceMeshIds,
                instanceMeshIds.Length,
                instanceMaterialIndices,
                instanceTransforms,
                materialParameters.ToArray(),
                materialParameters.Count,
                materialAlbedoTextureIndices.ToArray(),
                materialAlbedoTextureIndices.Count,
                texturePixels.ToArray(),
                texturePixels.Count,
                textureMetadata.ToArray(),
                textureMetadata.Count,
                _frameIndex++,
                _texture.Id,
                ref _lastFrameStats,
                error,
                error.Capacity)) {

            interopStopwatch.Stop();
            _lastInteropMs = interopStopwatch.Elapsed.TotalMilliseconds;

            _initError = error.ToString();
            LogErrorOnce(_initError);
            DrawUnavailable();
            return;
        }
        interopStopwatch.Stop();
        _lastInteropMs = interopStopwatch.Elapsed.TotalMilliseconds;
        _lastTextureUploadMs = 0.0;

        DrawTexturePro(
            _texture,
            new Rectangle(0, 0, _texture.Width, _texture.Height),
            new Rectangle(0, 0, GetScreenWidth(), GetScreenHeight()),
            Vector2.Zero,
            0,
            Color.White);

        DrawDebugState();
    }

    public override void Shutdown() {

        if (_nativeHandle != IntPtr.Zero) {

            if (_texture.Id != 0) {

                OptixNative.ReleaseOutputTexture(_nativeHandle, _texture.Id);
            }

            OptixNative.Destroy(_nativeHandle);
            _nativeHandle = IntPtr.Zero;
        }

        if (_texture.Id != 0) {
            UnloadTexture(_texture);
        }
    }

    private void EnsureTextureSize() {

        var (newRenderWidth, newRenderHeight) = GetRenderDimensions();
        var (newOutputWidth, newOutputHeight) = GetOutputDimensions();

        if (newRenderWidth == _renderWidth &&
            newRenderHeight == _renderHeight &&
            newOutputWidth == _outputWidth &&
            newOutputHeight == _outputHeight) {

            return;
        }

        if (_texture.Id != 0) {

            if (_nativeHandle != IntPtr.Zero) {

                OptixNative.ReleaseOutputTexture(_nativeHandle, _texture.Id);
            }

            UnloadTexture(_texture);
        }

        _renderWidth = newRenderWidth;
        _renderHeight = newRenderHeight;
        _outputWidth = newOutputWidth;
        _outputHeight = newOutputHeight;

        var image = GenImageColor(_outputWidth, _outputHeight, Color.Black);
        _texture = LoadTextureFromImage(image);
        UnloadImage(image);

        if (Settings.Quality.ResetAccumulationOnResize) {

            _frameIndex = 0;
            _previousSceneSignature = 0;
        }

        if (_nativeHandle != IntPtr.Zero) {

            var error = new StringBuilder(MaxErrorLength);
            if (!OptixNative.Resize(_nativeHandle, _renderWidth, _renderHeight, _outputWidth, _outputHeight, error, error.Capacity)) {

                _initError = error.ToString();
                LogErrorOnce(_initError);
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

        if (_initAttempted) {

            return;
        }

        _initAttempted = true;

        var error = new StringBuilder(MaxErrorLength);
        try {
            if (!OptixNative.Create(_renderWidth, _renderHeight, _outputWidth, _outputHeight, ref _nativeHandle, error, error.Capacity)) {

                _initError = error.ToString();
                LogErrorOnce(_initError);
            }
        } catch (DllNotFoundException exception) {

            _initError = $"Native OptiX library could not be loaded: {exception.Message}";
            LogErrorOnce(_initError);
        } catch (BadImageFormatException exception) {

            _initError = $"Native OptiX library is not compatible with this runtime: {exception.Message}";
            LogErrorOnce(_initError);
        } catch (EntryPointNotFoundException exception) {

            _initError = $"Native OptiX entry point is missing: {exception.Message}";
            LogErrorOnce(_initError);
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

            _frameIndex = 0;
        }
    }

    private void DrawDebugState() {

        DrawText($"F1 Normal Debug: {(Settings.Debug.EnableNormalDebug ? "ON" : "OFF")}", 10, 56, 20, Color.DarkBlue);
        DrawText($"F2 Sun Light: {(Settings.Lighting.EnableSunLight ? "ON" : "OFF")}", 10, 80, 20, Color.DarkBlue);
        DrawText($"F3 Hard Shadows: {(Settings.Shadows.EnableHardShadows ? "ON" : "OFF")}", 10, 104, 20, Color.DarkBlue);
        DrawText($"F4 Denoiser: {(Settings.Quality.EnableDenoiser ? "ON" : "OFF")}", 10, 128, 20, Color.DarkBlue);
        DrawText($"F5/F6 Render Scale: {RenderSettings.RenderScale:0.00}x", 10, 152, 20, Color.DarkBlue);
        DrawText($"F7/F8 Denoiser Interval: {Settings.Quality.DenoiserIntervalFrames}", 10, 176, 20, Color.DarkBlue);
        DrawText($"Interop: {_lastInteropMs:0.0} ms", 10, 200, 20, Color.Maroon);
        DrawText($"Native Total: {_lastFrameStats.TotalMs:0.0} ms", 10, 224, 20, Color.Maroon);
        DrawText($"Upload/Launch: {_lastFrameStats.UploadSceneMs:0.0} / {_lastFrameStats.LaunchMs:0.0} ms", 10, 248, 20, Color.Maroon);
        DrawText($"Denoise/Present: {_lastFrameStats.DenoiseMs:0.0} / {_lastFrameStats.ReadbackMs:0.0} ms", 10, 272, 20, Color.Maroon);
        DrawText($"ToneMap/CSharp: {_lastFrameStats.ToneMapMs:0.0} / {_lastTextureUploadMs:0.0} ms", 10, 296, 20, Color.Maroon);
        DrawText($"Denoised Frame: {(_lastFrameStats.DenoisedThisFrame != 0 ? "YES" : "NO")}", 10, 320, 20, Color.Maroon);
        DrawText($"Scene Mats/TexMats/Tex: {_lastSceneMaterialCount}/{_lastSceneTexturedMaterialCount}/{_lastSceneTextureCount}", 10, 344, 20, Color.Maroon);
        DrawText($"UV MinMax: {_lastSceneUvRange.X:0.00},{_lastSceneUvRange.Y:0.00} / {_lastSceneUvRange.Z:0.00},{_lastSceneUvRange.W:0.00}", 10, 368, 20, Color.Maroon);
    }

    private void LogErrorOnce(string? error) {

        if (!Settings.Debug.LogNativeErrors || string.IsNullOrWhiteSpace(error) || error == _lastLoggedError) {

            return;
        }

        _lastLoggedError = error;
        Console.WriteLine($"[OptiX] {error}");
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

            _frameIndex = 0;
            _previousCamera = camera;
            _previousSceneSignature = sceneSignature;
            return;
        }

        if (_previousCamera is not OptixCamera lastCamera ||
            !AreEqual(lastCamera, camera) ||
            _previousSceneSignature != sceneSignature) {

            _frameIndex = 0;
        }

        _previousCamera = camera;
        _previousSceneSignature = sceneSignature;
    }

    private int ComputeSceneSignature() {

        var hash = new HashCode();

        foreach (var drawCall in _drawCalls) {

            hash.Add(RuntimeHelpers.GetHashCode(drawCall.MeshData));
            hash.Add(drawCall.MaterialData.Color.X);
            hash.Add(drawCall.MaterialData.Color.Y);
            hash.Add(drawCall.MaterialData.Color.Z);
            hash.Add(drawCall.MaterialData.Color.W);
            hash.Add(drawCall.MaterialData.Reflectivity);
            AddMatrixToHash(ref hash, drawCall.Matrix);
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

    private readonly record struct DrawCall(MeshData MeshData, MaterialData MaterialData, Matrix4x4 Matrix);

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

        [DllImport(LibraryName, EntryPoint = "roptixUploadMesh", CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        [return: MarshalAs(UnmanagedType.I1)]
        public static extern bool UploadMesh(
            IntPtr handle,
            int meshId,
            float[] vertices,
            int vertexFloatCount,
            float[] normals,
            int normalFloatCount,
            float[] texCoords,
            int texCoordFloatCount,
            uint[] indices,
            int indexCount,
            StringBuilder error,
            int errorCapacity);

        [DllImport(LibraryName, EntryPoint = "roptixUpdateMeshVertices", CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        [return: MarshalAs(UnmanagedType.I1)]
        public static extern bool UpdateMeshVertices(
            IntPtr handle,
            int meshId,
            float[] vertices,
            int vertexFloatCount,
            StringBuilder error,
            int errorCapacity);

        [DllImport(LibraryName, EntryPoint = "roptixRenderInstances", CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        [return: MarshalAs(UnmanagedType.I1)]
        public static extern bool RenderInstances(
            IntPtr handle,
            int renderWidth,
            int renderHeight,
            int outputWidth,
            int outputHeight,
            OptixCamera camera,
            OptixRenderSettings settings,
            int[] meshIds,
            int instanceCount,
            int[] materialIndices,
            float[] transforms,
            float[] materialParameters,
            int materialFloatCount,
            int[] materialAlbedoTextureIndices,
            int materialTextureIndexCount,
            byte[] texturePixels,
            int texturePixelByteCount,
            int[] textureMetadata,
            int textureMetadataCount,
            uint frameIndex,
            uint outputTextureId,
            ref OptixFrameStats stats,
            StringBuilder error,
            int errorCapacity);

    }
}
