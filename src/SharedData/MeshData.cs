using System.Numerics;
using Raylib_cs;

internal class MeshData(int vertexCount, int triangleCount, float[] vertices, float[] normals, float[] texCoords, ushort[] indices) : SharedData {
    
    public int VertexCount = vertexCount;
    public int TriangleCount = triangleCount;
    public float[] Vertices = vertices;
    public float[] Normals = normals;
    public float[] TexCoords = texCoords;
    public ushort[] Indices = indices;
    
    public Mesh? RaylibMesh;
    public OptixMesh? OptixMeshData;

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

        if (RaylibMesh.HasValue) Raylib.UnloadMesh(RaylibMesh.Value);
    }

    protected override void BuildOptix() {

        UnloadOptix();
        OptixMeshData = new OptixMesh(Vertices, Normals, Indices);
    }

    protected override void UnloadOptix() {

        OptixMeshData = null;
    }

    public OptixGeometry CreateOptixGeometry(Matrix4x4 matrix) {

        if (OptixMeshData is null) {

            throw new InvalidOperationException("OptiX mesh data has not been built.");
        }

        var geometryVertices = new float[OptixMeshData.Vertices.Length];
        var geometryNormals = new float[OptixMeshData.Normals.Length];
        var normalMatrix = matrix;
        normalMatrix.M41 = 0;
        normalMatrix.M42 = 0;
        normalMatrix.M43 = 0;

        for (var index = 0; index < OptixMeshData.Vertices.Length; index += 3) {

            var position = Raymath.Vector3Transform(new Vector3(
                OptixMeshData.Vertices[index],
                OptixMeshData.Vertices[index + 1],
                OptixMeshData.Vertices[index + 2]), matrix);

            var normal = Vector3.Normalize(Raymath.Vector3Transform(new Vector3(
                OptixMeshData.Normals[index],
                OptixMeshData.Normals[index + 1],
                OptixMeshData.Normals[index + 2]), normalMatrix));

            geometryVertices[index] = position.X;
            geometryVertices[index + 1] = position.Y;
            geometryVertices[index + 2] = position.Z;

            geometryNormals[index] = normal.X;
            geometryNormals[index + 1] = normal.Y;
            geometryNormals[index + 2] = normal.Z;
        }

        return new OptixGeometry(geometryVertices, geometryNormals, OptixMeshData.Indices);
    }
}
