using System.Numerics;
using System.Runtime.InteropServices;
using Raylib_cs;

internal class MeshData : SharedData {

    public readonly int VertexCount;
    public readonly int TriangleCount;
    public readonly float[] Vertices;
    public readonly float[] Normals;
    public readonly float[] TexCoords;
    public readonly ushort[] Indices;

    public readonly int MaterialIndex;
    public readonly int MeshIndex;
    public readonly Vector3[]? BaseVertices;
    public readonly Vector3[]? BaseNormals;
    public readonly Vector3[]? AnimatedVertices;
    public readonly Vector3[]? AnimatedNormals;
    public readonly VertexBoneData[]? BoneData;
    public readonly bool UsesSkinning;

    public Material? FallbackMaterial;
    public Mesh? RaylibMesh;
    public MeshData(int vertexCount, int triangleCount, float[] vertices, float[] normals, float[] texCoords, ushort[] indices) {

        VertexCount = vertexCount;
        TriangleCount = triangleCount;
        Vertices = vertices;
        Normals = normals;
        TexCoords = texCoords;
        Indices = indices;
        MaterialIndex = -1;
        MeshIndex = -1;
    }

    public MeshData(
        Vector3[] vertices,
        Vector3[] normals,
        Vector2[] texCoords,
        uint[] indices,
        int materialIndex,
        int meshIndex,
        VertexBoneData[] boneData,
        bool usesSkinning,
        Material fallbackMaterial) {

        VertexCount = vertices.Length;
        TriangleCount = indices.Length / 3;
        Vertices = MemoryMarshal.Cast<Vector3, float>(vertices.AsSpan()).ToArray();
        Normals = MemoryMarshal.Cast<Vector3, float>(normals.AsSpan()).ToArray();
        TexCoords = MemoryMarshal.Cast<Vector2, float>(texCoords.AsSpan()).ToArray();
        Indices = new ushort[indices.Length];

        for (var i = 0; i < indices.Length; i++) {
            Indices[i] = (ushort)indices[i];
        }

        MaterialIndex = materialIndex;
        MeshIndex = meshIndex;
        BaseVertices = vertices;
        BaseNormals = normals;
        AnimatedVertices = vertices.ToArray();
        AnimatedNormals = normals.ToArray();
        BoneData = boneData;
        UsesSkinning = usesSkinning;
        FallbackMaterial = fallbackMaterial;
    }

    protected override void BuildRaylib() {

        UnloadRaylib();

        var mesh = new Mesh(VertexCount, TriangleCount);

        mesh.AllocVertices();
        mesh.AllocNormals();
        mesh.AllocTexCoords();
        mesh.AllocIndices();

        Vertices.CopyTo(mesh.VerticesAs<float>());
        Normals.CopyTo(mesh.NormalsAs<float>());
        TexCoords.CopyTo(mesh.TexCoordsAs<float>());
        Indices.CopyTo(mesh.IndicesAs<ushort>());

        Raylib.UploadMesh(ref mesh, false);

        RaylibMesh = mesh;
    }

    protected override void UnloadRaylib() {

        if (FallbackMaterial.HasValue) {
            Raylib.UnloadMaterial(FallbackMaterial.Value);
            FallbackMaterial = null;
        }

        if (RaylibMesh.HasValue) {
            Raylib.UnloadMesh(RaylibMesh.Value);
            RaylibMesh = null;
        }
    }

    protected override void BuildOptix() {
    }

    protected override void UnloadOptix() {
    }

    public void UploadAnimatedGeometry() {

        if (AnimatedVertices == null || AnimatedNormals == null) {
            return;
        }

        MemoryMarshal.Cast<Vector3, float>(AnimatedVertices.AsSpan()).CopyTo(Vertices);
        MemoryMarshal.Cast<Vector3, float>(AnimatedNormals.AsSpan()).CopyTo(Normals);

        if (!RaylibMesh.HasValue) {
            return;
        }

        unsafe {
            var mesh = RaylibMesh.Value;

            fixed (float* vertexPointer = Vertices) {
                Buffer.MemoryCopy(vertexPointer, mesh.Vertices, (long)Vertices.Length * sizeof(float), (long)Vertices.Length * sizeof(float));
            }

            fixed (float* normalPointer = Normals) {
                Buffer.MemoryCopy(normalPointer, mesh.Normals, (long)Normals.Length * sizeof(float), (long)Normals.Length * sizeof(float));
            }

            Raylib.UpdateMeshBuffer(mesh, 0, mesh.Vertices, Vertices.Length * sizeof(float), 0);
            Raylib.UpdateMeshBuffer(mesh, 2, mesh.Normals, Normals.Length * sizeof(float), 0);
        }
    }

    public OptixGeometry CreateOptixGeometry(Matrix4x4 matrix) {

        var geometryVertices = new float[Vertices.Length];
        var geometryNormals = new float[Normals.Length];
        var geometryTexCoords = new float[TexCoords.Length];
        var normalMatrix = matrix;
        normalMatrix.M41 = 0;
        normalMatrix.M42 = 0;
        normalMatrix.M43 = 0;

        for (var index = 0; index < Vertices.Length; index += 3) {
            var position = Raymath.Vector3Transform(new Vector3(
                Vertices[index],
                Vertices[index + 1],
                Vertices[index + 2]), matrix);

            var normal = Vector3.Normalize(Raymath.Vector3Transform(new Vector3(
                Normals[index],
                Normals[index + 1],
                Normals[index + 2]), normalMatrix));

            geometryVertices[index] = position.X;
            geometryVertices[index + 1] = position.Y;
            geometryVertices[index + 2] = position.Z;

            geometryNormals[index] = normal.X;
            geometryNormals[index + 1] = normal.Y;
            geometryNormals[index + 2] = normal.Z;
        }

        Array.Copy(TexCoords, geometryTexCoords, TexCoords.Length);

        return new OptixGeometry(geometryVertices, geometryNormals, geometryTexCoords, Indices);
    }
}
