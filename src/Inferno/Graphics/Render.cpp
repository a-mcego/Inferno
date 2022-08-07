#include "pch.h"
#include "Render.h"
#include "imgui_local.h"
#include "Level.h"
#include "Editor/Editor.h"
#include "Buffers.h"
#include "Utility.h"
#include "Graphics/LevelMesh.h"
#include "Mesh.h"
#include "Render.Gizmo.h"
#include "Render.Debug.h"
#include "Render.Editor.h"
#include "Settings.h"
#include "DirectX.h"
#include "Physics.h"
#include "SoundSystem.h"
#include "Render.Particles.h"
#include "Game.Segment.h"
#include "Game.Text.h"
#include "Editor/UI/BriefingEditor.h"

using namespace DirectX;
using namespace Inferno::Graphics;

namespace Inferno::Render {
    Color ClearColor = { 0.1f, 0.1f, 0.1f, 1.0f };
    BoundingFrustum CameraFrustum;
    bool LevelChanged = false;

    //const string TEST_MODEL = "robottesttube(orbot).OOF"; // mixed transparency test
    const string TEST_MODEL = "gyro.OOF";

    // Dynamic render batches
    // Usage: Batch vertices / indices then use returned structs to render later
    template<class TVertex, class TIndex = unsigned short>
    class RenderBatch {
        ID3D12Device* _device;

        ComPtr<ID3D12Resource> _indexBuffer;
        ComPtr<ID3D12Resource> _vertexBuffer;

        //Buffer<TVertex> _vertexBuffer;

        int _vertexOffset = 0, _indexOffset = 0;
        int _indexBufferSize;
        int _vertexBufferSize;
        bool _inBatch = false;
        void* _pVertexBuffer;
        void* _pIndexBuffer;
        //int _requestedIndexBufferSize = _indexBufferSize;
        //int _requestedVertexBufferSize = _vertexBufferSize;

    public:
        RenderBatch(ID3D12Device* device, uint vertexCapacity = 5000, uint indexCapacity = 10000)
            : _device(device), _indexBufferSize(indexCapacity), _vertexBufferSize(vertexCapacity) {
            //CreateUploadBuffer(_vertexBuffer, vertexCapacity * sizeof(TVertex));
            //CreateUploadBuffer(_indexBuffer, indexCapacity * sizeof(TIndex));
        }

        void Begin() {
            if (_inBatch) throw Exception("Cannot start batch if already began");

            _inBatch = true;
            _vertexOffset = 0;
            _indexOffset = 0;

            ThrowIfFailed(_vertexBuffer->Map(0, &CPU_READ_NONE, &_pVertexBuffer));
            ThrowIfFailed(_indexBuffer->Map(0, &CPU_READ_NONE, &_pIndexBuffer));
        }

        //RenderBatchHandle Batch(List<TVertex>& vertices, List<TIndex>& index) {
        //    if (_indexOffset + index.size() > _indexBufferSize) {
        //        SPDLOG_WARN("Batch capacity reached");
        //        return {};
        //        // grow buffers if too small, but can't in the middle of a frame...
        //        // request to grow on next frame? warning
        //    }

        //    std::memcpy(_pVertexBuffer + _vertexOffset * sizeof(TVertex), vertices.data(), vertices.size() * sizeof(TVertex));
        //    std::memcpy(_pIndexBuffer + _indexOffset * sizeof(TIndex), index.data(), index.size() * sizeof(TIndex));
        //    //std::copy(vertices.begin(), vertices.end(), _pVertexBuffer + _vertexOffset * sizeof(TVertex));

        //    RenderBatchHandle handle = { _indexOffset, _vertexOffset, vertices.size() };
        //    _vertexOffset += vertices.size();
        //    _indexOffset += index.size();
        //    return handle;
        //}

        void End() {
            _inBatch = false;

            _vertexBuffer->Unmap(0, &CPU_READ_NONE);
            _indexBuffer->Unmap(0, &CPU_READ_NONE);
        }
    };


}

namespace Inferno::Render {
    enum class RenderPass {
        Opaque, // Solid level geometry or objects
        Walls, // Level walls, might be transparent
        Transparent // Sprites, transparent portions of models
    };

    using VertexType = DirectX::VertexPositionTexture;

    namespace {
        HWND _hwnd;

        // todo: put all of these resources into a class and use RAII
        Ptr<GraphicsMemory> _graphicsMemory;

        Ptr<MeshBuffer> _meshBuffer;
        Ptr<SpriteBatch> _tempBatch;
        void* ActiveEffect = nullptr; // address of the currently active effect
    }

    struct RenderBatchHandle {
        int IndexOffset;
        int BufferOffset;
        int Size;
    };


    // Applies an effect that uses the frame constants
    template<class T>
    void ApplyEffect(GraphicsContext& ctx, const Effect<T>& effect) {
        if (ActiveEffect == &effect) return;
        ActiveEffect = (void*)&effect;
        ctx.ApplyEffect(effect);
        ctx.SetConstantBuffer(0, Adapter->FrameConstantsBuffer.GetGPUVirtualAddress());
    }

    LevelMeshBuilder _levelMeshBuilder;
    Ptr<PackedBuffer> _levelMeshBuffer;

    List<RenderCommand> _opaqueQueue;
    List<RenderCommand> _transparentQueue;

    void DrawModel(GraphicsContext& ctx, const Object& object, ModelID modelId, float alpha, TexID texOverride = TexID::None) {
        auto& effect = Effects->Object;
        ApplyEffect(ctx, effect);
        auto cmdList = ctx.CommandList();
        ctx.SetConstantBuffer(0, Adapter->FrameConstantsBuffer.GetGPUVirtualAddress());

        auto& model = Resources::GetModel(modelId);
        if (model.DataSize == 0) {
            DrawObjectOutline(object);
            return;
        }

        auto& meshHandle = _meshBuffer->GetHandle(modelId);

        effect.Shader->SetSampler(cmdList, GetTextureSampler());
        auto& seg = Game::Level.GetSegment(object.Segment);
        ObjectShader::Constants constants = {};
        constants.Ambient = Settings::RenderMode == RenderMode::Shaded ? seg.VolumeLight : Color(1, 1, 1);
        constants.EmissiveLight = object.Render.Emissive;

        //Matrix transform = object.GetTransform(t);
        Matrix transform = Matrix::Lerp(object.GetLastTransform(), object.GetTransform(), alpha);
        transform.Forward(-transform.Forward()); // flip z axis to correct for LH models

        if (object.Control.Type == ControlType::Weapon) {
            auto r = Matrix::CreateFromYawPitchRoll(object.Movement.Physics.AngularVelocity * (float)ElapsedTime * 6.28f);
            auto translation = transform.Translation();
            transform *= Matrix::CreateTranslation(translation);
            transform = r * transform;
            transform *= Matrix::CreateTranslation(-translation);
        }

        int submodelIndex = 0;
        for (auto& submodel : model.Submodels) {
            // accumulate the offsets for each submodel
            auto submodelOffset = Vector3::Zero;
            auto* smc = &submodel;
            while (smc->Parent != ROOT_SUBMODEL) {
                submodelOffset += smc->Offset;
                smc = &model.Submodels[smc->Parent];
            }

            auto world = Matrix::CreateTranslation(submodelOffset) * transform;
            constants.World = world;
            effect.Shader->SetConstants(cmdList, constants);

            // get the mesh associated with the submodel
            auto& subMesh = meshHandle.Meshes[submodelIndex++];

            for (int i = 0; i < subMesh.size(); i++) {
                auto mesh = subMesh[i];
                if (!mesh) continue;

                TexID tid = texOverride;
                if (texOverride == TexID::None)
                    tid = mesh->EffectClip == EClipID::None ? mesh->Texture : Resources::GetEffectClip(mesh->EffectClip).VClip.GetFrame(ElapsedTime);

                const Material2D& material = tid == TexID::None ? Materials->White : Materials->Get(tid);
                effect.Shader->SetMaterial(cmdList, material);

                cmdList->IASetVertexBuffers(0, 1, &mesh->VertexBuffer);
                cmdList->IASetIndexBuffer(&mesh->IndexBuffer);
                cmdList->DrawIndexedInstanced(mesh->IndexCount, 1, 0, 0, 0);
                DrawCalls++;
            }
        }
    }


    // Draws a square glow that always faces the camera (Descent 3 submodels);
    void DrawObjectGlow(ID3D12GraphicsCommandList* cmd, float radius, const Color& color) {
        if (radius <= 0) return;
        const auto r = radius;
        ObjectVertex v0({ -r, r, 0 }, { 0, 0 }, color);
        ObjectVertex v1({ r, r, 0 }, { 1, 0 }, color);
        ObjectVertex v2({ r, -r, 0 }, { 1, 1 }, color);
        ObjectVertex v3({ -r, -r, 0 }, { 0, 1 }, color);

        // Horrible immediate mode nonsense
        DrawCalls++;
        g_SpriteBatch->Begin(cmd);
        g_SpriteBatch->DrawQuad(v0, v1, v2, v3);
        g_SpriteBatch->End();
    }

    void DrawOutrageModel(const Object& object, ID3D12GraphicsCommandList* cmd, int index, bool transparentPass) {
        auto& meshHandle = _meshBuffer->GetOutrageHandle(index);

        ObjectShader::Constants constants = {};
        auto& seg = Game::Level.GetSegment(object.Segment);
        constants.EmissiveLight = object.Render.Emissive;
        constants.Ambient = Settings::RenderMode == RenderMode::Shaded ? seg.VolumeLight : Color(1, 1, 1);

        Matrix transform = object.GetTransform();
        transform.Forward(-transform.Forward()); // flip z axis to correct for LH models

        auto model = Resources::GetOutrageModel(TEST_MODEL);
        if (model == nullptr) return;

        for (int submodelIndex = 0; submodelIndex < model->Submodels.size(); submodelIndex++) {
            auto& submodel = model->Submodels[submodelIndex];
            auto& submesh = meshHandle.Meshes[submodelIndex];

            // accumulate the offsets for each submodel
            auto submodelOffset = Vector3::Zero;
            auto* smc = &submodel;
            while (smc->Parent != -1) {
                submodelOffset += smc->Offset;
                smc = &model->Submodels[smc->Parent];
            }

            auto world = Matrix::CreateTranslation(submodelOffset) * transform;

            using namespace Outrage;

            if (submodel.HasFlag(SubmodelFlag::Facing)) {
                auto smPos = Vector3::Transform(Vector3::Zero, world);
                auto billboard = Matrix::CreateBillboard(smPos, Camera.Position, Camera.Up);
                constants.World = world;
                //constants.Projection = billboard * ViewProjection;
            }
            else {
                if (submodel.HasFlag(SubmodelFlag::Rotate))
                    world = Matrix::CreateFromAxisAngle(submodel.Keyframes[1].Axis, XM_2PI * submodel.Rotation * (float)Render::ElapsedTime) * world;

                constants.World = world;
                //constants.Projection = world * ViewProjection;
            }

            //constants.Time = (float)ElapsedTime;

            // get the mesh associated with the submodel
            for (auto& [i, mesh] : submesh) {

                auto& material = Render::NewTextureCache->GetTextureInfo(model->TextureHandles[i]);
                bool transparent = material.Saturate() || material.Alpha();

                if ((transparentPass && !transparent) || (!transparentPass && transparent))
                    continue; // skip saturate textures unless on glow pass

                auto handle = i >= 0 ?
                    Render::NewTextureCache->GetResource(model->TextureHandles[i], (float)ElapsedTime) :
                    Materials->White.Handles[0];

                bool additive = material.Saturate() || submodel.HasFlag(SubmodelFlag::Facing);

                auto& effect = additive ? Effects->ObjectGlow : Effects->Object;
                effect.Apply(cmd);
                effect.Shader->SetSampler(cmd, GetTextureSampler());
                effect.Shader->SetMaterial(cmd, handle);

                if (transparentPass && submodel.HasFlag(SubmodelFlag::Facing)) {
                    if (material.Saturate())
                        constants.Ambient = Color(1, 1, 1);
                    //constants.Colors[1] = Color(1, 1, 1, 1);
                    effect.Shader->SetConstants(cmd, constants);
                    DrawObjectGlow(cmd, submodel.Radius, Color(1, 1, 1, 1));
                }
                else {
                    //constants.Colors[1] = material.Color; // color 1 is used for texture alpha
                    effect.Shader->SetConstants(cmd, constants);
                    cmd->IASetVertexBuffers(0, 1, &mesh->VertexBuffer);
                    cmd->IASetIndexBuffer(&mesh->IndexBuffer);
                    cmd->DrawIndexedInstanced(mesh->IndexCount, 1, 0, 0, 0);
                    DrawCalls++;
                }
            }
        }
    }

    void DrawVClip(GraphicsContext& ctx,
                   const VClip& vclip,
                   const Vector3& position,
                   float radius,
                   const Color& color,
                   float elapsed,
                   bool additive,
                   float rotation,
                   const Vector3* up) {
        auto frame = vclip.NumFrames - (int)std::floor(elapsed / vclip.FrameTime) % vclip.NumFrames - 1;
        auto tid = vclip.Frames[frame];

        auto transform = up ?
            Matrix::CreateConstrainedBillboard(position, Camera.Position, *up) :
            Matrix::CreateBillboard(position, Camera.Position, Camera.Up);

        if (rotation != 0)
            transform = Matrix::CreateRotationZ(rotation) * transform;

        // create quad and transform it
        auto& ti = Resources::GetTextureInfo(tid);
        auto ratio = (float)ti.Height / (float)ti.Width;
        auto h = radius * ratio;
        auto w = radius;
        auto p0 = Vector3::Transform({ -w, h, 0 }, transform); // bl
        auto p1 = Vector3::Transform({ w, h, 0 }, transform); // br
        auto p2 = Vector3::Transform({ w, -h, 0 }, transform); // tr
        auto p3 = Vector3::Transform({ -w, -h, 0 }, transform); // tl

        ObjectVertex v0(p0, { 0, 0 }, color);
        ObjectVertex v1(p1, { 1, 0 }, color);
        ObjectVertex v2(p2, { 1, 1 }, color);
        ObjectVertex v3(p3, { 0, 1 }, color);

        auto& effect = additive ? Effects->SpriteAdditive : Effects->Sprite;
        ApplyEffect(ctx, effect);
        auto& material = Materials->Get(tid);
        effect.Shader->SetDiffuse(ctx.CommandList(), material.Handles[0]);
        effect.Shader->SetDepthTexture(ctx.CommandList(), Adapter->LinearizedDepthBuffer.GetSRV());
        auto sampler = Render::GetClampedTextureSampler();
        effect.Shader->SetSampler(ctx.CommandList(), sampler);

        DrawCalls++;
        g_SpriteBatch->Begin(ctx.CommandList());
        g_SpriteBatch->DrawQuad(v0, v1, v2, v3);
        g_SpriteBatch->End();
    }

    // When up is provided, it constrains the sprite to that axis
    void DrawSprite(GraphicsContext& ctx, const Object& object, bool additive, const Vector3* up = nullptr, bool lit = false) {
        auto& vclip = Resources::GetVideoClip(object.Render.VClip.ID);
        if (vclip.NumFrames == 0) {
            DrawObjectOutline(object);
            return;
        }

        Color color = lit ? Game::Level.GetSegment(object.Segment).VolumeLight : Color(1, 1, 1);
        DrawVClip(ctx, vclip, object.Position, object.Radius, color, (float)ElapsedTime, additive, object.Render.VClip.Rotation, up);
    }

    void DrawLevelMesh(GraphicsContext& ctx, const Inferno::LevelMesh& mesh) {
        if (!mesh.Chunk) return;
        auto& chunk = *mesh.Chunk;

        LevelShader::InstanceConstants constants{};
        constants.LightingScale = Settings::RenderMode == RenderMode::Shaded ? 1.0f : 0.0f; // How much light to apply

        auto cmdList = ctx.CommandList();
        Shaders->Level.SetDepthTexture(cmdList, Adapter->LinearizedDepthBuffer.GetSRV());

        if (chunk.Cloaked) {
            Shaders->Level.SetMaterial1(cmdList, Materials->Black);
            Shaders->Level.SetMaterial2(cmdList, Materials->Black);
            constants.LightingScale = 1;
        }
        else {
            {
                auto& map1 = chunk.EffectClip1 == EClipID::None ?
                    Materials->Get(chunk.TMap1) :
                    Materials->Get(Resources::GetEffectClip(chunk.EffectClip1).VClip.GetFrame(ElapsedTime));

                Shaders->Level.SetMaterial1(cmdList, map1);
            }

            if (chunk.TMap2 > LevelTexID::Unset) {
                constants.Overlay = true;

                auto& map2 = chunk.EffectClip2 == EClipID::None ?
                    Materials->Get(chunk.TMap2) :
                    Materials->Get(Resources::GetEffectClip(chunk.EffectClip2).VClip.GetFrame(ElapsedTime));

                Shaders->Level.SetMaterial2(cmdList, map2);
            }
        }

        auto& ti = Resources::GetLevelTextureInfo(chunk.TMap1);
        constants.Scroll = ti.Slide;
        constants.Scroll2 = chunk.OverlaySlide;
        constants.Distort = ti.Slide != Vector2::Zero;

        Shaders->Level.SetInstanceConstants(cmdList, constants);
        mesh.Draw(cmdList);
        DrawCalls++;
    }


    // Initialize device dependent objects here (independent of window size).
    void CreateDeviceDependentResources() {
        Shaders = MakePtr<ShaderResources>();
        Effects = MakePtr<EffectResources>(Shaders.get());
        Materials = MakePtr<MaterialLibrary>(3000);
        g_SpriteBatch = MakePtr<PrimitiveBatch<ObjectVertex>>(Device);
        Canvas = MakePtr<Canvas2D>(Device);
        BriefingCanvas = MakePtr<Canvas2D>(Device);
        _graphicsMemory = MakePtr<GraphicsMemory>(Device);
        Bloom = MakePtr<PostFx::Bloom>();
        NewTextureCache = MakePtr<TextureCache>();

        Debug::Initialize();

        ImGuiBatch::Initialize(_hwnd, (float)Settings::FontSize);
        static_assert(sizeof(ImTextureID) >= sizeof(D3D12_CPU_DESCRIPTOR_HANDLE), "D3D12_CPU_DESCRIPTOR_HANDLE is too large to fit in an ImTextureID");
        g_ImGuiBatch = MakePtr<ImGuiBatch>(Adapter->GetBackBufferCount());

        CreateEditorResources();
        LoadFonts();

        ResourceUploadBatch resourceUpload(Device);

        resourceUpload.Begin();

        {
            RenderTargetState rtState(Adapter->GetBackBufferFormat(), Adapter->SceneDepthBuffer.GetFormat());
            SpriteBatchPipelineStateDescription pd(rtState);
            _tempBatch = MakePtr<SpriteBatch>(Device, resourceUpload, pd);
        }

        auto task = resourceUpload.End(Adapter->GetCommandQueue());
        task.wait();
    }

    void CreateWindowSizeDependentResources(int width, int height) {
        Bloom->Create(width, height);
    }

    void Initialize(HWND hwnd, int width, int height) {
        assert(hwnd);
        _hwnd = hwnd;
        Adapter = MakePtr<DeviceResources>(BackBufferFormat);
        StaticTextures = MakePtr<StaticTextureDef>();
        Adapter->SetWindow(hwnd, width, height);
        Adapter->CreateDeviceResources();
        Render::Heaps = MakePtr<DescriptorHeaps>(20000, 200, 10);
        Adapter->CreateWindowSizeDependentResources();
        CreateDeviceDependentResources();
        Adapter->ReloadResources();

        CreateWindowSizeDependentResources(width, height);
        Camera.SetViewport((float)width, (float)height);
        _levelMeshBuffer = MakePtr<PackedBuffer>(1024 * 1024 * 10);

        Editor::Events::LevelChanged += [] { LevelChanged = true; };
        Editor::Events::TexturesChanged += [] {
            //PendingTextures.push_back(id);
            Materials->LoadLevelTextures(Game::Level, false);
        };
    }

    void Shutdown() {
        if (Adapter)
            Adapter->WaitForGpu();

        Materials->Shutdown(); // wait for thread to terminate
        Materials.reset();
        NewTextureCache.reset();
        Render::Heaps.reset();
        StaticTextures.reset();
        Effects.reset();
        Shaders.reset();
        Canvas.reset();
        BriefingCanvas.reset();
        _graphicsMemory.reset();
        g_SpriteBatch.reset();
        g_ImGuiBatch.reset();

        ReleaseEditorResources();
        _levelMeshBuffer.reset();
        _meshBuffer.reset();

        Adapter.reset();
        Bloom.reset();
        _tempBatch.reset();
        Debug::Shutdown();
        Device = nullptr;
        ReportLiveObjects();
    }

    void Resize(int width, int height) {
        //SPDLOG_INFO("Resize: {} {}", width, height);

        if (!Adapter->WindowSizeChanged(width, height))
            return;

        CreateWindowSizeDependentResources(width, height);
        Camera.SetViewport((float)width, (float)height);
    }

    // Loads a single model at runtime
    void LoadModelDynamic(ModelID id) {
        if (!_meshBuffer) return;
        //SPDLOG_INFO("LoadModelDynamic: {}", id);
        _meshBuffer->LoadModel(id);
        auto ids = GetTexturesForModel(id);
        Materials->LoadMaterials(ids, false);
    }

    void LoadTextureDynamic(LevelTexID id) {
        List<TexID> list = { Resources::LookupLevelTexID(id) };
        if (auto eclip = Resources::TryGetEffectClip(id))
            Seq::append(eclip->VClip.GetFrames(), list);
        Materials->LoadMaterials(list, false);
    }

    void LoadTextureDynamic(VClipID id) {
        auto& vclip = Resources::GetVideoClip(id);
        Materials->LoadMaterials(vclip.GetFrames(), false);
    }

    void LoadLevel(Level& level) {
        Adapter->WaitForGpu();

        SPDLOG_INFO("Load models");
        // Load models for objects in the level
        _meshBuffer = MakePtr<MeshBuffer>(Resources::GameData.Models.size());

        List<ModelID> modelIds;
        for (auto& obj : level.Objects)
            if (obj.Render.Type == RenderType::Model)
                _meshBuffer->LoadModel(obj.Render.Model.ID);

        {
            if (auto model = Resources::GetOutrageModel(TEST_MODEL)) {
                _meshBuffer->LoadOutrageModel(*model, 0);
                Materials->LoadOutrageModel(*model);
            }

            NewTextureCache->MakeResident();
        }

        _levelMeshBuilder.Update(level, *_levelMeshBuffer);
    }

    void DrawObject(GraphicsContext& ctx, const Object& object, float lerp, RenderPass pass) {
        switch (object.Type) {
            case ObjectType::Robot:
            {
                // could be transparent or opaque pass
                auto& info = Resources::GetRobotInfo(object.ID);
                auto texOverride = Resources::LookupLevelTexID(object.Render.Model.TextureOverride);
                DrawModel(ctx, object, info.Model, lerp, texOverride);
                break;
            }

            case ObjectType::Hostage:
            {
                if (pass != RenderPass::Transparent) return;
                auto up = object.Rotation.Up();
                DrawSprite(ctx, object, false, &up, Settings::RenderMode == RenderMode::Shaded);
                break;
            }

            case ObjectType::Coop:
            case ObjectType::Player:
            case ObjectType::Reactor:
            case ObjectType::SecretExitReturn:
            case ObjectType::Marker:
            {
                if (pass != RenderPass::Opaque) return;
                auto texOverride = Resources::LookupLevelTexID(object.Render.Model.TextureOverride);
                DrawModel(ctx, object, object.Render.Model.ID, lerp, texOverride);
                break;
            }

            case ObjectType::Weapon:
                if (object.Render.Type == RenderType::Model) {
                    auto texOverride = Resources::LookupLevelTexID(object.Render.Model.TextureOverride);
                    DrawModel(ctx, object, object.Render.Model.ID, lerp, texOverride);
                }
                else {
                    if (pass != RenderPass::Transparent) return;
                    DrawSprite(ctx, object, true);
                }
                break;

            case ObjectType::Fireball:
            {
                if (pass != RenderPass::Transparent) return;
                if (object.Render.VClip.ID == VClips::Matcen) {
                    auto up = object.Rotation.Up();
                    DrawSprite(ctx, object, true, &up);
                }
                else {
                    DrawSprite(ctx, object, true);
                }
                break;
            }

            case ObjectType::Powerup:
            {
                if (pass != RenderPass::Transparent) return;
                DrawSprite(ctx, object, false);
                break;
            }

            case ObjectType::Debris:
                break;
            case ObjectType::Clutter:
                break;
            default:
                break;
        }
    }

    IEffect* _activeEffect;

    // todo: skip transparent submodels (D2 energy guy, D3 facing submodels)
    void ModelDepthPrepass(ID3D12GraphicsCommandList* cmdList, Object& object, ModelID modelId, float lerp) {
        auto& model = Resources::GetModel(modelId);
        auto& meshHandle = _meshBuffer->GetHandle(modelId);

        ObjectDepthShader::Constants constants = {};
        Matrix transform = Matrix::Lerp(object.GetLastTransform(), object.GetTransform(), lerp);
        transform.Forward(-transform.Forward()); // flip z axis to correct for LH models

        auto& shader = Shaders->DepthObject;

        if (object.Control.Type == ControlType::Weapon) {
            auto r = Matrix::CreateFromYawPitchRoll(object.Movement.Physics.AngularVelocity * (float)ElapsedTime * 6.28f);
            auto translation = transform.Translation();
            transform *= Matrix::CreateTranslation(translation);
            transform = r * transform;
            transform *= Matrix::CreateTranslation(-translation);
        }

        int submodelIndex = 0;
        for (auto& submodel : model.Submodels) {
            // accumulate the offsets for each submodel
            auto submodelOffset = Vector3::Zero;
            auto* smc = &submodel;
            while (smc->Parent != ROOT_SUBMODEL) {
                submodelOffset += smc->Offset;
                smc = &model.Submodels[smc->Parent];
            }

            auto world = Matrix::CreateTranslation(submodelOffset) * transform;
            constants.World = world;
            shader.SetConstants(cmdList, constants);

            // get the mesh associated with the submodel
            auto& subMesh = meshHandle.Meshes[submodelIndex++];

            for (int i = 0; i < subMesh.size(); i++) {
                auto mesh = subMesh[i];
                if (!mesh) continue;

                cmdList->IASetVertexBuffers(0, 1, &mesh->VertexBuffer);
                cmdList->IASetIndexBuffer(&mesh->IndexBuffer);
                cmdList->DrawIndexedInstanced(mesh->IndexCount, 1, 0, 0, 0);
                DrawCalls++;
            }
        }
    }

    void LevelDepthCutout(ID3D12GraphicsCommandList* cmdList, const RenderCommand& cmd) {
        assert(cmd.Type == RenderCommandType::LevelMesh);
        auto& mesh = *cmd.Data.LevelMesh;
        if (!mesh.Chunk) return;
        auto& chunk = *mesh.Chunk;
        if (chunk.Blend == BlendMode::Additive) return;

        DepthCutoutShader::Constants consts{};
        consts.Threshold = 0.01f;

        auto& effect = Effects->DepthCutout;
        effect.Apply(cmdList);
        effect.Shader->SetSampler(cmdList, GetTextureSampler());

        {
            auto& map1 = chunk.EffectClip1 == EClipID::None ?
                Materials->Get(chunk.TMap1) :
                Materials->Get(Resources::GetEffectClip(chunk.EffectClip1).VClip.GetFrame(ElapsedTime));

            effect.Shader->SetMaterial1(cmdList, map1);
        }

        if (chunk.TMap2 > LevelTexID::Unset) {
            consts.HasOverlay = true;

            auto& map2 = chunk.EffectClip2 == EClipID::None ?
                Materials->Get(chunk.TMap2) :
                Materials->Get(Resources::GetEffectClip(chunk.EffectClip2).VClip.GetFrame(ElapsedTime));

            effect.Shader->SetMaterial2(cmdList, map2);
        }

        auto& ti = Resources::GetLevelTextureInfo(chunk.TMap1);
        consts.Scroll = ti.Slide;
        consts.Scroll2 = chunk.OverlaySlide;
        effect.Shader->SetConstants(cmdList, consts);

        mesh.Draw(cmdList);
        DrawCalls++;
    }

    void ExecuteRenderCommand(GraphicsContext& ctx, const RenderCommand& cmd, float lerp, RenderPass pass) {
        switch (cmd.Type) {
            case RenderCommandType::LevelMesh:
            {
                auto& mesh = *cmd.Data.LevelMesh;

                if (Settings::RenderMode == RenderMode::Flat) {
                    if (mesh.Chunk->Blend == BlendMode::Alpha || mesh.Chunk->Blend == BlendMode::Additive) {
                        if (pass != RenderPass::Walls) return;
                        ApplyEffect(ctx, Effects->LevelWallFlat);
                    }
                    else {
                        if (pass != RenderPass::Opaque) return;
                        ApplyEffect(ctx, Effects->LevelFlat);
                    }

                    cmd.Data.LevelMesh->Draw(ctx.CommandList());
                    DrawCalls++;
                }
                else {
                    if (mesh.Chunk->Blend == BlendMode::Alpha) {
                        if (pass != RenderPass::Walls) return;
                        ApplyEffect(ctx, Effects->LevelWall);
                    }
                    else if (mesh.Chunk->Blend == BlendMode::Additive) {
                        if (pass != RenderPass::Walls) return;
                        ApplyEffect(ctx, Effects->LevelWallAdditive);
                    }
                    else {
                        if (pass != RenderPass::Opaque) return;
                        ApplyEffect(ctx, Effects->Level);
                    }

                    Shaders->Level.SetSampler(ctx.CommandList(), GetTextureSampler());
                    DrawLevelMesh(ctx, *cmd.Data.LevelMesh);
                }

                break;
            }
            case RenderCommandType::Object:
                DrawObject(ctx, *cmd.Data.Object, lerp, pass);
                break;
        }
    }

    void DrawObject(Level& level, Object& obj, float distSquared, float alpha) {
        auto position = Vector3::Lerp(obj.LastPosition, obj.Position, alpha);

        BoundingSphere bounds(position, obj.Radius); // might should use GetBoundingSphere
        if (!CameraFrustum.Contains(bounds))
            return;

        if (auto seg = level.TryGetSegment(obj.Segment)) {
            auto vec = position - seg->Center;
            vec.Normalize();
            position = seg->Center + vec; // Shift slightly away from center so objects within seg are sorted correctly
        }

        // shift depth closer to camera to draw them after walls.
        // Flat value is for nearby objects and multiplier is for distant ones
        float depth = (position - Camera.Position).LengthSquared() * 0.98f - 100;

        if (depth > distSquared)
            DrawObjectOutline(obj);
        else if (obj.Render.Type == RenderType::Model)
            _opaqueQueue.push_back({ &obj, depth });
        else
            _transparentQueue.push_back({ &obj, depth });
    }

    void DrawDebug(Level&) {
        //Debug::DrawPoint(Inferno::Debug::ClosestPoint, Color(1, 0, 0));
        if (Settings::EnablePhysics) {
            for (auto& point : Inferno::Debug::ClosestPoints) {
                Debug::DrawPoint(point, Color(1, 0, 0));
            }
        }

        for (auto& emitter : Inferno::Sound::Debug::Emitters) {
            Debug::DrawPoint(emitter, { 0 ,1, 0 });
        }
    }

    void ClearDepthPrepass(GraphicsContext& ctx) {
        auto& target = Adapter->GetHdrRenderTarget();
        auto& depthBuffer = Adapter->GetHdrDepthBuffer();
        auto& linearDepthBuffer = Adapter->GetLinearDepthBuffer();
        //D3D12_CPU_DESCRIPTOR_HANDLE targets[] = {
        //    linearDepthBuffer.GetRTV(),
        //    linearDepthBuffer.GetRTV()
        //};

        ctx.SetRenderTarget(linearDepthBuffer.GetRTV(), depthBuffer.GetDSV());
        ctx.ClearColor(target);
        ctx.ClearDepth(depthBuffer);
        ctx.ClearColor(linearDepthBuffer);
        ctx.SetViewportAndScissor((UINT)target.GetWidth(), (UINT)target.GetHeight());
        linearDepthBuffer.Transition(ctx.CommandList(), D3D12_RESOURCE_STATE_RENDER_TARGET);
    }

    void ClearMainRenderTarget(GraphicsContext& ctx) {
        //ctx.BeginEvent(L"Clear");

        auto& target = Adapter->GetHdrRenderTarget();
        auto& depthBuffer = Adapter->GetHdrDepthBuffer();
        ctx.SetRenderTarget(target.GetRTV(), depthBuffer.GetDSV());
        //ctx.ClearColor(target);
        //ctx.ClearDepth(depthBuffer);
        ctx.SetViewportAndScissor((UINT)target.GetWidth(), (UINT)target.GetHeight());

        //ctx.EndEvent();
    }

    //void QueueMeshes() {
    //    for (auto& mesh : Meshes)
    //        Render::DrawOpaque(Render::RenderCommand(&mesh, 0));

    //    for (auto& mesh : WallMeshes) {
    //        float depth = (mesh.Chunk->Center - Render::Camera.Position).LengthSquared();
    //        Render::DrawTransparent(Render::RenderCommand{ &mesh, depth });
    //    }
    //}

    void DepthPrepass(GraphicsContext& ctx, float lerp) {
        ctx.BeginEvent(L"Depth prepass");
        // Depth prepass
        ClearDepthPrepass(ctx);
        auto cmdList = ctx.CommandList();

        {
            // Opaque geometry prepass
            for (auto& cmd : _opaqueQueue) {
                if (cmd.Type == RenderCommandType::LevelMesh) {
                    ApplyEffect(ctx, Effects->Depth);
                    cmd.Data.LevelMesh->Draw(cmdList);
                    DrawCalls++;
                }
                else {
                    // Models
                    ApplyEffect(ctx, Effects->DepthObject);
                    auto& object = *cmd.Data.Object;
                    if (object.Render.Type != RenderType::Model) continue;
                    auto model = object.Render.Model.ID;
                    if (cmd.Data.Object->Type == ObjectType::Robot)
                        model = Resources::GetRobotInfo(object.ID).Model;
                    ModelDepthPrepass(cmdList, object, model, lerp);
                }
            }
        }

        //{
        //    // Opaque object depth prepass
        //    ApplyEffect(ctx, Effects->DepthObject);

        //    for (auto& cmd : _transparentQueue) {
        //        if (cmd.Type != RenderCommandType::Object) continue;

        //        auto& object = *cmd.Data.Object;
        //        if (object.Render.Type != RenderType::Model) continue;
        //        auto model = object.Render.Model.ID;

        //        if (cmd.Data.Object->Type == ObjectType::Robot)
        //            model = Resources::GetRobotInfo(object.ID).Model;

        //        ModelDepthPrepass(cmdList, object, model, lerp);
        //    }
        //}


        if (Settings::RenderMode != RenderMode::Flat) {
            // Level walls (potentially transparent)
            auto& effect = Effects->DepthCutout;
            ApplyEffect(ctx, effect);

            for (auto& cmd : _transparentQueue) {
                if (cmd.Type != RenderCommandType::LevelMesh) continue;
                LevelDepthCutout(cmdList, cmd);
            }
        }

        if (Settings::MsaaSamples > 1) {
            // must resolve MS target to allow shader sampling
            Adapter->LinearizedDepthBuffer.ResolveFromMultisample(cmdList, Adapter->MsaaLinearizedDepthBuffer);
            Adapter->MsaaLinearizedDepthBuffer.Transition(cmdList, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        }

        Adapter->LinearizedDepthBuffer.Transition(cmdList, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        Adapter->GetHdrDepthBuffer().Transition(cmdList, D3D12_RESOURCE_STATE_DEPTH_READ);
        ctx.EndEvent();
    }

    void DrawLevel(GraphicsContext& ctx, float lerp) {
        ctx.BeginEvent(L"Level");

        if (Settings::ShowFlickeringLights)
            UpdateFlickeringLights(Game::Level, (float)ElapsedTime, FrameTime);

        if (LevelChanged) {
            Adapter->WaitForGpu();
            _levelMeshBuilder.Update(Game::Level, *_levelMeshBuffer);
            LevelChanged = false;
        }

        ScopedTimer levelTimer(&Metrics::QueueLevel);
        if (Settings::RenderMode != RenderMode::None) {
            // Queue commands for level meshes
            for (auto& mesh : _levelMeshBuilder.GetMeshes())
                _opaqueQueue.push_back({ &mesh, 0 });

            for (auto& mesh : _levelMeshBuilder.GetWallMeshes()) {
                float depth = (mesh.Chunk->Center - Camera.Position).LengthSquared();
                _transparentQueue.push_back({ &mesh, depth });
            }
        }

        if (Settings::ShowObjects) {
            auto distSquared = Settings::ObjectRenderDistance * Settings::ObjectRenderDistance;
            for (auto& obj : Game::Level.Objects) {
                if (obj.Lifespan <= 0) continue;
                DrawObject(Game::Level, obj, distSquared, lerp);
            }
        }

        Seq::sortBy(_transparentQueue, [](const RenderCommand& l, const RenderCommand& r) {
            return l.Depth > r.Depth;
        });

        ctx.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        DepthPrepass(ctx, lerp);

        {
            ctx.BeginEvent(L"Level");
            auto& target = Adapter->GetHdrRenderTarget();
            auto& depthBuffer = Adapter->GetHdrDepthBuffer();
            ctx.SetRenderTarget(target.GetRTV(), depthBuffer.GetDSV());
            ctx.SetViewportAndScissor((UINT)target.GetWidth(), (UINT)target.GetHeight());

            ScopedTimer execTimer(&Metrics::ExecuteRenderCommands);

            ctx.BeginEvent(L"Opaque queue");
            for (auto& cmd : _opaqueQueue)
                ExecuteRenderCommand(ctx, cmd, lerp, RenderPass::Opaque);
            ctx.EndEvent();

            ctx.BeginEvent(L"Wall queue");
            for (auto& cmd : _transparentQueue)
                ExecuteRenderCommand(ctx, cmd, lerp, RenderPass::Walls);
            ctx.EndEvent();

            ctx.BeginEvent(L"Transparent queue");
            for (auto& cmd : _transparentQueue)
                ExecuteRenderCommand(ctx, cmd, lerp, RenderPass::Transparent);
            ctx.EndEvent();

            ctx.EndEvent();

            //for (auto& cmd : _transparentQueue) // draw transparent geometry on models
            //    ExecuteRenderCommand(cmdList, cmd, true);

            // Draw heat volumes
            //    _levelResources->Volumes.Draw(cmdList);

            DrawParticles(ctx);

            if (!Settings::ScreenshotMode) {
                ctx.BeginEvent(L"Editor");
                DrawEditor(ctx.CommandList(), Game::Level);
                DrawDebug(Game::Level);
                ctx.EndEvent();
            }
            else {
                Inferno::DrawGameText(Game::Level.Name, *Canvas, target, 0, 20 * Shell::DpiScale, FontSize::Big, { 1, 1, 1 }, AlignH::Center, AlignV::Top);
                Inferno::DrawGameText("Inferno Engine", *Canvas, target, -20 * Shell::DpiScale, -20 * Shell::DpiScale, FontSize::MediumGold, { 1, 1, 1 }, AlignH::Right, AlignV::Bottom);
            }
            Debug::EndFrame(ctx.CommandList());
        }


        if (Settings::MsaaSamples > 1) {
            Adapter->SceneColorBuffer.ResolveFromMultisample(ctx.CommandList(), Adapter->MsaaColorBuffer);
        }

        ctx.EndEvent();
    }

    void PostProcess(GraphicsContext& ctx) {
        ctx.BeginEvent(L"Post");
        // Post process
        auto backBuffer = Adapter->GetBackBuffer();
        ctx.ClearColor(*backBuffer);
        ctx.SetRenderTarget(backBuffer->GetRTV());
        //backBuffer->Transition(ctx.CommandList(), D3D12_RESOURCE_STATE_RENDER_TARGET);
        //SetRenderTarget(ctx.CommandList(), *backBuffer);

        Adapter->SceneColorBuffer.Transition(ctx.CommandList(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

        if (Settings::EnableBloom && Adapter->TypedUAVLoadSupport_R11G11B10_FLOAT())
            Bloom->Apply(ctx.CommandList(), Adapter->SceneColorBuffer);

        // draw to backbuffer using a shader + polygon
        _tempBatch->SetViewport(Adapter->GetScreenViewport());
        _tempBatch->Begin(ctx.CommandList());
        auto size = Adapter->GetOutputSize();
        _tempBatch->Draw(Adapter->SceneColorBuffer.GetSRV(), XMUINT2{ (uint)size.x, (uint)size.y }, XMFLOAT2{ 0, 0 });
        //if (DebugEmissive)
        //    draw with shader that subtracts 1 from all values;

        _tempBatch->End();
    }

    void DrawUI(GraphicsContext& ctx) {
        ctx.BeginEvent(L"UI");
        auto size = Adapter->GetOutputSize();
        ScopedTimer imguiTimer(&Metrics::ImGui);
        Canvas->Render(ctx, size);
        // Imgui batch modifies render state greatly. Normal geometry will likely not render correctly afterwards.
        g_ImGuiBatch->Render(ctx.CommandList());
        ctx.EndEvent();
    }

    void DrawBriefing(GraphicsContext& ctx, RenderTarget& target) {
        if (!Settings::Windows.BriefingEditor) return;

        ctx.BeginEvent(L"Briefing");
        ctx.ClearColor(target);
        ctx.SetRenderTarget(target.GetRTV());
        ctx.SetViewportAndScissor((UINT)target.GetWidth(), (UINT)target.GetHeight());
        auto& briefing = Editor::BriefingEditor::DebugBriefing;
        if (!briefing.Screens.empty() && !briefing.Screens[0].Pages.empty()) {
            DrawGameText(briefing.Screens[1].Pages[1], *BriefingCanvas, target, 20, 20, FontSize::Small, { 0, 1, 0 });
        }
        BriefingCanvas->Render(ctx, { (float)target.GetWidth(), (float)target.GetHeight() });

        Adapter->Scanline.Execute(ctx.CommandList(), target, Adapter->BriefingScanlineBuffer);
        Adapter->BriefingScanlineBuffer.Transition(ctx.CommandList(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        target.Transition(ctx.CommandList(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

        ctx.EndEvent();
    }

    void Present(float alpha) {
        Metrics::BeginFrame();
        ScopedTimer presentTimer(&Metrics::Present);
        DrawCalls = 0;
        PolygonCount = 0;

        auto& ctx = Adapter->GetGraphicsContext();
        ctx.Reset();
        ActiveEffect = nullptr;

        Heaps->SetDescriptorHeaps(ctx.CommandList());
        DrawBriefing(ctx, Adapter->BriefingColorBuffer);

        auto output = Adapter->GetOutputSize();
        Camera.SetViewport(output.x, output.y);
        Camera.LookAtPerspective();
        ViewProjection = Camera.ViewProj();
        CameraFrustum = Camera.GetFrustum();

        FrameConstants frameConstants{};
        frameConstants.ElapsedTime = (float)ElapsedTime;
        frameConstants.ViewProjection = Camera.ViewProj();
        frameConstants.NearClip = Camera.NearClip;
        frameConstants.FarClip = Camera.FarClip;
        frameConstants.Eye = Camera.Position;
        frameConstants.FrameSize = Adapter->GetOutputSize();

        Adapter->FrameConstantsBuffer.Begin();
        Adapter->FrameConstantsBuffer.Copy({ &frameConstants, 1 });
        Adapter->FrameConstantsBuffer.End();

        DrawLevel(ctx, alpha);

        PostProcess(ctx);
        DrawUI(ctx);

        auto commandQueue = Adapter->GetCommandQueue();
        {
            ScopedTimer presentCallTimer(&Metrics::PresentCall);
            PIXBeginEvent(commandQueue, PIX_COLOR_DEFAULT, L"Present");
            Adapter->Present();
            PIXEndEvent(commandQueue);
        }

        Materials->Dispatch();
        _graphicsMemory->Commit(commandQueue);
        _opaqueQueue.clear();
        _transparentQueue.clear();
    }

    void ReloadTextures() {
        Materials->Reload();
        //NewTextureCache->Reload();
    }
}
