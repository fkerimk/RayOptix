using System.Numerics;
using Raylib_cs;
using static Raylib_cs.Raylib;

internal class RaylibRenderer(CameraData cameraData) : Renderer {

    public override string Name => "Raylib";

    public override void Init() {
        
        // not needed
    }

    public override void Begin() {
       
        BeginMode3D(cameraData.RaylibCamera!.Value);
    }

    public override void End() {
        
        EndMode3D();
    }

    public override void Shutdown() {
        
        // not needed
    }

    public override void DrawMesh(MeshData meshData, MaterialData materialData, Matrix4x4 matrix) {
        
        Raylib.DrawMesh(meshData.RaylibMesh!.Value, materialData.RaylibMaterial!.Value, matrix);
    }
}