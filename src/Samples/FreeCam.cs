using System.Numerics;

using static Time;
using static Util;
using static Input;

using static Button;

internal static class FreeCam {
    
    private const float MoveSpeed = 6.0f;
    private const float MouseSensitivity = 0.0035f;
    private const float PitchLimit = 1.55f;
    
    private static float _yaw;
    private static float _pitch;
    
    public static void Init() {
        
        var fwd = Vector3.Normalize(Render.Camera.Target - Render.Camera.Position);
        
        _yaw = MathF.Atan2(fwd.Z, fwd.X);
        _pitch = MathF.Asin(fwd.Y);
    }

    public static void Update() {
        
        if (IsButtonPressed(MouseRight)) SetCursor(false);
        if (IsButtonReleased(MouseRight)) SetCursor(true);

        if (IsButtonDown(MouseRight)) {

            _yaw += MouseDelta.X * MouseSensitivity;
            _pitch -= MouseDelta.Y * MouseSensitivity;
            _pitch = Math.Clamp(_pitch, -PitchLimit, PitchLimit);

            var forward = GetForward(_yaw, _pitch);
            var right = Vector3.Normalize(Vector3.Cross(forward, Vector3.UnitY));
            var move = Vector3.Zero;

            if (IsButtonDown(KeyBoardW)) move += forward;
            if (IsButtonDown(KeyBoardS)) move -= forward;
            if (IsButtonDown(KeyBoardD)) move += right;
            if (IsButtonDown(KeyBoardA)) move -= right;
            if (IsButtonDown(KeyBoardE)) move += Vector3.UnitY;
            if (IsButtonDown(KeyBoardQ)) move -= Vector3.UnitY;

            if (move != Vector3.Zero) {

                move = Vector3.Normalize(move) * (MoveSpeed * DeltaTime);
                Render.Camera.Position += move;
            }

            Render. Camera.Target = Render.Camera.Position + forward;
        }

        Render.Camera.Fov -= MouseScroll * 5;
    }
}