using System.Numerics;

internal abstract class Renderer(Camera mainCamera) {

    public abstract string Name { get; }

    public readonly Camera MainCamera = mainCamera;
    
    public abstract void Init();
    public abstract void Begin();
    public abstract void End();
    public abstract void Shutdown();
    
    public abstract void DrawMesh(Mesh mesh, Material material, Matrix4x4 matrix);
    public abstract void DrawModel(Model model, Matrix4x4 matrix);
    
}