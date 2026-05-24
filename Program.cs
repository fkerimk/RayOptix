using System.Numerics;
using Raylib_cs;
using static Raylib_cs.Raylib;

internal static class Program {

    private static void Main(string[] args) {

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
        raylibRenderer.Init();
        
        var renderer = raylibRenderer;
        
        while (!WindowShouldClose()) {

            BeginDrawing();

            ClearBackground(Color.DarkGray);

            renderer.Begin();
            renderer.DrawMesh(cubeMesh, cubeMaterial, Matrix4x4.Identity);
            renderer.End();
            
            DrawText($"Renderer: {renderer.Name}", 10, 10, 32, Color.Orange);

            EndDrawing();
        }

        cubeMaterial.Unload();
        cubeMesh.Unload();
        
        raylibRenderer.Shutdown();
        
        CloseWindow();
    }
}
