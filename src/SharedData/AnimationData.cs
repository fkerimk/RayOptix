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
