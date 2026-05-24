using System.Numerics;

internal class CameraData(Vector3 position, Vector3 target, float fov) : SharedData {
    
    public Vector3 Position = position;
    public Vector3 Target = target;
    public float Fov = fov;
    
    public Raylib_cs.Camera3D? RaylibCamera;
    public OptixCamera? OptixCameraData;

    protected override void BuildRaylib() {
        
        UnloadRaylib();
        
        RaylibCamera = new Raylib_cs.Camera3D {

            Up = Vector3.UnitY,
            Projection = Raylib_cs.CameraProjection.Perspective,
            FovY = Fov,
            Position = Position,
            Target = Target,
        };
    }

    protected override void UnloadRaylib() {

        RaylibCamera = null;
    }

    protected override void BuildOptix() {

        UnloadOptix();
        OptixCameraData = new OptixCamera(Position, Target, Fov);
    }

    protected override void UnloadOptix() {

        OptixCameraData = null;
    }
}
