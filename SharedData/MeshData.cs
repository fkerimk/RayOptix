internal class MeshData(int vertexCount, int triangleCount, float[] vertices, float[] normals, float[] texCoords, ushort[] indices) : SharedData {
    
    public int VertexCount = vertexCount;
    public int TriangleCount = triangleCount;
    public float[] Vertices = vertices;
    public float[] Normals = normals;
    public float[] TexCoords = texCoords;
    public ushort[] Indices = indices;
    
    public Raylib_cs.Mesh? RaylibMesh;

    protected override void BuildRaylib() {
        
        UnloadRaylib();
        
        var mesh = new Raylib_cs.Mesh(VertexCount, TriangleCount);
        
        mesh.AllocVertices();
        mesh.AllocNormals();
        mesh.AllocTexCoords();
        mesh.AllocIndices();
        
        Vertices.CopyTo(mesh.VerticesAs<float>());
        Normals.CopyTo(mesh.NormalsAs<float>());
        TexCoords.CopyTo(mesh.TexCoordsAs<float>());
        Indices.CopyTo(mesh.IndicesAs<ushort>());

        RaylibMesh = mesh;
        
        Raylib_cs.Raylib.UploadMesh(ref mesh, false);
    }

    protected override void UnloadRaylib() {

        if (RaylibMesh.HasValue) Raylib_cs.Raylib.UnloadMesh(RaylibMesh.Value);
    }
}