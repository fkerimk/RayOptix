using Raylib_cs;

internal sealed class TextureData(string? filePath = null) : SharedData {

    public string? FilePath = filePath;
    public string? Name;
    public byte[]? EncodedBytes;

    public Texture2D? RaylibTexture;

    protected override void BuildRaylib() {

        UnloadRaylib();

        if (EncodedBytes is { Length: > 0 }) {
            var extension = Path.GetExtension(FilePath ?? Name ?? ".png");
            if (string.IsNullOrWhiteSpace(extension)) {
                extension = ".png";
            }

            var image = Raylib.LoadImageFromMemory(extension, EncodedBytes);
            RaylibTexture = Raylib.LoadTextureFromImage(image);
            Raylib.UnloadImage(image);
            return;
        }

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
