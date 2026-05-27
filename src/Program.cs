using System.Numerics;
using static Time;
using static Util;
using static Input;
using static Render;
using static Primitives;
using static Button;

Setup(new Vector3(-5, 5, 5), Vector3.Zero);

FreeCam.Init();

var cubeMesh = PrimitiveMeshCube(1);

var floorMaterial = new Material(color: new Vector4(0.78f, 0.73f, 0.66f, 1));
var wallMaterial = new Material(color: new Vector4(0.55f, 0.70f, 0.82f, 1), reflectivity: 1);
var cubeMaterial = new Material(color: new Vector4(0.92f, 0.36f, 0.24f, 1));

var human = new Model("res/model/human.glb");

while (IsAlive) {

    FreeCam.Update();
    
    human.UpdateAnimation(DeltaTime);
    
    if (IsButtonPressed(KeyBoardSpace))
        ActiveRenderer = ActiveRenderer is OptixRenderer ? Render.RaylibRenderer : Render.OptixRenderer;

    Start();
    
    DrawMesh(cubeMesh, floorMaterial, new Vector3(0, -0.125f, 0), Vector3.Zero, new Vector3(5, 0.25f, 5));
    DrawMesh(cubeMesh, wallMaterial, new Vector3(2.5f - 0.125f, 2.5f, 0), new Vector3(90, 0, 0), new Vector3(5, 0.25f, 5));
    DrawMesh(cubeMesh, wallMaterial, new Vector3(0, 2.5f, -2.5f + 0.125f), new Vector3(90, 0, 90), new Vector3(5, 0.25f, 5));
    DrawMesh(cubeMesh, cubeMaterial, new Vector3(0, MathF.Sin(TotalTime) + 3.5f, -1), new Vector3(0,  TotalTime * 90, 0), Vector3.One);
    DrawMesh(cubeMesh, cubeMaterial, new Vector3(0, MathF.Cos(TotalTime) + 3.5f,  1), new Vector3(0, -TotalTime * 90, 0), Vector3.One);

    SpiralRotation((pos, angle) => DrawModel(human,  new Vector3(pos.X, 0, pos.Y),  Vector3.UnitY * angle, Vector3.One), spiral: 100);
    
    Stop();
}

Shutdown();