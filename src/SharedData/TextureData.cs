using Raylib_cs;

internal sealed unsafe class TextureData(string? filePath = null) : SharedData {

    public readonly string? FilePath = filePath;
    public string? Name;
    public byte[]? EncodedBytes;

    public Texture2D? RaylibTexture;
    public byte[]? OptixPixels;
    public int OptixWidth;
    public int OptixHeight;

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

        UnloadOptix();

        if (!TryLoadImageForOptix(out var image)) {
            return;
        }

        try {
            Raylib.ImageFormat(ref image, PixelFormat.UncompressedR8G8B8A8);

            OptixWidth = image.Width;
            OptixHeight = image.Height;

            if (OptixWidth <= 0 || OptixHeight <= 0) {
                return;
            }

            var colors = Raylib.LoadImageColors(image);
            if (colors == null) {
                return;
            }

            try {
                OptixPixels = new byte[OptixWidth * OptixHeight * 4];

                for (var i = 0; i < OptixWidth * OptixHeight; i++) {
                    var color = colors[i];
                    var pixelIndex = i * 4;
                    OptixPixels[pixelIndex] = color.R;
                    OptixPixels[pixelIndex + 1] = color.G;
                    OptixPixels[pixelIndex + 2] = color.B;
                    OptixPixels[pixelIndex + 3] = color.A;
                }
            } finally {
                Raylib.UnloadImageColors(colors);
            }
        } finally {
            Raylib.UnloadImage(image);
        }
    }

    protected override void UnloadOptix() {

        OptixPixels = null;
        OptixWidth = 0;
        OptixHeight = 0;
    }

    public void EnsureOptixPixels() {

        if (OptixPixels is { Length: > 0 } && OptixWidth > 0 && OptixHeight > 0) {
            return;
        }

        BuildOptix();
    }

    private bool TryLoadImageForOptix(out Image image) {
        if (EncodedBytes is { Length: > 0 }) {
            var extension = Path.GetExtension(FilePath ?? Name ?? ".png");
            if (string.IsNullOrWhiteSpace(extension)) {
                extension = ".png";
            }

            image = Raylib.LoadImageFromMemory(extension, EncodedBytes);
            return image.Data != null;
        }

        if (!string.IsNullOrWhiteSpace(FilePath) && File.Exists(FilePath)) {
            image = Raylib.LoadImage(FilePath);
            return image.Data != null;
        }

        if (RaylibTexture.HasValue) {
            image = Raylib.LoadImageFromTexture(RaylibTexture.Value);
            return image.Data != null;
        }

        image = default;
        return false;
    }
}
