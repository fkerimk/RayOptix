using System.Numerics;
using System.Runtime.InteropServices;
using System.Text;
using Raylib_cs;
using static Raylib_cs.Raylib;

internal sealed class OptixRenderer(CameraData cameraData) : Renderer {

    private const int SamplesPerPixel = 8;
    private const int MaxErrorLength = 2048;

    private readonly List<float> vertices = [];
    private readonly List<float> normals = [];
    private readonly List<ushort> indices = [];

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

        vertices.Clear();
        normals.Clear();
        indices.Clear();
    }

    public override void DrawMesh(MeshData meshData, MaterialData materialData, Matrix4x4 matrix) {

        var vertexOffset = vertices.Count / 3;

        for (var index = 0; index < meshData.Vertices.Length; index += 3) {

            var position = Vector3.Transform(new Vector3(
                meshData.Vertices[index],
                meshData.Vertices[index + 1],
                meshData.Vertices[index + 2]), matrix);

            var normal = Vector3.Normalize(Vector3.TransformNormal(new Vector3(
                meshData.Normals[index],
                meshData.Normals[index + 1],
                meshData.Normals[index + 2]), matrix));

            vertices.Add(position.X);
            vertices.Add(position.Y);
            vertices.Add(position.Z);

            normals.Add(normal.X);
            normals.Add(normal.Y);
            normals.Add(normal.Z);
        }

        for (var index = 0; index < meshData.Indices.Length; index++) {

            indices.Add((ushort)(meshData.Indices[index] + vertexOffset));
        }
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
        var camera = new NativeCamera(cameraData.Position, cameraData.Target, cameraData.Fov);

        if (!OptixNative.Render(nativeHandle,
                textureWidth,
                textureHeight,
                camera,
                vertices.ToArray(),
                vertices.Count,
                normals.ToArray(),
                normals.Count,
                indices.ToArray(),
                indices.Count,
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

    [StructLayout(LayoutKind.Sequential)]
    private readonly struct NativeCamera(Vector3 position, Vector3 target, float fovY) {

        public readonly Vector3 Position = position;
        public readonly Vector3 Target = target;
        public readonly float FovY = fovY;
    }

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
            NativeCamera camera,
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
