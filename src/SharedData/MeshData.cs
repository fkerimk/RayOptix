using System.Numerics;
using System.Runtime.InteropServices;
using Raylib_cs;

internal class MeshData : SharedData {

    public readonly int VertexCount;
    public readonly int TriangleCount;
    public readonly float[] Vertices;
    public readonly float[] Normals;
    public readonly float[] TexCoords;
    public readonly uint[] Indices;

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
    public MeshData(int vertexCount, int triangleCount, float[] vertices, float[] normals, float[] texCoords, uint[] indices) {

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
        Indices = indices;

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
        var raylibIndices = Array.ConvertAll(Indices, i => (ushort)i);
        raylibIndices.CopyTo(mesh.IndicesAs<ushort>());

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

    public OptixGeometry CreateOptixGeometry() {

        var geometryVertices = new float[Vertices.Length];
        var geometryNormals = new float[Normals.Length];
        var geometryTexCoords = new float[TexCoords.Length];

        Array.Copy(Vertices, geometryVertices, Vertices.Length);
        Array.Copy(Normals, geometryNormals, Normals.Length);
        Array.Copy(TexCoords, geometryTexCoords, TexCoords.Length);

        return new OptixGeometry(geometryVertices, geometryNormals, geometryTexCoords, Indices);
    }
}
