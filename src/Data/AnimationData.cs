using static System.Guid;

internal sealed class AnimationData(List<AnimationClipData> clips) : SharedData("anim_" + NewGuid()) {

    public readonly List<AnimationClipData> Clips = clips;
}
