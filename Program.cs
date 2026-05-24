using System.Numerics;
using Raylib_cs;
using static Raylib_cs.Raylib;

internal static class Program {

    private static void Main(string[] args) {
        
        SetTraceLogLevel(TraceLogLevel.Error);
        InitWindow(1280, 720, "RayOptix");
        SetWindowMonitor(0);

        var cam = new Camera3D {

            Up = Vector3.UnitY,
            Projection = CameraProjection.Perspective,
            FovY = 60,
            Position = new Vector3(5, 5, 5),
            Target = Vector3.Zero,
        };
        
        while (!WindowShouldClose()) {
            
            BeginDrawing();
            
            ClearBackground(Color.DarkGray);
            
            BeginMode3D(cam);
            
            DrawCube(Vector3.Zero, 1, 1, 1, Color.White);
            
            EndMode3D();
            
            EndDrawing();
        }
        
        CloseWindow();
    }
}