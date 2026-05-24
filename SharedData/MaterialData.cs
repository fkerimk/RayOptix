internal class MaterialData : SharedData {
    
    public Raylib_cs.Material? RaylibMaterial;

    protected override void BuildRaylib() {
        
        UnloadRaylib();
        
        RaylibMaterial = Raylib_cs.Raylib.LoadMaterialDefault();
    }

    protected override void UnloadRaylib() {

        if (RaylibMaterial.HasValue) Raylib_cs.Raylib.UnloadMaterial(RaylibMaterial.Value);
    }

    protected override void BuildOptix() {
        
    }

    protected override void UnloadOptix() {
        
        
    }
}