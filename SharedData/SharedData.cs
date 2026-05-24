internal abstract class SharedData {

    public void Build() {
        
        BuildRaylib();
        BuildOptix();
    }

    public void Unload() {
        
        UnloadOptix();
        UnloadRaylib();
    }

    protected abstract void BuildRaylib();
    protected abstract void UnloadRaylib();
    protected abstract void BuildOptix();

    protected abstract void UnloadOptix();
}
