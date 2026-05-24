internal abstract class SharedData {

    public void Build() {
        
        BuildRaylib();
    }

    public void Unload() {
        
        UnloadRaylib();
    }

    protected abstract void BuildRaylib();
    protected abstract void UnloadRaylib();
}