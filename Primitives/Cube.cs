internal static partial class Primitive {
    
    internal static class Mesh {

        public static MeshData Cube(float size) {
            
            var half = size * 0.5f;

            float[] vertices = [
                
                -half, -half,  half,   half, -half,  half,   half,  half,  half,  -half,  half,  half,
                 half, -half, -half,  -half, -half, -half,  -half,  half, -half,   half,  half, -half,
                -half, -half, -half,  -half, -half,  half,  -half,  half,  half,  -half,  half, -half,
                 half, -half,  half,   half, -half, -half,   half,  half, -half,   half,  half,  half,
                -half,  half,  half,   half,  half,  half,   half,  half, -half,  -half,  half, -half,
                -half, -half, -half,   half, -half, -half,   half, -half,  half,  -half, -half,  half,
            ];

            float[] normals = [
                
                 0,  0,  1,   0,  0,  1,   0,  0,  1,   0,  0,  1,
                 0,  0, -1,   0,  0, -1,   0,  0, -1,   0,  0, -1,
                -1,  0,  0,  -1,  0,  0,  -1,  0,  0,  -1,  0,  0,
                 1,  0,  0,   1,  0,  0,   1,  0,  0,   1,  0,  0,
                 0,  1,  0,   0,  1,  0,   0,  1,  0,   0,  1,  0,
                 0, -1,  0,   0, -1,  0,   0, -1,  0,   0, -1,  0,
            ];

            float[] texCoords = [
                
                0, 1,   1, 1,   1, 0,   0, 0,
                0, 1,   1, 1,   1, 0,   0, 0,
                0, 1,   1, 1,   1, 0,   0, 0,
                0, 1,   1, 1,   1, 0,   0, 0,
                0, 1,   1, 1,   1, 0,   0, 0,
                0, 1,   1, 1,   1, 0,   0, 0,
            ];

            ushort[] indices = [
                
                 0,  1,  2,   0,  2,  3,
                 4,  5,  6,   4,  6,  7,
                 8,  9, 10,   8, 10, 11,
                12, 13, 14,  12, 14, 15,
                16, 17, 18,  16, 18, 19,
                20, 21, 22,  20, 22, 23,
            ];

            return new MeshData(24, 12, vertices, normals, texCoords, indices);
        }
    }
}