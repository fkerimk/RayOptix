internal abstract class SharedData(string id) {

    public void Build() {

        if (Render.DataDictionary.TryGetValue(id, out var value)) value.Unload();
        
        BuildRaylib();
        BuildOptix();
        
        Render.DataDictionary[id] = this;
    }

    public void Unload() {
        
        Console.WriteLine("[Unload] " + id);
        
        UnloadOptix();
        UnloadRaylib();

        Render.DataDictionary.Remove(id);
    }

    protected virtual void BuildRaylib() {}
    protected virtual void UnloadRaylib() {}
    protected virtual void BuildOptix() {}
    protected virtual void UnloadOptix() {}
}
