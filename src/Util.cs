using System.Numerics;
using static Raylib_cs.Raylib;
using static Raylib_cs.Raymath;

internal static class Util {
    
    public static Matrix4x4 TransformMatrix(Vector3 position, Vector3 rotation, Vector3 scale) {

        var positionMatrix = MatrixTranslate(position.X, position.Y, position.Z);
        var rotationMatrix = QuaternionToMatrix(QuaternionFromEuler(rotation.Z * DEG2RAD, rotation.Y * DEG2RAD, rotation.X * DEG2RAD));
        var scaleMatrix = MatrixScale(scale.X, scale.Y, scale.Z);
        
        return MatrixMultiply(MatrixMultiply(scaleMatrix, rotationMatrix), positionMatrix);
    }
}