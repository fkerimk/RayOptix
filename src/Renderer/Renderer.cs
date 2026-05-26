using System.Numerics;

internal abstract class Renderer {

    public abstract string name { get; }
    
    public abstract void Init();
    public abstract void Begin();
    public abstract void End();
    public abstract void Shutdown();
    public abstract void DrawMesh(MeshData meshData, MaterialData materialData, Matrix4x4 matrix);
    public abstract void DrawModel(ModelData modelData, Vector3 position, Vector3 rotation, Vector3 scale) ;
}