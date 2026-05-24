using System.Numerics;
using Raylib_cs;

internal class MaterialData(Vector3 albedo, bool optixReflective = false, float optixReflectivity = 1.0f) : SharedData {

    public Vector3 Albedo = albedo;
    public bool OptixReflective = optixReflective;
    public float OptixReflectivity = optixReflectivity;
    public Raylib_cs.Material? RaylibMaterial;
    public OptixMaterial? OptixMaterialData;

    protected override void BuildRaylib() {

        UnloadRaylib();

        var material = Raylib_cs.Raylib.LoadMaterialDefault();
        unsafe {

            material.Maps[(int)MaterialMapIndex.Albedo].Color = ToColor(Albedo);
        }
        RaylibMaterial = material;
    }

    protected override void UnloadRaylib() {

        if (RaylibMaterial.HasValue) Raylib_cs.Raylib.UnloadMaterial(RaylibMaterial.Value);
    }

    protected override void BuildOptix() {

        UnloadOptix();
        OptixMaterialData = new OptixMaterial(
            Albedo.X,
            Albedo.Y,
            Albedo.Z,
            OptixReflective ? 1 : 0,
            OptixReflectivity);
    }

    protected override void UnloadOptix() {

        OptixMaterialData = null;
    }

    private static Color ToColor(Vector3 color) {

        return new Color(
            ToColorChannel(color.X),
            ToColorChannel(color.Y),
            ToColorChannel(color.Z),
            (byte)255);
    }

    private static byte ToColorChannel(float value) {

        return (byte)(Math.Clamp(value, 0.0f, 1.0f) * 255.0f);
    }
}
