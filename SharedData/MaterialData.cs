using System.Numerics;
using Raylib_cs;

internal unsafe class MaterialData : SharedData {

    public Vector4 Color = new(1, 1, 1, 1);
    public float Reflectivity = 0;
    
    public Material? RaylibMaterial;
    public OptixMaterial? OptixMaterialData;

    protected override void BuildRaylib() {

        UnloadRaylib();

        var material = Raylib.LoadMaterialDefault();

        material.Maps[(int)MaterialMapIndex.Albedo].Color = ToColor(Color);
        
        RaylibMaterial = material;
    }

    protected override void UnloadRaylib() {

        if (RaylibMaterial.HasValue) Raylib.UnloadMaterial(RaylibMaterial.Value);
    }

    protected override void BuildOptix() {

        UnloadOptix();
        OptixMaterialData = new OptixMaterial(
            Color.X,
            Color.Y,
            Color.Z,
            Reflectivity > 0 ? 1 : 0,
            Reflectivity);
    }

    protected override void UnloadOptix() {

        OptixMaterialData = null;
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
