using System.Numerics;
using System.Runtime.InteropServices;
using System.Text;
using Raylib_cs;
using static Raylib_cs.Raylib;

internal sealed class OptixRenderer(CameraData cameraData) : Renderer {

    private const int SamplesPerPixel = 8;
    private const int MaxErrorLength = 2048;

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
        var camera = cameraData.OptixCameraData ?? new OptixCamera(cameraData.Position, cameraData.Target, cameraData.Fov);
        var scene = BuildSceneGeometry();

        if (!OptixNative.Render(nativeHandle,
                textureWidth,
                textureHeight,
                camera,
                scene.Vertices,
                scene.Vertices.Length,
                scene.Normals,
                scene.Normals.Length,
                scene.Indices,
                scene.Indices.Length,
                SamplesPerPixel,
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

        frameIndex = 0;

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

        if (string.IsNullOrWhiteSpace(error) || error == lastLoggedError) {

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
            float[] vertices,
            int vertexFloatCount,
            float[] normals,
            int normalFloatCount,
            ushort[] indices,
            int indexCount,
            int samplesPerPixel,
            uint frameIndex,
            byte[] outputPixels,
            int outputLength,
            StringBuilder error,
            int errorCapacity);
    }
}
