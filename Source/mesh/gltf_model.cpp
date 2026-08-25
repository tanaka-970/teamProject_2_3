// glTFモデルの責務のうち、モデルのライフサイクル（生成・破棄）だけを持つ。
//
//   gltf_model.cpp          … モデルの生成・破棄（このファイル）
//   gltf_modelLoad.cpp      … glTFの解析とGPUリソース準備
//   gltf_modelTexture.cpp   … CPUミップ生成とテクスチャ作成
//   gltf_modelLod.cpp       … LODキャッシュとバックグラウンドLOD生成
//   gltf_modelRender.cpp    … glTFプリミティブの描画
//   gltf_model_cache.cpp    … メッシュディスクキャッシュ
//   gltf_modelInternal.h    … 分割内部のテクスチャヘルパ宣言

#include "gltf_model.h"

#include "tinygltf-release/tiny_gltf.h"

#include "shader.h"
#include "texture.h"
#include "../render/motion_vector_context.h"
#include "../../RePlayEngine/Rendering/RenderStats.h"
#include "../../RePlayEngine/Rendering/Frustum.h"
#include "../../RePlayEngine/Rendering/MeshSimplifier.h"
#include "../../RePlayEngine/Assets/ParallelLoader.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <filesystem>
#include <chrono>
#include <functional>
#include <utility>
using namespace DirectX;

gltf_model::gltf_model(ID3D11Device* device, const std::string& filename)
{
    source_filename_ = filename;

    // まずメッシュキャッシュを試す。ヒットすればglTF解析とジオメトリ構築
    // (実測で3.7秒)を丸ごと飛ばせる。テクスチャはURIから並列で読む。
    {
        using Clock = std::chrono::steady_clock;
        const auto cache_start = Clock::now();
        if (LoadMeshCache(device, filename))
        {
            const auto texture_start = Clock::now();
            LoadTexturesFromUris(device, filename);
            timings_.image_decode_ms = std::chrono::duration<double, std::milli>(
                Clock::now() - texture_start).count();
            timings_.geometry_ms = std::chrono::duration<double, std::milli>(
                texture_start - cache_start).count();
            timings_.image_count = static_cast<int>(materials_.size());
            timings_.mesh_from_cache = true;

            // 描画に必要なシェーダーと定数バッファはキャッシュに含まないので作る。
            // DX12のScene提出はCPU Geometry/URIを利用するため、DX12起動時は
            // 旧D3D11のGPU資源を作らずにキャッシュをそのまま公開する。
            loaded_ = device != nullptr ? PrepareDeviceResources(device) : true;
            timings_.total_ms = std::chrono::duration<double, std::milli>(
                Clock::now() - cache_start).count();
        }
    }

    if (!loaded_) loaded_ = Load(device, filename);
    else
    {
        // キャッシュ経路でもコリジョンとLODは同じ手順で用意する。
    }
    if (!loaded_) return;

    // CPU-only importではLOD GPU bufferを作らず、ExportStaticPrimitivesへ
    // 原型Geometryを保持する。DX12 upload側がFence寿命を所有する。
    if (device == nullptr)
    {
        lods_ready_.store(true);
        return;
    }

    // 初回(キャッシュミス)のときだけ書き出す。次回起動から解析を飛ばせる。
    if (!timings_.mesh_from_cache) SaveMeshCache(filename);

    // まずディスクキャッシュを試す。QEMは重いので2回目以降は読むだけにする。
    const auto lod_cache_start = std::chrono::steady_clock::now();
    const bool cache_hit = LoadLodCache(device);
    timings_.lod_cache_ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - lod_cache_start).count();
    timings_.lod_from_cache = cache_hit;
    if (cache_hit)
    {
        for (Primitive& primitive : primitives_)
        {
            primitive.source_vertices.clear();
            primitive.source_vertices.shrink_to_fit();
            primitive.source_indices.clear();
            primitive.source_indices.shrink_to_fit();
        }
        lods_ready_.store(true);
        return;
    }

    // キャッシュが無い初回だけバックグラウンドで生成する。
    // ここを同期で回すとロードが数十秒伸びるため、
    // 出来上がるまではLOD0で描いておく。
    lod_thread_ = std::thread([this, device]
    {
        const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        BuildLods(device);
        SaveLodCache();
        // キャッシュへ書き出したらCPU側のコピーは不要。
        for (Primitive& primitive : primitives_)
        {
            primitive.lod_cache.clear();
            primitive.lod_cache.shrink_to_fit();
            primitive.source_vertices.clear();
            primitive.source_vertices.shrink_to_fit();
            primitive.source_indices.clear();
            primitive.source_indices.shrink_to_fit();
        }
        // ここで初めて描画側へ公開する。以降 lods は変更されない。
        lods_ready_.store(true);
        if (SUCCEEDED(com)) CoUninitialize();
    });
}

gltf_model::~gltf_model()
{
    // 生成スレッドがthisを触っているので必ず待つ。
    if (lod_thread_.joinable()) lod_thread_.join();
}

bool gltf_model::ComputeBounds(XMFLOAT3& minimum, XMFLOAT3& maximum) const noexcept
{
    bool found = false;
    for (const Primitive& primitive : primitives_)
    {
        if (primitive.index_count == 0 && primitive.vertex_count == 0) continue;

        if (!found)
        {
            minimum = primitive.bounds_minimum;
            maximum = primitive.bounds_maximum;
            found = true;
            continue;
        }

        minimum.x = (std::min)(minimum.x, primitive.bounds_minimum.x);
        minimum.y = (std::min)(minimum.y, primitive.bounds_minimum.y);
        minimum.z = (std::min)(minimum.z, primitive.bounds_minimum.z);
        maximum.x = (std::max)(maximum.x, primitive.bounds_maximum.x);
        maximum.y = (std::max)(maximum.y, primitive.bounds_maximum.y);
        maximum.z = (std::max)(maximum.z, primitive.bounds_maximum.z);
    }
    return found;
}
