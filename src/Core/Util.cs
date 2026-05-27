using System.Numerics;

using static Raylib_cs.Raylib;
using static Raylib_cs.Raymath;

using static Time;

internal static class Util {
    
    
    public static Matrix4x4 TransformMatrix(Vector3 position, Vector3 rotation, Vector3 scale) {

        var positionMatrix = MatrixTranslate(position.X, position.Y, position.Z);
        var rotationMatrix = QuaternionToMatrix(QuaternionFromEuler(rotation.Z * DEG2RAD, rotation.Y * DEG2RAD, rotation.X * DEG2RAD));
        var scaleMatrix = MatrixScale(scale.X, scale.Y, scale.Z);
        
        return MatrixMultiply(MatrixMultiply(scaleMatrix, rotationMatrix), positionMatrix);
    }
    
    public static Vector3 GetForward(float yaw, float pitch) {

        return Vector3.Normalize(new Vector3 (
            
            MathF.Cos(pitch) * MathF.Cos(yaw),
            MathF.Sin(pitch),
            MathF.Cos(pitch) * MathF.Sin(yaw))
        );
    }

    public static void SpiralRotation( Action<Vector2, float> action, int size = 5, float spacing = 0.75f, float speed = 50, float spiral = 25) {
        
        var offset = (size - 1) * spacing / 2f;

        for (var x = 0; x < size; x++)
        for (var z = 0; z < size; z++) {

            var pos = new Vector2(x * spacing - offset, z * spacing - offset);

            var distance = (float)Math.Sqrt(pos.X * pos.X + pos.Y * pos.Y);

            var rotationAngle = TotalTime *  speed - distance * spiral;

            action.Invoke(pos, rotationAngle);
        }
    }
}