public static class Time {

    public static float TotalTime => (float)Raylib_cs.Raylib.GetTime();
    public static float DeltaTime => Raylib_cs.Raylib.GetFrameTime();
}