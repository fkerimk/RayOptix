using System.Numerics;
using Raylib_cs;
using static Raylib_cs.Raylib;

internal static class Program {

    private const float MoveSpeed = 6.0f;
    private const float MouseSensitivity = 0.0035f;
    private const float PitchLimit = 1.55f;

    private static void Main(string[] args) {

        SetConfigFlags(ConfigFlags.ResizableWindow | ConfigFlags.VSyncHint);
        SetTraceLogLevel(TraceLogLevel.Error);
        InitWindow(1280, 720, "RayOptix");
        SetWindowMonitor(0);

        var camera = new CameraData(new Vector3(-5, 5, 5), Vector3.Zero, 60);
        camera.Build();

        var initialForward = Vector3.Normalize(camera.Target - camera.Position);
        var yaw = MathF.Atan2(initialForward.Z, initialForward.X);
        var pitch = MathF.Asin(initialForward.Y);
        
        var surfaceMesh = Primitive.Mesh.Cube(5, 0.25f, 5);
        surfaceMesh.Build();
        
        var floorMaterial = new MaterialData { Color = new Vector4(0.78f, 0.73f, 0.66f, 1) };
        floorMaterial.Build();

        var wallMaterial = new MaterialData { Color = new Vector4(0.55f, 0.70f, 0.82f, 1), Reflectivity = 0.9f };
        wallMaterial.Build();
        
        var cubeMesh = Primitive.Mesh.Cube(1);
        cubeMesh.Build();
        
        var cubeMaterial = new MaterialData { Color = new Vector4(0.92f, 0.36f, 0.24f, 1) };
        cubeMaterial.Build();

        var raylibRenderer = new RaylibRenderer(camera);
        var optixRenderer = new OptixRenderer(camera);

        Renderer[] renderers = [raylibRenderer, optixRenderer];
        
        var activeRendererIndex = 0;
        var activeRenderer = renderers[activeRendererIndex];

        foreach (var renderer in renderers) renderer.Init();
        
        while (!WindowShouldClose()) {

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

            camera.Fov -= GetMouseWheelMove() * 5;
            
            camera.Build();
            
            if (IsKeyPressed(KeyboardKey.Space)) {

                activeRendererIndex = (activeRendererIndex + 1) % renderers.Length;
                activeRenderer = renderers[activeRendererIndex];
            }

            BeginDrawing();

            ClearBackground(Color.DarkGray);

            activeRenderer.Begin();
            
            activeRenderer.DrawMesh(surfaceMesh, floorMaterial, TransformMatrix(new Vector3(0, -0.125f, 0), Vector3.Zero, Vector3.One));
            activeRenderer.DrawMesh(surfaceMesh, wallMaterial, TransformMatrix(new Vector3(2.5f - 0.125f, 2.5f - 0.125f, 0), new Vector3(90, 0, 0), Vector3.One));

            //for (int i = 0; i < 128; i++) { // performance test
            activeRenderer.DrawMesh(cubeMesh, cubeMaterial, TransformMatrix(new Vector3(0, MathF.Sin((float)GetTime()) + 2.5f, -1), new Vector3(0,  (float)GetTime() * 90, 0), Vector3.One));
            activeRenderer.DrawMesh(cubeMesh, cubeMaterial, TransformMatrix(new Vector3(0, MathF.Cos((float)GetTime()) + 2.5f,  1), new Vector3(0, -(float)GetTime() * 90, 0), Vector3.One));
            //}
            
            activeRenderer.End();
            
            DrawText($"Renderer: {activeRenderer.Name}", 10, 10, 32, Color.Orange);

            DrawFPS(10, GetScreenHeight() - 32);
            
            EndDrawing();
        }

        cubeMaterial.Unload();
        cubeMesh.Unload();
        
        foreach (var renderer in renderers) renderer.Shutdown();
        
        CloseWindow();
    }

    private static Matrix4x4 TransformMatrix(Vector3 position, Vector3 rotation, Vector3 scale) {

        var positionMatrix = Raymath.MatrixTranslate(position.X, position.Y, position.Z);
        var rotationMatrix = Raymath.QuaternionToMatrix(Raymath.QuaternionFromEuler(rotation.Z * DEG2RAD, rotation.Y * DEG2RAD, rotation.X * DEG2RAD));
        var scaleMatrix = Raymath.MatrixScale(scale.X, scale.Y, scale.Z);
        
        return Raymath.MatrixMultiply(Raymath.MatrixMultiply(scaleMatrix, rotationMatrix), positionMatrix);
    }

    private static Vector3 GetForward(float yaw, float pitch) {

        return Vector3.Normalize(new Vector3(
            MathF.Cos(pitch) * MathF.Cos(yaw),
            MathF.Sin(pitch),
            MathF.Cos(pitch) * MathF.Sin(yaw)));
    }
}
