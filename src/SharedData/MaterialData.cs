using System.Numerics;
using Raylib_cs;

internal unsafe class MaterialData : SharedData {

    public Vector4 Color = new(1, 1, 1, 1);
    public Vector4 EmissiveColor = Vector4.Zero;
    public float Reflectivity = 0;
    public readonly Dictionary<MaterialMapIndex, TextureData> Textures = [];
    
    public Material? RaylibMaterial;

    protected override void BuildRaylib() {

        UnloadRaylib();

        var material = Raylib.LoadMaterialDefault();

        material.Maps[(int)MaterialMapIndex.Albedo].Color = ToColor(Color);
        material.Maps[(int)MaterialMapIndex.Emission].Color = ToColor(EmissiveColor);

        foreach (var (mapIndex, textureData) in Textures) {
            if (!textureData.RaylibTexture.HasValue) {
                textureData.Build();
            }

            if (textureData.RaylibTexture.HasValue) {
                material.Maps[(int)mapIndex].Texture = textureData.RaylibTexture.Value;
            }
        }
        
        RaylibMaterial = material;
    }

    protected override void UnloadRaylib() {

        if (!RaylibMaterial.HasValue) {
            return;
        }

        var material = RaylibMaterial.Value;
        if (material.Maps != null) {
            for (var i = 0; i < 12; i++) {
                material.Maps[i].Texture = new Texture2D();
            }
        }

        Raylib.UnloadMaterial(material);
        RaylibMaterial = null;
    }

    protected override void BuildOptix() {
    }

    protected override void UnloadOptix() {
    }

    public TextureData? GetTexture(MaterialMapIndex mapIndex) {

        return Textures.GetValueOrDefault(mapIndex);
    }

    private static Color ToColor(Vector4 color) {

        return new Color(
            ToColorChannel(color.X),
            ToColorChannel(color.Y),
            ToColorChannel(color.Z),
            ToColorChannel(color.W));
    }

    private static byte ToColorChannel(float value) {

        return (byte)(Math.Clamp(value, 0.0f, 1.0f) * 255.0f);
    }
}
