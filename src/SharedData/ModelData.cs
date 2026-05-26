using System.Numerics;
using Assimp;
using Raylib_cs;
using static Raylib_cs.Raylib;
using Material = Raylib_cs.Material;
using Matrix4x4 = System.Numerics.Matrix4x4;
using Mesh = Raylib_cs.Mesh;
using Quaternion = System.Numerics.Quaternion;

internal sealed class ModelData(string filePath) : SharedData {

    private static readonly AssimpContext Context = new();
    private static readonly PostProcessSteps DefaultPostProcessSteps =
        PostProcessSteps.Triangulate
        | PostProcessSteps.FlipUVs
        | PostProcessSteps.GenerateSmoothNormals
        | PostProcessSteps.CalculateTangentSpace
        | PostProcessSteps.LimitBoneWeights
        | PostProcessSteps.SortByPrimitiveType;

    public string FilePath = filePath;

    public readonly List<ModelMeshData> Meshes = [];
    public readonly List<BoneInfoData> Bones = [];
    public readonly Dictionary<string, List<BoneInfoData>> BoneMap = [];
    public readonly List<TextureData> Textures = [];
    public readonly List<MaterialData> Materials = [];
    public readonly HashSet<string> AnimatedNodeNames = [];
    public Matrix4x4[] BindMeshNodeTransforms = [];
    public Matrix4x4[] CurrentMeshNodeTransforms = [];

    public AnimationData Animation = new([]);
    public ModelNodeData RootNode = new();
    public Matrix4x4 GlobalInverse = Matrix4x4.Identity;
    public Vector3 Position = Vector3.Zero;
    public Vector3 RotationDegrees = Vector3.Zero;
    public Vector3 Scale = Vector3.One;

    public int ActiveAnimationIndex;
    public float AnimationTimeTicks;

    public bool HasAnimation => Animation.Clips.Count > 0;

    protected override void BuildRaylib() {

        UnloadRaylib();

        var scene = ImportScene(FilePath);

        if (scene.RootNode == null) {
            throw new InvalidOperationException($"Model root node was missing for '{FilePath}'.");
        }

        var globalInverse = ToNumericsMatrix(scene.RootNode.Transform);
        Matrix4x4.Invert(globalInverse, out globalInverse);
        GlobalInverse = globalInverse;

        var boneMapping = new Dictionary<string, List<int>>();
        var animationClips = scene.Animations.Select(ProcessAnimation).ToList();
        var embeddedTextureLookup = LoadEmbeddedTextures(scene);
        BindMeshNodeTransforms = new Matrix4x4[scene.MeshCount];
        CurrentMeshNodeTransforms = new Matrix4x4[scene.MeshCount];

        BuildMaterials(scene, embeddedTextureLookup);

        for (var i = 0; i < scene.Meshes.Count; i++) {
            Meshes.Add(ProcessMesh(scene.Meshes[i], i, Bones, boneMapping));
        }

        RootNode = ProcessNode(scene.RootNode);
        Animation = new AnimationData(animationClips);

        foreach (var bone in Bones) {
            if (!BoneMap.TryGetValue(bone.Name, out var list)) {
                list = [];
                BoneMap[bone.Name] = list;
            }

            list.Add(bone);
        }

        AnimatedNodeNames.Clear();
        foreach (var boneName in BoneMap.Keys) {
            AnimatedNodeNames.Add(boneName);
        }

        foreach (var animation in Animation.Clips) {
            foreach (var nodeName in animation.ChannelMap.Keys) {
                AnimatedNodeNames.Add(nodeName);
            }
        }

        ActiveAnimationIndex = 0;
        AnimationTimeTicks = 0;

        if (Bones.Count > 0) {
            ApplyBindPose();
        }
    }

    protected override void UnloadRaylib() {

        foreach (var mesh in Meshes) {
            mesh.Unload();
        }

        Meshes.Clear();
        Bones.Clear();
        BoneMap.Clear();

        foreach (var texture in Textures) {
            texture.Unload();
        }

        Textures.Clear();

        foreach (var material in Materials) {
            material.Unload();
        }

        Materials.Clear();
        BindMeshNodeTransforms = [];
        CurrentMeshNodeTransforms = [];
        Animation = new AnimationData([]);
        RootNode = new ModelNodeData();
        GlobalInverse = Matrix4x4.Identity;
        ActiveAnimationIndex = 0;
        AnimationTimeTicks = 0;
    }

    protected override void BuildOptix() {
    }

    protected override void UnloadOptix() {
    }

    public void UpdateAnimation(float deltaTime) {

        if (!HasAnimation || Bones.Count == 0) {
            return;
        }

        var clip = Animation.Clips[ActiveAnimationIndex];
        var ticksPerSecond = clip.TicksPerSecond > 0 ? clip.TicksPerSecond : 25.0;
        AnimationTimeTicks += deltaTime * (float)ticksPerSecond;

        if (clip.Duration > 0) {
            if (clip.Loop) {
                AnimationTimeTicks %= (float)clip.Duration;
            } else {
                AnimationTimeTicks = MathF.Min(AnimationTimeTicks, (float)clip.Duration);
            }
        }

        UpdateAnimationHierarchy(RootNode, clip, AnimationTimeTicks, Matrix4x4.Identity, Matrix4x4.Identity, GlobalInverse, BoneMap);

        foreach (var mesh in Meshes) {
            if (mesh.UsesSkinning) {
                SkinMesh(mesh, Bones);
            } else {
                UpdateRigidMesh(mesh);
            }
        }
    }

    public void ApplyBindPose() {

        if (Bones.Count == 0) {
            return;
        }

        ApplyBindPoseHierarchy(RootNode, Matrix4x4.Identity, Matrix4x4.Identity, GlobalInverse, BoneMap);
        Array.Copy(CurrentMeshNodeTransforms, BindMeshNodeTransforms, CurrentMeshNodeTransforms.Length);

        foreach (var mesh in Meshes) {
            if (mesh.UsesSkinning) {
                SkinMesh(mesh, Bones);
            } else {
                ResetRigidMesh(mesh);
            }
        }
    }

    public void DrawRaylib() {

        var transform = CreateTransformMatrix(Position, RotationDegrees, Scale);

        foreach (var mesh in Meshes) {
            var material = mesh.MaterialIndex >= 0 && mesh.MaterialIndex < Materials.Count && Materials[mesh.MaterialIndex].RaylibMaterial.HasValue
                ? Materials[mesh.MaterialIndex].RaylibMaterial!.Value
                : mesh.FallbackMaterial;
            DrawMesh(mesh.Mesh, material, transform);
        }
    }

    private static Assimp.Scene ImportScene(string path) {

        Context.RemoveConfigs();

        if (string.Equals(Path.GetExtension(path), ".fbx", StringComparison.OrdinalIgnoreCase)) {
            Context.SetConfig(new Assimp.Configs.FBXImportEmbeddedTexturesConfig(true));
            Context.SetConfig(new Assimp.Configs.FBXPreservePivotsConfig(false));
            Context.SetConfig(new Assimp.Configs.FBXOptimizeEmptyAnimationCurvesConfig(true));
        }

        return Context.ImportFile(path, DefaultPostProcessSteps);
    }

    private void BuildMaterials(Assimp.Scene scene, Dictionary<string, TextureData> embeddedTextureLookup) {

        var modelDirectory = Path.GetDirectoryName(Path.GetFullPath(FilePath)) ?? Directory.GetCurrentDirectory();

        for (var materialIndex = 0; materialIndex < scene.MaterialCount; materialIndex++) {
            var sourceMaterial = scene.Materials[materialIndex];
            var material = new MaterialData {
                Color = sourceMaterial.HasColorDiffuse ? sourceMaterial.ColorDiffuse : Vector4.One,
                EmissiveColor = sourceMaterial.HasColorEmissive ? sourceMaterial.ColorEmissive : Vector4.Zero,
                Reflectivity = sourceMaterial.HasReflectivity ? sourceMaterial.Reflectivity : 0f
            };

            BindTexture(sourceMaterial, TextureType.BaseColor, MaterialMapIndex.Albedo, material, embeddedTextureLookup, modelDirectory);
            BindTexture(sourceMaterial, TextureType.Diffuse, MaterialMapIndex.Albedo, material, embeddedTextureLookup, modelDirectory);
            BindTexture(sourceMaterial, TextureType.NormalCamera, MaterialMapIndex.Normal, material, embeddedTextureLookup, modelDirectory);
            BindTexture(sourceMaterial, TextureType.Normals, MaterialMapIndex.Normal, material, embeddedTextureLookup, modelDirectory);
            BindTexture(sourceMaterial, TextureType.Height, MaterialMapIndex.Normal, material, embeddedTextureLookup, modelDirectory);
            BindTexture(sourceMaterial, TextureType.Metalness, MaterialMapIndex.Metalness, material, embeddedTextureLookup, modelDirectory);
            BindTexture(sourceMaterial, TextureType.Roughness, MaterialMapIndex.Roughness, material, embeddedTextureLookup, modelDirectory);
            BindTexture(sourceMaterial, TextureType.AmbientOcclusion, MaterialMapIndex.Occlusion, material, embeddedTextureLookup, modelDirectory);
            BindTexture(sourceMaterial, TextureType.Lightmap, MaterialMapIndex.Occlusion, material, embeddedTextureLookup, modelDirectory);
            BindTexture(sourceMaterial, TextureType.EmissionColor, MaterialMapIndex.Emission, material, embeddedTextureLookup, modelDirectory);
            BindTexture(sourceMaterial, TextureType.Emissive, MaterialMapIndex.Emission, material, embeddedTextureLookup, modelDirectory);

            material.Build();
            Materials.Add(material);
        }
    }

    private Dictionary<string, TextureData> LoadEmbeddedTextures(Assimp.Scene scene) {

        var lookup = new Dictionary<string, TextureData>(StringComparer.OrdinalIgnoreCase);

        for (var i = 0; i < scene.TextureCount; i++) {
            var texture = scene.Textures[i];

            if (!texture.IsCompressed || !texture.HasCompressedData || texture.CompressedData is not { Length: > 0 }) {
                continue;
            }

            var textureData = new TextureData(NormalizeTextureExtension(texture.CompressedFormatHint, texture.Filename)) {
                Name = BuildEmbeddedTextureName(texture.Filename, i),
                EncodedBytes = texture.CompressedData
            };
            textureData.Build();
            Textures.Add(textureData);

            RegisterTextureKey(lookup, $"*{i}", textureData);
            RegisterTextureKey(lookup, texture.Filename, textureData);
            RegisterTextureKey(lookup, Path.GetFileName(texture.Filename), textureData);
            RegisterTextureKey(lookup, Path.GetFileNameWithoutExtension(texture.Filename), textureData);
        }

        return lookup;
    }

    private void BindTexture(Assimp.Material sourceMaterial, TextureType textureType, MaterialMapIndex mapIndex, MaterialData material, Dictionary<string, TextureData> embeddedTextureLookup, string modelDirectory) {

        if (material.Textures.ContainsKey(mapIndex)) {
            return;
        }

        var count = sourceMaterial.GetMaterialTextureCount(textureType);
        for (var i = 0; i < count; i++) {
            if (!sourceMaterial.GetMaterialTexture(textureType, i, out var slot)) {
                continue;
            }

            var texture = ResolveTexture(slot.FilePath, embeddedTextureLookup, modelDirectory);
            if (texture == null) {
                continue;
            }

            material.Textures[mapIndex] = texture;
            return;
        }
    }

    private TextureData? ResolveTexture(string? filePath, Dictionary<string, TextureData> embeddedTextureLookup, string modelDirectory) {

        if (string.IsNullOrWhiteSpace(filePath)) {
            return null;
        }

        if (embeddedTextureLookup.TryGetValue(filePath, out var embeddedTexture)) {
            return embeddedTexture;
        }

        var resolvedPath = Path.GetFullPath(Path.Combine(modelDirectory, filePath));
        if (!File.Exists(resolvedPath)) {
            return null;
        }

        var existing = Textures.FirstOrDefault(texture => string.Equals(texture.FilePath, resolvedPath, StringComparison.OrdinalIgnoreCase));
        if (existing != null) {
            return existing;
        }

        var textureData = new TextureData(resolvedPath) {
            Name = Path.GetFileNameWithoutExtension(resolvedPath)
        };
        textureData.Build();
        Textures.Add(textureData);
        return textureData;
    }

    private static ModelMeshData ProcessMesh(Assimp.Mesh mesh, int meshIndex, List<BoneInfoData> bones, Dictionary<string, List<int>> boneMapping) {

        var vertices = new Vector3[mesh.VertexCount];
        var normals = new Vector3[mesh.VertexCount];
        var animatedVertices = new Vector3[mesh.VertexCount];
        var animatedNormals = new Vector3[mesh.VertexCount];
        var texCoords = new Vector2[mesh.VertexCount];
        var indices = new uint[mesh.FaceCount * 3];
        var boneData = new VertexBoneData[mesh.VertexCount];

        for (var i = 0; i < mesh.VertexCount; i++) {
            vertices[i] = ToNumericsVector(mesh.Vertices[i]);
            normals[i] = ToNumericsVector(mesh.Normals[i]);
            animatedVertices[i] = vertices[i];
            animatedNormals[i] = normals[i];

            if (mesh.HasTextureCoords(0)) {
                texCoords[i] = new Vector2(mesh.TextureCoordinateChannels[0][i].X, mesh.TextureCoordinateChannels[0][i].Y);
            }
        }

        for (var i = 0; i < mesh.FaceCount; i++) {
            indices[i * 3] = (uint)mesh.Faces[i].Indices[0];
            indices[i * 3 + 1] = (uint)mesh.Faces[i].Indices[1];
            indices[i * 3 + 2] = (uint)mesh.Faces[i].Indices[2];
        }

        foreach (var bone in mesh.Bones) {
            if (!boneMapping.TryGetValue(bone.Name, out var matchingIndices)) {
                matchingIndices = [];
                boneMapping[bone.Name] = matchingIndices;
            }

            var offset = ToNumericsMatrix(bone.OffsetMatrix);
            var boneIndex = matchingIndices.FirstOrDefault(index => MatricesAreEqual(bones[index].Offset, offset), -1);

            if (boneIndex == -1) {
                boneIndex = bones.Count;
                var boneInfo = new BoneInfoData {
                    Name = bone.Name,
                    Index = boneIndex,
                    Offset = offset
                };
                bones.Add(boneInfo);
                matchingIndices.Add(boneIndex);

                if (!boneMapping.TryGetValue(boneInfo.Name, out var boneList)) {
                    boneList = [];
                    boneMapping[boneInfo.Name] = boneList;
                }

                boneList.Add(boneInfo.Index);
            }

            foreach (var weight in bone.VertexWeights) {
                boneData[weight.VertexID].AddBoneData(boneIndex, weight.Weight);
            }
        }

        var material = LoadMaterialDefault();
        var uploadedMesh = CreateUploadedMesh(vertices, normals, texCoords, indices);
        return new ModelMeshData(uploadedMesh, material, mesh.MaterialIndex, meshIndex, vertices, normals, animatedVertices, animatedNormals, texCoords, indices, boneData, mesh.Bones.Count > 0);
    }

    private static ModelNodeData ProcessNode(Node node) {

        var modelNode = new ModelNodeData {
            Name = node.Name,
            Transformation = ToNumericsMatrix(node.Transform),
            RigidTransformation = node.Transform
        };

        foreach (var meshIndex in node.MeshIndices) {
            modelNode.MeshIndices.Add(meshIndex);
        }

        foreach (var child in node.Children) {
            modelNode.Children.Add(ProcessNode(child));
        }

        return modelNode;
    }

    private static AnimationClipData ProcessAnimation(Assimp.Animation animation) {

        var clip = new AnimationClipData {
            Name = animation.Name,
            TicksPerSecond = animation.TicksPerSecond != 0 ? animation.TicksPerSecond : 25.0
        };

        var maxTime = 0d;

        foreach (var channel in animation.NodeAnimationChannels) {
            var animationChannel = new AnimationChannelData { NodeName = channel.NodeName };

            foreach (var key in channel.PositionKeys) {
                animationChannel.PositionKeys.Add((key.Time, ToNumericsVector(key.Value)));
                maxTime = Math.Max(maxTime, key.Time);
            }

            foreach (var key in channel.RotationKeys) {
                animationChannel.RotationKeys.Add((key.Time, ToNumericsQuaternion(key.Value)));
                maxTime = Math.Max(maxTime, key.Time);
            }

            foreach (var key in channel.ScalingKeys) {
                animationChannel.ScaleKeys.Add((key.Time, ToNumericsVector(key.Value)));
                maxTime = Math.Max(maxTime, key.Time);
            }

            clip.Channels.Add(animationChannel);
            clip.ChannelMap[animationChannel.NodeName] = animationChannel;
        }

        clip.Duration = maxTime > 0 ? maxTime : animation.DurationInTicks;
        return clip;
    }

    private static bool MatricesAreEqual(Matrix4x4 left, Matrix4x4 right) {

        const float epsilon = 0.0001f;

        return Math.Abs(left.M11 - right.M11) < epsilon
               && Math.Abs(left.M12 - right.M12) < epsilon
               && Math.Abs(left.M13 - right.M13) < epsilon
               && Math.Abs(left.M14 - right.M14) < epsilon
               && Math.Abs(left.M21 - right.M21) < epsilon
               && Math.Abs(left.M22 - right.M22) < epsilon
               && Math.Abs(left.M23 - right.M23) < epsilon
               && Math.Abs(left.M24 - right.M24) < epsilon
               && Math.Abs(left.M31 - right.M31) < epsilon
               && Math.Abs(left.M32 - right.M32) < epsilon
               && Math.Abs(left.M33 - right.M33) < epsilon
               && Math.Abs(left.M34 - right.M34) < epsilon
               && Math.Abs(left.M41 - right.M41) < epsilon
               && Math.Abs(left.M42 - right.M42) < epsilon
               && Math.Abs(left.M43 - right.M43) < epsilon
               && Math.Abs(left.M44 - right.M44) < epsilon;
    }

    private static unsafe Mesh CreateUploadedMesh(Vector3[] vertices, Vector3[] normals, Vector2[] texCoords, uint[] indices) {

        var mesh = new Mesh {
            VertexCount = vertices.Length,
            TriangleCount = indices.Length / 3,
            Vertices = (float*)MemAlloc((uint)(vertices.Length * 3 * sizeof(float))),
            Normals = (float*)MemAlloc((uint)(normals.Length * 3 * sizeof(float))),
            TexCoords = (float*)MemAlloc((uint)(texCoords.Length * 2 * sizeof(float))),
            Indices = (ushort*)MemAlloc((uint)(indices.Length * sizeof(ushort)))
        };

        fixed (Vector3* vertexPointer = vertices) {
            Buffer.MemoryCopy(vertexPointer, mesh.Vertices, (long)vertices.Length * 3 * sizeof(float), (long)vertices.Length * 3 * sizeof(float));
        }

        fixed (Vector3* normalPointer = normals) {
            Buffer.MemoryCopy(normalPointer, mesh.Normals, (long)normals.Length * 3 * sizeof(float), (long)normals.Length * 3 * sizeof(float));
        }

        fixed (Vector2* texCoordPointer = texCoords) {
            Buffer.MemoryCopy(texCoordPointer, mesh.TexCoords, (long)texCoords.Length * 2 * sizeof(float), (long)texCoords.Length * 2 * sizeof(float));
        }

        for (var i = 0; i < indices.Length; i++) {
            mesh.Indices[i] = (ushort)indices[i];
        }

        GenMeshTangents(ref mesh);
        UploadMesh(ref mesh, false);
        return mesh;
    }

    private void UpdateAnimationHierarchy(ModelNodeData node, AnimationClipData clip, double time, in Matrix4x4 parentTransform, in Matrix4x4 rigidDriverTransform, in Matrix4x4 globalInverse, Dictionary<string, List<BoneInfoData>> boneMap) {

        var nodeTransform = node.Transformation;

        if (clip.ChannelMap.TryGetValue(node.Name, out var channel)) {
            nodeTransform = GetInterpolatedTransform(channel, time, node.Transformation);
        }

        var globalTransform = nodeTransform * parentTransform;
        var rigidNodeTransform = node.RigidTransformation;

        if (clip.ChannelMap.TryGetValue(node.Name, out var rigidChannel)) {
            rigidNodeTransform = GetInterpolatedTransform(rigidChannel, time, node.RigidTransformation);
        }

        var rigidGlobalTransform = rigidNodeTransform * rigidDriverTransform;
        var nextRigidDriverTransform = AnimatedNodeNames.Contains(node.Name) ? rigidGlobalTransform : rigidDriverTransform;

        foreach (var meshIndex in node.MeshIndices) {
            if ((uint)meshIndex < (uint)CurrentMeshNodeTransforms.Length) {
                CurrentMeshNodeTransforms[meshIndex] = nextRigidDriverTransform;
            }
        }

        if (boneMap.TryGetValue(node.Name, out var bones)) {
            foreach (var bone in bones) {
                bone.FinalTransformation = bone.Offset * globalTransform * globalInverse;
            }
        }

        foreach (var child in node.Children) {
            UpdateAnimationHierarchy(child, clip, time, globalTransform, nextRigidDriverTransform, globalInverse, boneMap);
        }
    }

    private void ApplyBindPoseHierarchy(ModelNodeData node, in Matrix4x4 parentTransform, in Matrix4x4 rigidDriverTransform, in Matrix4x4 globalInverse, Dictionary<string, List<BoneInfoData>> boneMap) {

        var globalTransform = node.Transformation * parentTransform;
        var rigidGlobalTransform = node.RigidTransformation * rigidDriverTransform;
        var nextRigidDriverTransform = AnimatedNodeNames.Contains(node.Name) ? rigidGlobalTransform : rigidDriverTransform;

        foreach (var meshIndex in node.MeshIndices) {
            if ((uint)meshIndex < (uint)CurrentMeshNodeTransforms.Length) {
                CurrentMeshNodeTransforms[meshIndex] = nextRigidDriverTransform;
            }
        }

        if (boneMap.TryGetValue(node.Name, out var bones)) {
            foreach (var bone in bones) {
                bone.FinalTransformation = bone.Offset * globalTransform * globalInverse;
            }
        }

        foreach (var child in node.Children) {
            ApplyBindPoseHierarchy(child, globalTransform, nextRigidDriverTransform, globalInverse, boneMap);
        }
    }

    private static unsafe void SkinMesh(ModelMeshData mesh, List<BoneInfoData> bones) {

        if (!mesh.UsesSkinning) {
            return;
        }

        Parallel.For(0, mesh.Vertices.Length, i => {
            var boneData = mesh.BoneData[i];
            var totalWeight = boneData.Weight0 + boneData.Weight1 + boneData.Weight2 + boneData.Weight3;

            if (totalWeight < 0.001f) {
                mesh.AnimatedVertices[i] = mesh.Vertices[i];
                mesh.AnimatedNormals[i] = mesh.Normals[i];
                return;
            }

            var vertex = mesh.Vertices[i];
            var normal = mesh.Normals[i];
            var finalVertex = Vector3.Zero;
            var finalNormal = Vector3.Zero;

            AccumulateWeight(boneData.Bone0, boneData.Weight0);
            AccumulateWeight(boneData.Bone1, boneData.Weight1);
            AccumulateWeight(boneData.Bone2, boneData.Weight2);
            AccumulateWeight(boneData.Bone3, boneData.Weight3);

            mesh.AnimatedVertices[i] = finalVertex;
            mesh.AnimatedNormals[i] = Vector3.Normalize(finalNormal);

            void AccumulateWeight(int boneIndex, float weight) {

                if (weight <= 0) {
                    return;
                }

                var matrix = bones[boneIndex].FinalTransformation;
                finalVertex += Vector3.Transform(vertex, matrix) * weight;
                finalNormal += Vector3.TransformNormal(normal, matrix) * weight;
            }
        });

        fixed (Vector3* vertexPointer = mesh.AnimatedVertices) {
            Buffer.MemoryCopy(vertexPointer, mesh.Mesh.Vertices, (long)mesh.AnimatedVertices.Length * 3 * sizeof(float), (long)mesh.AnimatedVertices.Length * 3 * sizeof(float));
        }

        fixed (Vector3* normalPointer = mesh.AnimatedNormals) {
            Buffer.MemoryCopy(normalPointer, mesh.Mesh.Normals, (long)mesh.AnimatedNormals.Length * 3 * sizeof(float), (long)mesh.AnimatedNormals.Length * 3 * sizeof(float));
        }

        UpdateMeshBuffer(mesh.Mesh, 0, mesh.Mesh.Vertices, mesh.AnimatedVertices.Length * 3 * sizeof(float), 0);
        UpdateMeshBuffer(mesh.Mesh, 2, mesh.Mesh.Normals, mesh.AnimatedNormals.Length * 3 * sizeof(float), 0);
    }

    private void UpdateRigidMesh(ModelMeshData mesh) {

        if (mesh.MeshIndex < 0 ||
            mesh.MeshIndex >= BindMeshNodeTransforms.Length ||
            mesh.MeshIndex >= CurrentMeshNodeTransforms.Length ||
            !TryBuildRigidDeltaTransform(BindMeshNodeTransforms[mesh.MeshIndex], CurrentMeshNodeTransforms[mesh.MeshIndex], out var deltaTransform)) {

            return;
        }

        for (var i = 0; i < mesh.Vertices.Length; i++) {
            mesh.AnimatedVertices[i] = Vector3.Transform(mesh.Vertices[i], deltaTransform);
            mesh.AnimatedNormals[i] = Vector3.Normalize(Vector3.TransformNormal(mesh.Normals[i], deltaTransform));
        }

        UploadAnimatedMesh(mesh);
    }

    private static void ResetRigidMesh(ModelMeshData mesh) {

        Array.Copy(mesh.Vertices, mesh.AnimatedVertices, mesh.Vertices.Length);
        Array.Copy(mesh.Normals, mesh.AnimatedNormals, mesh.Normals.Length);
        UploadAnimatedMesh(mesh);
    }

    private static unsafe void UploadAnimatedMesh(ModelMeshData mesh) {

        fixed (Vector3* vertexPointer = mesh.AnimatedVertices) {
            Buffer.MemoryCopy(vertexPointer, mesh.Mesh.Vertices, (long)mesh.AnimatedVertices.Length * 3 * sizeof(float), (long)mesh.AnimatedVertices.Length * 3 * sizeof(float));
        }

        fixed (Vector3* normalPointer = mesh.AnimatedNormals) {
            Buffer.MemoryCopy(normalPointer, mesh.Mesh.Normals, (long)mesh.AnimatedNormals.Length * 3 * sizeof(float), (long)mesh.AnimatedNormals.Length * 3 * sizeof(float));
        }

        UpdateMeshBuffer(mesh.Mesh, 0, mesh.Mesh.Vertices, mesh.AnimatedVertices.Length * 3 * sizeof(float), 0);
        UpdateMeshBuffer(mesh.Mesh, 2, mesh.Mesh.Normals, mesh.AnimatedNormals.Length * 3 * sizeof(float), 0);
    }

    private static Matrix4x4 GetInterpolatedTransform(AnimationChannelData channel, double time, Matrix4x4 bindPose) {

        Matrix4x4.Decompose(bindPose, out var bindScale, out var bindRotation, out var bindPosition);
        var position = InterpolatePosition(channel.PositionKeys, time, bindPosition);
        var rotation = InterpolateRotation(channel.RotationKeys, time, bindRotation);
        var scale = InterpolateScale(channel.ScaleKeys, time, bindScale);

        return Matrix4x4.CreateScale(scale) * Matrix4x4.CreateFromQuaternion(rotation) * Matrix4x4.CreateTranslation(position);
    }

    private static Vector3 InterpolatePosition(List<(double Time, Vector3 Position)> keys, double time, Vector3 fallback) =>
        keys.Count switch {
            0 => fallback,
            1 => keys[0].Position,
            _ => Vector3.Lerp(keys[FindKeyIndex(keys, time)].Position, keys[FindNextKeyIndex(keys, time)].Position, GetBlendFactor(keys, time))
        };

    private static Quaternion InterpolateRotation(List<(double Time, Quaternion Rotation)> keys, double time, Quaternion fallback) =>
        keys.Count switch {
            0 => fallback,
            1 => keys[0].Rotation,
            _ => Quaternion.Slerp(keys[FindKeyIndex(keys, time)].Rotation, keys[FindNextKeyIndex(keys, time)].Rotation, GetBlendFactor(keys, time))
        };

    private static Vector3 InterpolateScale(List<(double Time, Vector3 Scale)> keys, double time, Vector3 fallback) =>
        keys.Count switch {
            0 => fallback,
            1 => keys[0].Scale,
            _ => Vector3.Lerp(keys[FindKeyIndex(keys, time)].Scale, keys[FindNextKeyIndex(keys, time)].Scale, GetBlendFactor(keys, time))
        };

    private static int FindKeyIndex<T>(List<(double Time, T Value)> keys, double time) {

        for (var i = 0; i < keys.Count - 1; i++) {
            if (time < keys[i + 1].Time) {
                return i;
            }
        }

        return keys.Count - 1;
    }

    private static int FindNextKeyIndex<T>(List<(double Time, T Value)> keys, double time) {

        return (FindKeyIndex(keys, time) + 1) % keys.Count;
    }

    private static float GetBlendFactor<T>(List<(double Time, T Value)> keys, double time) {

        var current = keys[FindKeyIndex(keys, time)];
        var next = keys[FindNextKeyIndex(keys, time)];
        var length = next.Time - current.Time;
        if (next.Time <= current.Time) {
            return 0f;
        }

        return Math.Clamp((float)((time - current.Time) / length), 0f, 1f);
    }

    private static Matrix4x4 CreateTransformMatrix(Vector3 position, Vector3 rotationDegrees, Vector3 scale) {

        var positionMatrix = Raymath.MatrixTranslate(position.X, position.Y, position.Z);
        var rotationMatrix = Raymath.QuaternionToMatrix(Raymath.QuaternionFromEuler(rotationDegrees.Z * DEG2RAD, rotationDegrees.Y * DEG2RAD, rotationDegrees.X * DEG2RAD));
        var scaleMatrix = Raymath.MatrixScale(scale.X, scale.Y, scale.Z);

        return Raymath.MatrixMultiply(Raymath.MatrixMultiply(scaleMatrix, rotationMatrix), positionMatrix);
    }

    private static bool TryBuildRigidDeltaTransform(Matrix4x4 bindTransform, Matrix4x4 currentTransform, out Matrix4x4 deltaTransform) {

        if (!Matrix4x4.Decompose(bindTransform, out _, out var bindRotation, out var bindTranslation)) {
            deltaTransform = Matrix4x4.Identity;
            return false;
        }

        if (!Matrix4x4.Decompose(currentTransform, out _, out var currentRotation, out var currentTranslation)) {
            deltaTransform = Matrix4x4.Identity;
            return false;
        }

        var bindRigid = Matrix4x4.CreateFromQuaternion(bindRotation) * Matrix4x4.CreateTranslation(bindTranslation);
        var currentRigid = Matrix4x4.CreateFromQuaternion(currentRotation) * Matrix4x4.CreateTranslation(currentTranslation);

        if (!Matrix4x4.Invert(bindRigid, out var inverseBindRigid)) {
            deltaTransform = Matrix4x4.Identity;
            return false;
        }

        deltaTransform = inverseBindRigid * currentRigid;
        return true;
    }

    private static Matrix4x4 ToNumericsMatrix(Matrix4x4 matrix) => Matrix4x4.Transpose(matrix);
    private static Vector3 ToNumericsVector(Vector3 vector) => vector;
    private static Quaternion ToNumericsQuaternion(Quaternion quaternion) => quaternion;

    private static string BuildEmbeddedTextureName(string? filename, int index) {

        var name = Path.GetFileNameWithoutExtension(filename);
        return string.IsNullOrWhiteSpace(name) ? $"EmbeddedTexture_{index}" : name;
    }

    private static string NormalizeTextureExtension(string? formatHint, string? filename) {

        var extension = Path.GetExtension(filename);
        if (!string.IsNullOrWhiteSpace(extension)) {
            return extension.StartsWith('.') ? extension : "." + extension;
        }

        if (!string.IsNullOrWhiteSpace(formatHint)) {
            return "." + formatHint.Trim().TrimStart('.').ToLowerInvariant();
        }

        return ".png";
    }

    private static void RegisterTextureKey(Dictionary<string, TextureData> lookup, string? key, TextureData texture) {

        if (string.IsNullOrWhiteSpace(key)) {
            return;
        }

        lookup[key] = texture;
    }
}

internal sealed class ModelMeshData(
    Mesh mesh,
    Material material,
    int materialIndex,
    int meshIndex,
    Vector3[] vertices,
    Vector3[] normals,
    Vector3[] animatedVertices,
    Vector3[] animatedNormals,
    Vector2[] texCoords,
    uint[] indices,
    VertexBoneData[] boneData,
    bool usesSkinning) {

    public Mesh Mesh = mesh;
    public Material FallbackMaterial = material;
    public int MaterialIndex = materialIndex;
    public int MeshIndex = meshIndex;
    public Vector3[] Vertices = vertices;
    public Vector3[] Normals = normals;
    public Vector3[] AnimatedVertices = animatedVertices;
    public Vector3[] AnimatedNormals = animatedNormals;
    public Vector2[] TexCoords = texCoords;
    public uint[] Indices = indices;
    public VertexBoneData[] BoneData = boneData;
    public bool UsesSkinning = usesSkinning;

    public void Unload() {

        UnloadMaterial(FallbackMaterial);
        UnloadMesh(Mesh);
    }
}

internal struct VertexBoneData {

    public int Bone0;
    public int Bone1;
    public int Bone2;
    public int Bone3;
    public float Weight0;
    public float Weight1;
    public float Weight2;
    public float Weight3;

    public void AddBoneData(int id, float weight) {

        if (weight <= 0) {
            return;
        }

        if (Weight0 <= 0) {
            Bone0 = id;
            Weight0 = weight;
        } else if (Weight1 <= 0) {
            Bone1 = id;
            Weight1 = weight;
        } else if (Weight2 <= 0) {
            Bone2 = id;
            Weight2 = weight;
        } else if (Weight3 <= 0) {
            Bone3 = id;
            Weight3 = weight;
        }
    }
}

internal sealed class BoneInfoData {

    public string Name = "";
    public int Index;
    public Matrix4x4 Offset;
    public Matrix4x4 FinalTransformation;
}

internal sealed class ModelNodeData {

    public string Name = "";
    public Matrix4x4 Transformation = Matrix4x4.Identity;
    public Matrix4x4 RigidTransformation = Matrix4x4.Identity;
    public readonly List<int> MeshIndices = [];
    public readonly List<ModelNodeData> Children = [];
}
