using System.Numerics;
using Raylib_cs;
using static Raylib_cs.Raylib;

public static unsafe class Input {

    public static Vector2 MousePos => GetMousePosition();
    public static Vector2 MouseDelta => GetMouseDelta();
    public static float MouseScroll => GetMouseWheelMove();

    public static bool IsButtonDown     (Button button, int gamepad = 0) => ResolveButton(button, &IsMouseButtonDown    , &IsKeyDown    , &IsGamepadButtonDown    , gamepad);
    public static bool IsButtonPressed  (Button button, int gamepad = 0) => ResolveButton(button, &IsMouseButtonPressed , &IsKeyPressed , &IsGamepadButtonPressed , gamepad);
    public static bool IsButtonReleased (Button button, int gamepad = 0) => ResolveButton(button, &IsMouseButtonReleased, &IsKeyReleased, &IsGamepadButtonReleased, gamepad);
    public static bool IsButtonUp       (Button button, int gamepad = 0) => ResolveButton(button, &IsMouseButtonUp      , &IsKeyUp      , &IsGamepadButtonUp      , gamepad);

    public static void SetCursor(bool enabled) {
        
        if (enabled)
             EnableCursor();
        else DisableCursor();
    }
    
    private static bool ResolveButton(Button b, delegate*<MouseButton, CBool> mouse, delegate*<KeyboardKey, CBool> keyboard, delegate*<int, GamepadButton, CBool> gamepad, int gamepadId = 0) {
        
        var val = (uint)b;
        if (val == 0) return false;
        
        var code = (int)(val & 0xFFFFFF);
        
        return (val >> 24) switch {
            
            1 => mouse    ((MouseButton)code),
            2 => keyboard ((KeyboardKey)code),
            3 => gamepad  (gamepadId, (GamepadButton)code),
            
            _ => false
        };
    }
}

public enum Button : uint {

    None = 0,

    // Mouse
    MouseLeft    = (1 << 24) | MouseButton.Left,
    MouseRight   = (1 << 24) | MouseButton.Right,
    MouseMiddle  = (1 << 24) | MouseButton.Middle,
    MouseSide    = (1 << 24) | MouseButton.Side,
    MouseExtra   = (1 << 24) | MouseButton.Extra,
    MouseForward = (1 << 24) | MouseButton.Forward,
    MouseBack    = (1 << 24) | MouseButton.Back,
    
    // Keyboard
    KeyBoardApostrophe   = (2 << 24) | KeyboardKey.Apostrophe,
    KeyBoardComma        = (2 << 24) | KeyboardKey.Comma,
    KeyBoardMinus        = (2 << 24) | KeyboardKey.Minus,
    KeyBoardPeriod       = (2 << 24) | KeyboardKey.Period,
    KeyBoardSlash        = (2 << 24) | KeyboardKey.Slash,
    KeyBoard0            = (2 << 24) | KeyboardKey.Zero,
    KeyBoard1            = (2 << 24) | KeyboardKey.One,
    KeyBoard2            = (2 << 24) | KeyboardKey.Two,
    KeyBoard3            = (2 << 24) | KeyboardKey.Three,
    KeyBoard4            = (2 << 24) | KeyboardKey.Four,
    KeyBoard5            = (2 << 24) | KeyboardKey.Five,
    KeyBoard6            = (2 << 24) | KeyboardKey.Six,
    KeyBoard7            = (2 << 24) | KeyboardKey.Seven,
    KeyBoard8            = (2 << 24) | KeyboardKey.Eight,
    KeyBoard9            = (2 << 24) | KeyboardKey.Nine,
    KeyBoardSemicolon    = (2 << 24) | KeyboardKey.Semicolon,
    KeyBoardEqual        = (2 << 24) | KeyboardKey.Equal,
    KeyBoardA            = (2 << 24) | KeyboardKey.A,
    KeyBoardB            = (2 << 24) | KeyboardKey.B,
    KeyBoardC            = (2 << 24) | KeyboardKey.C,
    KeyBoardD            = (2 << 24) | KeyboardKey.D,
    KeyBoardE            = (2 << 24) | KeyboardKey.E,
    KeyBoardF            = (2 << 24) | KeyboardKey.F,
    KeyBoardG            = (2 << 24) | KeyboardKey.G,
    KeyBoardH            = (2 << 24) | KeyboardKey.H,
    KeyBoardI            = (2 << 24) | KeyboardKey.I,
    KeyBoardJ            = (2 << 24) | KeyboardKey.J,
    KeyBoardK            = (2 << 24) | KeyboardKey.K,
    KeyBoardL            = (2 << 24) | KeyboardKey.L,
    KeyBoardM            = (2 << 24) | KeyboardKey.M,
    KeyBoardN            = (2 << 24) | KeyboardKey.N,
    KeyBoardO            = (2 << 24) | KeyboardKey.O,
    KeyBoardP            = (2 << 24) | KeyboardKey.P,
    KeyBoardQ            = (2 << 24) | KeyboardKey.Q,
    KeyBoardR            = (2 << 24) | KeyboardKey.R,
    KeyBoardS            = (2 << 24) | KeyboardKey.S,
    KeyBoardT            = (2 << 24) | KeyboardKey.T,
    KeyBoardU            = (2 << 24) | KeyboardKey.U,
    KeyBoardV            = (2 << 24) | KeyboardKey.V,
    KeyBoardW            = (2 << 24) | KeyboardKey.W,
    KeyBoardX            = (2 << 24) | KeyboardKey.X,
    KeyBoardY            = (2 << 24) | KeyboardKey.Y,
    KeyBoardZ            = (2 << 24) | KeyboardKey.Z,
    KeyBoardSpace        = (2 << 24) | KeyboardKey.Space,
    KeyBoardEscape       = (2 << 24) | KeyboardKey.Escape,
    KeyBoardEnter        = (2 << 24) | KeyboardKey.Enter,
    KeyBoardTab          = (2 << 24) | KeyboardKey.Tab,
    KeyBoardBackspace    = (2 << 24) | KeyboardKey.Backspace,
    KeyBoardInsert       = (2 << 24) | KeyboardKey.Insert,
    KeyBoardDelete       = (2 << 24) | KeyboardKey.Delete,
    KeyBoardRight        = (2 << 24) | KeyboardKey.Right,
    KeyBoardLeft         = (2 << 24) | KeyboardKey.Left,
    KeyBoardDown         = (2 << 24) | KeyboardKey.Down,
    KeyBoardUp           = (2 << 24) | KeyboardKey.Up,
    KeyBoardPageUp       = (2 << 24) | KeyboardKey.PageUp,
    KeyBoardPageDown     = (2 << 24) | KeyboardKey.PageDown,
    KeyBoardHome         = (2 << 24) | KeyboardKey.Home,
    KeyBoardEnd          = (2 << 24) | KeyboardKey.End,
    KeyBoardCapsLock     = (2 << 24) | KeyboardKey.CapsLock,
    KeyBoardScrollLock   = (2 << 24) | KeyboardKey.ScrollLock,
    KeyBoardNumLock      = (2 << 24) | KeyboardKey.NumLock,
    KeyBoardPrintScreen  = (2 << 24) | KeyboardKey.PrintScreen,
    KeyBoardPause        = (2 << 24) | KeyboardKey.Pause,
    KeyBoardF1           = (2 << 24) | KeyboardKey.F1,
    KeyBoardF2           = (2 << 24) | KeyboardKey.F2,
    KeyBoardF3           = (2 << 24) | KeyboardKey.F3,
    KeyBoardF4           = (2 << 24) | KeyboardKey.F4,
    KeyBoardF5           = (2 << 24) | KeyboardKey.F5,
    KeyBoardF6           = (2 << 24) | KeyboardKey.F6,
    KeyBoardF7           = (2 << 24) | KeyboardKey.F7,
    KeyBoardF8           = (2 << 24) | KeyboardKey.F8,
    KeyBoardF9           = (2 << 24) | KeyboardKey.F9,
    KeyBoardF10          = (2 << 24) | KeyboardKey.F10,
    KeyBoardF11          = (2 << 24) | KeyboardKey.F11,
    KeyBoardF12          = (2 << 24) | KeyboardKey.F12,
    KeyBoardLeftShift    = (2 << 24) | KeyboardKey.LeftShift,
    KeyBoardLeftControl  = (2 << 24) | KeyboardKey.LeftControl,
    KeyBoardLeftAlt      = (2 << 24) | KeyboardKey.LeftAlt,
    KeyBoardLeftSuper    = (2 << 24) | KeyboardKey.LeftSuper,
    KeyBoardRightShift   = (2 << 24) | KeyboardKey.RightShift,
    KeyBoardRightControl = (2 << 24) | KeyboardKey.RightControl,
    KeyBoardRightAlt     = (2 << 24) | KeyboardKey.RightAlt,
    KeyBoardRightSuper   = (2 << 24) | KeyboardKey.RightSuper,
    KeyBoardKeyboardMenu = (2 << 24) | KeyboardKey.KeyboardMenu,
    KeyBoardLeftBracket  = (2 << 24) | KeyboardKey.LeftBracket,
    KeyBoardBackslash    = (2 << 24) | KeyboardKey.Backslash,
    KeyBoardRightBracket = (2 << 24) | KeyboardKey.RightBracket,
    KeyBoardGrave        = (2 << 24) | KeyboardKey.Grave,
    KeyBoardKp0          = (2 << 24) | KeyboardKey.Kp0,
    KeyBoardKp1          = (2 << 24) | KeyboardKey.Kp1,
    KeyBoardKp2          = (2 << 24) | KeyboardKey.Kp2,
    KeyBoardKp3          = (2 << 24) | KeyboardKey.Kp3,
    KeyBoardKp4          = (2 << 24) | KeyboardKey.Kp4,
    KeyBoardKp5          = (2 << 24) | KeyboardKey.Kp5,
    KeyBoardKp6          = (2 << 24) | KeyboardKey.Kp6,
    KeyBoardKp7          = (2 << 24) | KeyboardKey.Kp7,
    KeyBoardKp8          = (2 << 24) | KeyboardKey.Kp8,
    KeyBoardKp9          = (2 << 24) | KeyboardKey.Kp9,
    KeyBoardKpDecimal    = (2 << 24) | KeyboardKey.KpDecimal,
    KeyBoardKpDivide     = (2 << 24) | KeyboardKey.KpDivide,
    KeyBoardKpMultiply   = (2 << 24) | KeyboardKey.KpMultiply,
    KeyBoardKpSubtract   = (2 << 24) | KeyboardKey.KpSubtract,
    KeyBoardKpAdd        = (2 << 24) | KeyboardKey.KpAdd,
    KeyBoardKpEnter      = (2 << 24) | KeyboardKey.KpEnter,
    KeyBoardKpEqual      = (2 << 24) | KeyboardKey.KpEqual,
    KeyBoardBack         = (2 << 24) | KeyboardKey.Back,
    KeyBoardMenu         = (2 << 24) | KeyboardKey.Menu,
    KeyBoardVolumeUp     = (2 << 24) | KeyboardKey.VolumeUp,
    KeyBoardVolumeDown   = (2 << 24) | KeyboardKey.VolumeDown,
    
    // Gamepad
    GamePadLeftFaceUp     = (3 << 24) | GamepadButton.LeftFaceUp,
    GamePadLeftFaceRight  = (3 << 24) | GamepadButton.LeftFaceRight,
    GamePadLeftFaceDown   = (3 << 24) | GamepadButton.LeftFaceDown,
    GamePadLeftFaceLeft   = (3 << 24) | GamepadButton.LeftFaceLeft,
    GamePadRightFaceUp    = (3 << 24) | GamepadButton.RightFaceUp,
    GamePadRightFaceRight = (3 << 24) | GamepadButton.RightFaceRight,
    GamePadRightFaceDown  = (3 << 24) | GamepadButton.RightFaceDown,
    GamePadRightFaceLeft  = (3 << 24) | GamepadButton.RightFaceLeft,
    GamePadLeftTrigger1   = (3 << 24) | GamepadButton.LeftTrigger1,
    GamePadLeftTrigger2   = (3 << 24) | GamepadButton.LeftTrigger2,
    GamePadRightTrigger1  = (3 << 24) | GamepadButton.RightTrigger1,
    GamePadRightTrigger2  = (3 << 24) | GamepadButton.RightTrigger2,
    GamePadMiddleLeft     = (3 << 24) | GamepadButton.MiddleLeft,
    GamePadMiddle         = (3 << 24) | GamepadButton.Middle,
    GamePadMiddleRight    = (3 << 24) | GamepadButton.MiddleRight,
    GamePadLeftThumb      = (3 << 24) | GamepadButton.LeftThumb,
    GamePadRightThumb     = (3 << 24) | GamepadButton.RightThumb,
}