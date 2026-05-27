using System.Numerics;
using Raylib_cs;

internal unsafe class Material : SharedData {

    public Vector4 Color;
    public Vector4 EmissiveColor;
    public float Reflectivity;
    
    public readonly Dictionary<MaterialMapIndex, TextureData> Textures = [];
    
    public Raylib_cs.Material? RaylibMaterial;

    public Material(
        
        Vector4? color = null,
        Vector4? emissiveColor = null,
        float? reflectivity = null
        
    ) : base("mat_" + Guid.NewGuid()) {
        
        Color = color ?? Vector4.One;
        EmissiveColor = emissiveColor ?? Vector4.Zero;
        Reflectivity = reflectivity ?? 0;
        
        Build();
    }

    protected override void BuildRaylib() {

        UnloadRaylib();

        var material = Raylib.LoadMaterialDefault();

        material.Maps[(int)MaterialMapIndex.Albedo].Color = ToColor(Color);
        material.Maps[(int)MaterialMapIndex.Emission].Color = ToColor(EmissiveColor);

        foreach (var (mapIndex, textureData) in Textures) {
            
            if (!textureData.RaylibTexture.HasValue)
                textureData.Build();

            if (textureData.RaylibTexture.HasValue)
                material.Maps[(int)mapIndex].Texture = textureData.RaylibTexture.Value;
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
