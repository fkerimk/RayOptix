using System.Numerics;
using Raylib_cs;
using static Raylib_cs.Raylib;

internal class RaylibRenderer(Camera mainCamera) : Renderer(mainCamera) {
    
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
        BeginMode3D(new Camera3D(MainCamera.Position, MainCamera.Target, Vector3.UnitY, MainCamera.Fov, CameraProjection.Perspective));
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

    public override void DrawMesh(Mesh mesh, Material material, Matrix4x4 matrix) {

        Raylib.DrawMesh(mesh.RaylibMesh!.Value, material.RaylibMaterial!.Value, matrix);
    }

    public override void DrawModel(Model model, Matrix4x4 matrix) {

        foreach (var mesh in model.Meshes) {
            var material = mesh.MaterialIndex >= 0 && mesh.MaterialIndex < model.Materials.Count && model.Materials[mesh.MaterialIndex].RaylibMaterial.HasValue
                ? model.Materials[mesh.MaterialIndex].RaylibMaterial!.Value
                : mesh.FallbackMaterial;

            if (mesh.RaylibMesh.HasValue && material.HasValue) {
                Raylib.DrawMesh(mesh.RaylibMesh.Value, material.Value, matrix);
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
