using System.Numerics;
using Raylib_cs;
using static Raylib_cs.Raylib;

internal class RaylibRenderer(CameraData cameraData) : Renderer {

    private RenderTexture2D renderTexture;
    private int renderWidth;
    private int renderHeight;

    public override string name => "Raylib";

    public override void Init() {

        (renderWidth, renderHeight) = GetRenderDimensions();
        renderTexture = LoadRenderTexture(renderWidth, renderHeight);
    }

    public override void Begin() {

        EnsureTextureSize();

        BeginTextureMode(renderTexture);
        ClearBackground(Color.DarkGray);
        BeginMode3D(cameraData.RaylibCamera!.Value);
    }

    public override void End() {

        EndMode3D();
        EndTextureMode();

        DrawTexturePro(
            renderTexture.Texture,
            new Rectangle(0, 0, renderTexture.Texture.Width, -renderTexture.Texture.Height),
            new Rectangle(0, 0, GetScreenWidth(), GetScreenHeight()),
            Vector2.Zero,
            0,
            Color.White);

        HandleDebugInput();
        DrawDebugState();
    }

    public override void Shutdown() {

        UnloadRenderTexture(renderTexture);
    }

    public override void DrawMesh(MeshData meshData, MaterialData materialData, Matrix4x4 matrix) {

        Raylib.DrawMesh(meshData.RaylibMesh!.Value, materialData.RaylibMaterial!.Value, matrix);
    }

    public override void DrawModel(ModelData modelData, Vector3 position, Vector3 rotation, Vector3 scale) {
        
        var transform = Util.TransformMatrix(position, rotation, scale);

        foreach (var mesh in modelData.Meshes) {
            var material = mesh.MaterialIndex >= 0 && mesh.MaterialIndex < modelData.Materials.Count && modelData.Materials[mesh.MaterialIndex].RaylibMaterial.HasValue
                ? modelData.Materials[mesh.MaterialIndex].RaylibMaterial!.Value
                : mesh.FallbackMaterial;
            Raylib.DrawMesh(mesh.Mesh, material, transform);
        }
    }

    private void HandleDebugInput() {

        if (IsKeyPressed(KeyboardKey.F5)) {
            RenderSettings.RenderScale = MathF.Max(
                RenderSettings.MinRenderScale,
                RenderSettings.RenderScale - RenderSettings.RenderScaleStep);
        }

        if (IsKeyPressed(KeyboardKey.F6)) {
            RenderSettings.RenderScale = MathF.Min(
                RenderSettings.MaxRenderScale,
                RenderSettings.RenderScale + RenderSettings.RenderScaleStep);
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

    private void EnsureTextureSize() {

        var (newWidth, newHeight) = GetRenderDimensions();

        if (newWidth == renderWidth && newHeight == renderHeight) {

            return;
        }

        UnloadRenderTexture(renderTexture);

        renderWidth = newWidth;
        renderHeight = newHeight;

        renderTexture = LoadRenderTexture(renderWidth, renderHeight);
    }

    private void DrawDebugState() {

        DrawText($"F5/F6 Render Scale: {RenderSettings.RenderScale:0.00}x", 10, 56, 20, Color.DarkBlue);
    }
}