using System.Numerics;

internal sealed class BoneInfoData {

    public string Name = "";
    public int Index;
    public Matrix4x4 Offset;
    public Matrix4x4 FinalTransformation;
}
