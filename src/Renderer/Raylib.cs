using System.Numerics;
using Raylib_cs;
using static Raylib_cs.Raylib;

internal class RaylibRenderer(CameraData cameraData) : Renderer {

    private RenderTexture2D _renderTexture;
    private int _renderWidth;
    private int _renderHeight;

    public override string Name => "Raylib";

    public override void Init() {

        (_renderWidth, _renderHeight) = GetRenderDimensions();
        _renderTexture = LoadRenderTexture(_renderWidth, _renderHeight);
    }

    public override void Begin() {

        EnsureTextureSize();

        BeginTextureMode(_renderTexture);
        ClearBackground(Color.DarkGray);
        BeginMode3D(cameraData.RaylibCamera!.Value);
    }

    public override void End() {

        EndMode3D();
        EndTextureMode();

        DrawTexturePro(
            _renderTexture.Texture,
            new Rectangle(0, 0, _renderTexture.Texture.Width, -_renderTexture.Texture.Height),
            new Rectangle(0, 0, GetScreenWidth(), GetScreenHeight()),
            Vector2.Zero,
            0,
            Color.White);

        HandleDebugInput();
        DrawDebugState();
    }

    public override void Shutdown() {

        UnloadRenderTexture(_renderTexture);
    }

    public override void DrawMesh(MeshData meshData, MaterialData materialData, Matrix4x4 matrix) {

        Raylib.DrawMesh(meshData.RaylibMesh!.Value, materialData.RaylibMaterial!.Value, matrix);
    }

    public override void DrawModel(ModelData modelData, Vector3 position, Vector3 rotation, Vector3 scale) {
        var transform = Util.TransformMatrix(
            modelData.Position + position,
            modelData.RotationDegrees + rotation,
            new Vector3(modelData.Scale.X * scale.X, modelData.Scale.Y * scale.Y, modelData.Scale.Z * scale.Z));

        foreach (var mesh in modelData.Meshes) {
            var material = mesh.MaterialIndex >= 0 && mesh.MaterialIndex < modelData.Materials.Count && modelData.Materials[mesh.MaterialIndex].RaylibMaterial.HasValue
                ? modelData.Materials[mesh.MaterialIndex].RaylibMaterial!.Value
                : mesh.FallbackMaterial;

            if (mesh.RaylibMesh.HasValue && material.HasValue) {
                Raylib.DrawMesh(mesh.RaylibMesh.Value, material.Value, transform);
            }
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

        if (newWidth == _renderWidth && newHeight == _renderHeight) {

            return;
        }

        UnloadRenderTexture(_renderTexture);

        _renderWidth = newWidth;
        _renderHeight = newHeight;

        _renderTexture = LoadRenderTexture(_renderWidth, _renderHeight);
    }

    private void DrawDebugState() {

        DrawText($"F5/F6 Render Scale: {RenderSettings.RenderScale:0.00}x", 10, 56, 20, Color.DarkBlue);
    }
}
