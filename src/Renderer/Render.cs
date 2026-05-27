using System.Numerics;
using static Raylib_cs.ConfigFlags;
using static Raylib_cs.Raylib;
using static Raylib_cs.TraceLogLevel;

internal static class Render {

    public static Camera Camera = null!;

    public static RaylibRenderer RaylibRenderer = null!;
    public static OptixRenderer OptixRenderer = null!;
    public static Renderer ActiveRenderer = null!;

    public static readonly Dictionary<string, SharedData> DataDictionary = [];
    
    public static bool IsAlive => !WindowShouldClose();
    public static float Width => GetScreenWidth();
    public static float Height => GetScreenHeight();
    
    public static void Setup(Vector3 camPos, Vector3 camTarget, int width = 1280, int height = 720, string title = "RayOptix") {
        
        SetTraceLogLevel(Error);
        SetConfigFlags(ResizableWindow | VSyncHint);
        InitWindow(width, height, title);
        SetWindowMonitor(0);
        SetExitKey(0);
        
        Camera = new Camera(camPos, camTarget, 60);
        
        RaylibRenderer = new RaylibRenderer(Camera);
        OptixRenderer = new OptixRenderer(Camera);
        
        RaylibRenderer.Init();
        OptixRenderer.Init();

        ActiveRenderer = RaylibRenderer;
    }
    
    public static void Start() {
        
        BeginDrawing();
        ClearBackground(Colors.Background);
        
        ActiveRenderer.Begin();
    }
    
    public static void Stop() {
        
        ActiveRenderer.End();
        
        EndDrawing();
    }

    public static void Shutdown() {

        foreach (var data in DataDictionary.Values) data.Unload();
        
        RaylibRenderer.Shutdown();
        OptixRenderer.Shutdown();
        
        CloseWindow();
    }

    public static void DrawMesh(Mesh mesh, Material material, Vector3 position, Vector3 rotation, Vector3 scale) {
        
        ActiveRenderer.DrawMesh(mesh, material, Util.TransformMatrix(position, rotation, scale));
    }
    
    public static void DrawModel(Model model, Vector3 position, Vector3 rotation, Vector3 scale) {
        
        ActiveRenderer.DrawModel(model, Util.TransformMatrix(position, rotation, scale * model.Scale));
    }
}