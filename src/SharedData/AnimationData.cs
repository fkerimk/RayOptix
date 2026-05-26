internal sealed class AnimationData(List<AnimationClipData> clips) : SharedData {

    public readonly List<AnimationClipData> Clips = clips;

    protected override void BuildRaylib() {
    }

    protected override void UnloadRaylib() {
    }

    protected override void BuildOptix() {
    }

    protected override void UnloadOptix() {
    }
}

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
    public readonly List<(double Time, System.Numerics.Vector3 Position)> PositionKeys = [];
    public readonly List<(double Time, System.Numerics.Quaternion Rotation)> RotationKeys = [];
    public readonly List<(double Time, System.Numerics.Vector3 Scale)> ScaleKeys = [];
}
