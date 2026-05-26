using System.Numerics;

internal sealed class AnimationClipData {

    public string Name = "";
    public double Duration;
    public double TicksPerSecond;
    public readonly bool Loop = true;
    public readonly List<AnimationChannelData> Channels = [];
    public readonly Dictionary<string, AnimationChannelData> ChannelMap = [];
}

internal sealed class AnimationChannelData {

    public string NodeName = "";
    public readonly List<(double Time, Vector3 Position)> PositionKeys = [];
    public readonly List<(double Time, Quaternion Rotation)> RotationKeys = [];
    public readonly List<(double Time, Vector3 Scale)> ScaleKeys = [];
}
