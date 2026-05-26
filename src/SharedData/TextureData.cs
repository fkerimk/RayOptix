using Raylib_cs;

internal sealed class TextureData(string? filePath = null) : SharedData {

    public string? FilePath = filePath;

    public Texture2D? RaylibTexture;

    protected override void BuildRaylib() {

        UnloadRaylib();

        if (string.IsNullOrWhiteSpace(FilePath) || !File.Exists(FilePath)) {
            return;
        }

        RaylibTexture = Raylib.LoadTexture(FilePath);
    }

    protected override void UnloadRaylib() {

        if (RaylibTexture.HasValue) {
            Raylib.UnloadTexture(RaylibTexture.Value);
            RaylibTexture = null;
        }
    }

    protected override void BuildOptix() {
    }

    protected override void UnloadOptix() {
    }
}
