#pragma once

#include "Heap.h"
#include "Level.h"
#include "Resources.h"
#include "Buffers.h"
#include "Concurrent.h"
#include "FileSystem.h"
#include "ScopedTimer.h"

namespace Inferno::Render {
    constexpr void FillTexture(span<ubyte> data, ubyte red, ubyte green, ubyte blue, ubyte alpha) {
        for (size_t i = 0; i < data.size() / 4; i++) {
            data[i * 4] = red;
            data[i * 4 + 1] = green;
            data[i * 4 + 2] = blue;
            data[i * 4 + 3] = alpha;
        }
    }

    struct Material2D {
        enum { Diffuse, SuperTransparency, Emissive, Specular, Normal, Count };

        Texture2D Textures[Count]{};
        // SRV handles
        D3D12_GPU_DESCRIPTOR_HANDLE Handles[Count] = {};
        uint Index = 0;
        TexID ID = TexID::Invalid;
        string Name;
    };

    struct MaterialUpload {
        TexID ID = TexID::None;
        Outrage::Bitmap Outrage;
        const PigBitmap* Bitmap;
        bool SuperTransparent = false;
        bool ForceLoad = false;
    };

    // Supports loading and unloading materials
    class MaterialLibrary {
        Material2D _defaultMaterial;
        MaterialInfo _defaultMaterialInfo = {};

        bool _requestPrune = false;
        Texture2D _black, _white, _purple, _normal;
        ConcurrentList<Material2D> _materials, _pendingCopies;
        ConcurrentList<MaterialUpload> _requestedUploads;
        Dictionary<string, Material2D> _unpackedMaterials;
        Set<TexID> _submittedUploads; // textures submitted for async processing. Used to filter future requests.

        Ptr<WorkerThread> _worker;
        List<MaterialInfo> _materialInfo;

        friend class MaterialUploadWorker;

    public:
        MaterialLibrary(size_t size);

        void Shutdown();
        Material2D White, Black;

        void LoadMaterials(span<const TexID> tids, bool forceLoad);
        void LoadMaterialsAsync(span<const TexID> ids, bool forceLoad = false);
        void Dispatch();

        span<MaterialInfo> GetAllMaterialInfo() { return _materialInfo; }
        MaterialInfo& GetMaterialInfo(TexID id) {
            if (!Seq::inRange(_materialInfo, (int)id)) return _defaultMaterialInfo;
            return _materialInfo[(int)id];
        }

        void ResetMaterials() {
            for (auto& material : _materialInfo)
                material = {};
        }

        // Gets a material based on a D1/D2 texture ID
        const Material2D& Get(TexID id) const {
            if ((int)id > _materials.Size()) return _defaultMaterial;
            auto& material = _materials[(int)id];
            return material.ID > TexID::Invalid ? material : _defaultMaterial;
        }

        const Material2D& Get(EClipID id, float time, bool critical) const {
            auto& eclip = Resources::GetEffectClip(id);
            if (eclip.TimeLeft > 0)
                time = eclip.VClip.PlayTime - eclip.TimeLeft;

            TexID tex = eclip.VClip.GetFrame(time);
            if (critical && eclip.CritClip != EClipID::None) {
                auto& crit = Resources::GetEffectClip(eclip.CritClip);
                tex = crit.VClip.GetFrame(time);
            }

            if ((int)tex > _materials.Size()) return _defaultMaterial;
            auto& material = _materials[(int)tex];
            return material.ID > TexID::Invalid ? material : _defaultMaterial;
        }

        // Gets a material based on a D1/D2 level texture ID
        const Material2D& Get(LevelTexID tid) const {
            auto id = Resources::LookupTexID(tid);
            return Get(id);
        }

        // Gets a material loaded from the filesystem based on name
        const Material2D& Get(const string& name) {
            if (!_unpackedMaterials.contains(name)) return _defaultMaterial;
            return _unpackedMaterials[name];
        };

        void LoadLevelTextures(const Inferno::Level& level, bool force);
        void LoadTextures(span<string> names);

        void Reload();

        bool PreloadDoors = true; // For editor previews

        // Unloads unused materials
        void Prune() { _requestPrune = true; }
        void Unload();

        // Materials to keep loaded after a prune
        Set<TexID> KeepLoaded;

    private:
        MaterialUpload PrepareUpload(TexID id, bool forceLoad);
        void PruneInternal();

        bool HasUnloadedTextures(span<const TexID> tids) {
            bool hasPending = false;
            for (auto& id : tids) {
                if (id <= TexID::Invalid) continue;
                if (_materials[(int)id].ID == id) continue;
                hasPending = true;
                break;
            }

            return hasPending;
        }

        void LoadDefaults();
    };

    DirectX::ResourceUploadBatch BeginTextureUpload();

    ComPtr<ID3D12CommandQueue> EndTextureUpload(DirectX::ResourceUploadBatch&);

    List<TexID> GetTexturesForModel(ModelID id);
    Set<TexID> GetLevelTextures(const Level& level, bool preloadDoors);

    inline Ptr<MaterialLibrary> Materials;
    Set<TexID> GetLevelSegmentTextures(const Level& level);
}
