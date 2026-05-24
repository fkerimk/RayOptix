using System.Numerics;

internal sealed class OptixMesh(float[] vertices, float[] normals, ushort[] indices) {

    public readonly float[] Vertices = vertices;
    public readonly float[] Normals = normals;
    public readonly ushort[] Indices = indices;
}

internal sealed class OptixGeometry(float[] vertices, float[] normals, ushort[] indices) {

    public readonly float[] Vertices = vertices;
    public readonly float[] Normals = normals;
    public readonly ushort[] Indices = indices;
}

internal readonly struct OptixCamera(Vector3 position, Vector3 target, float fovY) {

    public readonly Vector3 Position = position;
    public readonly Vector3 Target = target;
    public readonly float FovY = fovY;
}
