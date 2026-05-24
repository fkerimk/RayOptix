using System.Numerics;
using Raylib_cs;
using static Raylib_cs.Raylib;

internal static class Program {

    private const float MoveSpeed = 6.0f;
    private const float MouseSensitivity = 0.0035f;
    private const float PitchLimit = 1.55f;

    private static void Main(string[] args) {

        SetConfigFlags(ConfigFlags.ResizableWindow);
        SetTraceLogLevel(TraceLogLevel.Error);
        InitWindow(1280, 720, "RayOptix");
        SetWindowMonitor(0);

        var camera = new CameraData(new Vector3(5, 5, 5), Vector3.Zero, 60);
        camera.Build();

        var initialForward = Vector3.Normalize(camera.Target - camera.Position);
        var yaw = MathF.Atan2(initialForward.Z, initialForward.X);
        var pitch = MathF.Asin(initialForward.Y);
        
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

            //freecamhere
            if (IsMouseButtonPressed(MouseButton.Right)) DisableCursor();
            if (IsMouseButtonReleased(MouseButton.Right)) EnableCursor();

            if (IsMouseButtonDown(MouseButton.Right)) {

                var mouseDelta = GetMouseDelta();
                yaw += mouseDelta.X * MouseSensitivity;
                pitch -= mouseDelta.Y * MouseSensitivity;
                pitch = Math.Clamp(pitch, -PitchLimit, PitchLimit);

                var forward = GetForward(yaw, pitch);
                var right = Vector3.Normalize(Vector3.Cross(forward, Vector3.UnitY));
                var move = Vector3.Zero;

                if (IsKeyDown(KeyboardKey.W)) move += forward;
                if (IsKeyDown(KeyboardKey.S)) move -= forward;
                if (IsKeyDown(KeyboardKey.D)) move += right;
                if (IsKeyDown(KeyboardKey.A)) move -= right;
                if (IsKeyDown(KeyboardKey.E)) move += Vector3.UnitY;
                if (IsKeyDown(KeyboardKey.Q)) move -= Vector3.UnitY;

                if (move != Vector3.Zero) {

                    move = Vector3.Normalize(move) * (MoveSpeed * GetFrameTime());
                    camera.Position += move;
                }

                camera.Target = camera.Position + forward;
            }
            
            camera.Build();
            
            if (IsKeyPressed(KeyboardKey.Space)) {

                activeRendererIndex = (activeRendererIndex + 1) % renderers.Length;
                activeRenderer = renderers[activeRendererIndex];
            }

            BeginDrawing();

            ClearBackground(Color.DarkGray);

            activeRenderer.Begin();
            activeRenderer.DrawMesh(cubeMesh, cubeMaterial, Raymath.MatrixTranslate(0, 0, 0));
            activeRenderer.DrawMesh(cubeMesh, cubeMaterial, Raymath.MatrixTranslate(3, 0, 0));
            activeRenderer.End();
            
            DrawText($"Renderer: {activeRenderer.Name}", 10, 10, 32, Color.Orange);

            EndDrawing();
        }

        cubeMaterial.Unload();
        cubeMesh.Unload();
        
        foreach (var renderer in renderers) renderer.Shutdown();
        
        CloseWindow();
    }

    private static Vector3 GetForward(float yaw, float pitch) {

        return Vector3.Normalize(new Vector3(
            MathF.Cos(pitch) * MathF.Cos(yaw),
            MathF.Sin(pitch),
            MathF.Cos(pitch) * MathF.Sin(yaw)));
    }
}
