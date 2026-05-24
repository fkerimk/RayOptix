using System.Numerics;
using Raylib_cs;
using static Raylib_cs.Raylib;

internal static class Program {

    private static void Main(string[] args) {

        SetConfigFlags(ConfigFlags.ResizableWindow);
        SetTraceLogLevel(TraceLogLevel.Error);
        InitWindow(1280, 720, "RayOptix");
        SetWindowMonitor(0);

        var camera = new CameraData(new Vector3(5, 5, 5), Vector3.Zero, 60);
        camera.Build();
        
        var cubeMesh = Primitive.Mesh.Cube(1);
        cubeMesh.Build();
        
        var cubeMaterial = new MaterialData();
        cubeMaterial.Build();

        var raylibRenderer = new RaylibRenderer(camera);
        var optixRenderer = new OptixRenderer(camera);

        Renderer[] renderers = [raylibRenderer, optixRenderer];
        
        var activeRendererIndex = 0;
        var activeRenderer = renderers[activeRendererIndex];

        foreach (var renderer in renderers) renderer.Init();
        
        while (!WindowShouldClose()) {

            if (IsKeyPressed(KeyboardKey.Space)) {

                activeRendererIndex = (activeRendererIndex + 1) % renderers.Length;
                activeRenderer = renderers[activeRendererIndex];
            }

            BeginDrawing();

            ClearBackground(Color.DarkGray);

            activeRenderer.Begin();
            activeRenderer.DrawMesh(cubeMesh, cubeMaterial, Matrix4x4.Identity);
            activeRenderer.End();
            
            DrawText($"Renderer: {activeRenderer.Name}", 10, 10, 32, Color.Orange);

            EndDrawing();
        }

        cubeMaterial.Unload();
        cubeMesh.Unload();
        
        foreach (var renderer in renderers) renderer.Shutdown();
        
        CloseWindow();
    }
}
