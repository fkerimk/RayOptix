using System.Numerics;
using System.Runtime.InteropServices;

[StructLayout(LayoutKind.Sequential)]
internal struct CameraData(Vector3 position, Vector3 target, float fov) : IEquatable<CameraData> {
    
    public Vector3 Position = position;
    public Vector3 Target = target;
    public float Fov = fov;

    public static bool operator ==(CameraData a, CameraData b) => a.Equals(b);
    public static bool operator !=(CameraData a, CameraData b) => !(a == b);

    public bool Equals(CameraData other) => Position.Equals(other.Position) && Target.Equals(other.Target) && Fov.Equals(other.Fov);
    public override bool Equals(object? obj) => obj is CameraData other && Equals(other);
    public override int GetHashCode() => HashCode.Combine(Position, Target, Fov);
}

internal class Camera(Vector3 position, Vector3 target, float fov) : SharedData("cam_" + Guid.NewGuid()) {

    public Vector3 Position { get => Data.Position; set => Data.Position = value; }
    public Vector3 Target { get => Data.Target; set => Data.Target = value; }
    public float Fov { get => Data.Fov; set => Data.Fov = value; }
    
    public CameraData Data = new(position, target, fov);
}
