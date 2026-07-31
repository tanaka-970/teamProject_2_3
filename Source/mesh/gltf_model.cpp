#include "gltf_model.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define TINYGLTF_IMPLEMENTATION
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

using namespace DirectX;

namespace
{
    const unsigned char* AccessorBytes(const tinygltf::Model& model,
        const tinygltf::Accessor& accessor, size_t& stride)
    {
        if (accessor.bufferView < 0 || accessor.bufferView >= static_cast<int>(model.bufferViews.size())) return nullptr;
        const auto& view = model.bufferViews[accessor.bufferView];
        if (view.buffer < 0 || view.buffer >= static_cast<int>(model.buffers.size())) return nullptr;
        const auto& buffer = model.buffers[view.buffer];
        stride = accessor.ByteStride(view);
        const size_t offset = view.byteOffset + accessor.byteOffset;
        return offset < buffer.data.size() ? buffer.data.data() + offset : nullptr;
    }

    bool ReadFloatVector(const tinygltf::Model& model, int accessor_index,
        int component_count, std::vector<float>& output)
    {
        if (accessor_index < 0 || accessor_index >= static_cast<int>(model.accessors.size())) return false;
        const auto& accessor = model.accessors[accessor_index];
        if (accessor.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT) return false;
        size_t stride = 0;
        const unsigned char* bytes = AccessorBytes(model, accessor, stride);
        if (!bytes || stride < sizeof(float) * component_count) return false;
        output.resize(accessor.count * component_count);
        for (size_t i = 0; i < accessor.count; ++i)
        {
            const float* value = reinterpret_cast<const float*>(bytes + i * stride);
            for (int c = 0; c < component_count; ++c) output[i * component_count + c] = value[c];
        }
        return true;
    }

    bool ReadIndices(const tinygltf::Model& model, int accessor_index, std::vector<uint32_t>& output)
    {
        if (accessor_index < 0 || accessor_index >= static_cast<int>(model.accessors.size())) return false;
        const auto& accessor = model.accessors[accessor_index];
        size_t stride = 0;
        const unsigned char* bytes = AccessorBytes(model, accessor, stride);
        if (!bytes) return false;
        output.resize(accessor.count);
        for (size_t i = 0; i < accessor.count; ++i)
        {
            const unsigned char* value = bytes + i * stride;
            switch (accessor.componentType)
            {
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE: output[i] = *value; break;
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: output[i] = *reinterpret_cast<const uint16_t*>(value); break;
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT: output[i] = *reinterpret_cast<const uint32_t*>(value); break;
            default: return false;
            }
        }
        return true;
    }

    XMMATRIX NodeLocal(const tinygltf::Node& node)
    {
        if (node.matrix.size() == 16)
        {
            XMFLOAT4X4 source{};
            float* dst = &source._11;
            // glTFのmatrixは列優先(要素12,13,14が平行移動)。これをXMFLOAT4X4へ
            // 順に詰めると、行ベクトル規約のDirectXMath行列としてそのまま正しい。
            // ここで転置すると平行移動が_14/_24/_34(w成分)へ移り、
            // 頂点が射影除算で無限遠に飛ぶので転置してはいけない。
            for (size_t i = 0; i < 16; ++i) dst[i] = static_cast<float>(node.matrix[i]);
            return XMLoadFloat4x4(&source);
        }
        const XMMATRIX scale = node.scale.size() == 3
            ? XMMatrixScaling(static_cast<float>(node.scale[0]), static_cast<float>(node.scale[1]), static_cast<float>(node.scale[2]))
            : XMMatrixIdentity();
        const XMMATRIX rotation = node.rotation.size() == 4
            ? XMMatrixRotationQuaternion(XMVectorSet(static_cast<float>(node.rotation[0]), static_cast<float>(node.rotation[1]),
                static_cast<float>(node.rotation[2]), static_cast<float>(node.rotation[3])))
            : XMMatrixIdentity();
        const XMMATRIX translation = node.translation.size() == 3
            ? XMMatrixTranslation(static_cast<float>(node.translation[0]), static_cast<float>(node.translation[1]), static_cast<float>(node.translation[2]))
            : XMMatrixIdentity();
        return scale * rotation * translation;
    }

    template<class VertexT>
    void GenerateNormals(std::vector<VertexT>& vertices, const std::vector<uint32_t>& indices)
    {
        for (auto& vertex : vertices) vertex.normal = { 0, 0, 0 };
        for (size_t i = 0; i + 2 < indices.size(); i += 3)
        {
            const uint32_t a = indices[i], b = indices[i + 1], c = indices[i + 2];
            if (a >= vertices.size() || b >= vertices.size() || c >= vertices.size()) continue;
            const XMVECTOR p0 = XMLoadFloat3(&vertices[a].position);
            const XMVECTOR p1 = XMLoadFloat3(&vertices[b].position);
            const XMVECTOR p2 = XMLoadFloat3(&vertices[c].position);
            const XMVECTOR normal = XMVector3Cross(p1 - p0, p2 - p0);
            for (uint32_t index : { a, b, c })
            {
                XMVECTOR sum = XMLoadFloat3(&vertices[index].normal) + normal;
                XMStoreFloat3(&vertices[index].normal, sum);
            }
        }
        for (auto& vertex : vertices)
            XMStoreFloat3(&vertex.normal, XMVector3Normalize(XMLoadFloat3(&vertex.normal)));
    }

    // ミップチェーンをCPU側で作ってからテクスチャを生成する。
    // ローダーはDeviceContextを持たないためGenerateMipsが使えないが、
    // 全レベルを初期データとして渡せば同じ結果になる。
    // 1K以上のテクスチャをミップ無しで使うと遠景が激しくちらつき、
    // TAAでも取り切れないのでミップは必須。
    bool CreateTextureWithMipChain(ID3D11Device* device,
        const std::vector<uint8_t>& top_level_rgba, UINT width, UINT height,
        ID3D11ShaderResourceView** out_view)
    {
        if (!device || !out_view || width == 0 || height == 0) return false;
        if (top_level_rgba.size() < static_cast<size_t>(width) * height * 4) return false;

        UINT mip_levels = 1;
        for (UINT size = (std::max)(width, height); size > 1; size >>= 1) ++mip_levels;

        // レベルごとの画素を保持する。initial dataがポインタを参照するため、
        // CreateTexture2Dが終わるまで生存させる必要がある。
        std::vector<std::vector<uint8_t>> levels;
        levels.reserve(mip_levels);
        levels.push_back(top_level_rgba);

        UINT level_width = width;
        UINT level_height = height;
        for (UINT level = 1; level < mip_levels; ++level)
        {
            const UINT next_width = (std::max)(1u, level_width >> 1);
            const UINT next_height = (std::max)(1u, level_height >> 1);
            const std::vector<uint8_t>& source = levels.back();
            std::vector<uint8_t> destination(static_cast<size_t>(next_width) * next_height * 4);

            // 2x2ボックスフィルタ。奇数サイズでも範囲外を読まないようクランプする。
            for (UINT y = 0; y < next_height; ++y)
            {
                const UINT y0 = (std::min)(y * 2, level_height - 1);
                const UINT y1 = (std::min)(y * 2 + 1, level_height - 1);
                for (UINT x = 0; x < next_width; ++x)
                {
                    const UINT x0 = (std::min)(x * 2, level_width - 1);
                    const UINT x1 = (std::min)(x * 2 + 1, level_width - 1);
                    const size_t taps[4]
                    {
                        (static_cast<size_t>(y0) * level_width + x0) * 4,
                        (static_cast<size_t>(y0) * level_width + x1) * 4,
                        (static_cast<size_t>(y1) * level_width + x0) * 4,
                        (static_cast<size_t>(y1) * level_width + x1) * 4,
                    };
                    const size_t out_index = (static_cast<size_t>(y) * next_width + x) * 4;
                    for (int channel = 0; channel < 4; ++channel)
                    {
                        const unsigned sum =
                            static_cast<unsigned>(source[taps[0] + channel]) +
                            static_cast<unsigned>(source[taps[1] + channel]) +
                            static_cast<unsigned>(source[taps[2] + channel]) +
                            static_cast<unsigned>(source[taps[3] + channel]);
                        destination[out_index + channel] = static_cast<uint8_t>((sum + 2) / 4);
                    }
                }
            }

            levels.push_back(std::move(destination));
            level_width = next_width;
            level_height = next_height;
        }

        std::vector<D3D11_SUBRESOURCE_DATA> initial(mip_levels);
        level_width = width;
        level_height = height;
        for (UINT level = 0; level < mip_levels; ++level)
        {
            initial[level].pSysMem = levels[level].data();
            initial[level].SysMemPitch = level_width * 4;
            initial[level].SysMemSlicePitch = 0;
            level_width = (std::max)(1u, level_width >> 1);
            level_height = (std::max)(1u, level_height >> 1);
        }

        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = mip_levels;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
        if (FAILED(device->CreateTexture2D(&desc, initial.data(), texture.GetAddressOf())))
            return false;
        return SUCCEEDED(device->CreateShaderResourceView(texture.Get(), nullptr, out_view));
    }
}

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
            loaded_ = PrepareDeviceResources(device);
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

std::filesystem::path gltf_model::LodCachePath() const
{
    if (source_filename_.empty()) return {};
    // パスと形状からキーを作る。頂点数を混ぜてモデル差し替えを検出する。
    std::uint64_t hash = 1469598103934665603ull;
    for (const unsigned char character : source_filename_)
    {
        hash ^= character;
        hash *= 1099511628211ull;
    }
    for (const Primitive& primitive : primitives_)
    {
        hash ^= primitive.index_count;
        hash *= 1099511628211ull;
        hash ^= primitive.vertex_count;
        hash *= 1099511628211ull;
    }
    char name[64]{};
    sprintf_s(name, "%016llx_lod0.replaylod", static_cast<unsigned long long>(hash));
    return std::filesystem::path("resources") / ".replay_cache" / "lods" / name;
}

bool gltf_model::CreateLodBuffers(ID3D11Device* device, LodLevel& level,
    const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices)
{
    if (!device || vertices.empty() || indices.empty()) return false;

    D3D11_BUFFER_DESC desc{};
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    desc.ByteWidth = static_cast<UINT>(vertices.size() * sizeof(Vertex));
    D3D11_SUBRESOURCE_DATA initial{ vertices.data(), 0, 0 };
    // ID3D11Device::Create系はスレッドセーフなのでワーカーから呼べる。
    if (FAILED(device->CreateBuffer(&desc, &initial, level.vertex_buffer.GetAddressOf())))
        return false;

    desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
    desc.ByteWidth = static_cast<UINT>(indices.size() * sizeof(uint32_t));
    initial.pSysMem = indices.data();
    if (FAILED(device->CreateBuffer(&desc, &initial, level.index_buffer.GetAddressOf())))
        return false;

    level.index_count = static_cast<UINT>(indices.size());
    level.vertex_count = static_cast<UINT>(vertices.size());
    return true;
}

bool gltf_model::LoadLodCache(ID3D11Device* device)
{
    const auto path = LodCachePath();
    if (path.empty() || !std::filesystem::exists(path)) return false;

    std::ifstream stream(path, std::ios::binary);
    if (!stream) return false;

    std::uint32_t magic = 0, version = 0, primitive_count = 0;
    stream.read(reinterpret_cast<char*>(&magic), sizeof(magic));
    stream.read(reinterpret_cast<char*>(&version), sizeof(version));
    stream.read(reinterpret_cast<char*>(&primitive_count), sizeof(primitive_count));
    if (!stream || magic != 0x444F4C52u /* 'RLOD' */ || version != 1) return false;
    if (primitive_count != primitives_.size()) return false;

    for (std::uint32_t index = 0; index < primitive_count; ++index)
    {
        std::uint32_t lod_count = 0;
        stream.read(reinterpret_cast<char*>(&lod_count), sizeof(lod_count));
        if (!stream || lod_count > 8) return false;

        for (std::uint32_t level_index = 0; level_index < lod_count; ++level_index)
        {
            std::uint32_t vertex_count = 0, index_count = 0;
            stream.read(reinterpret_cast<char*>(&vertex_count), sizeof(vertex_count));
            stream.read(reinterpret_cast<char*>(&index_count), sizeof(index_count));
            if (!stream || vertex_count == 0 || index_count == 0) return false;

            std::vector<Vertex> vertices(vertex_count);
            std::vector<uint32_t> indices(index_count);
            stream.read(reinterpret_cast<char*>(vertices.data()),
                static_cast<std::streamsize>(vertex_count * sizeof(Vertex)));
            stream.read(reinterpret_cast<char*>(indices.data()),
                static_cast<std::streamsize>(index_count * sizeof(uint32_t)));
            if (!stream) return false;

            LodLevel level{};
            if (!CreateLodBuffers(device, level, vertices, indices)) return false;
            primitives_[index].lods.push_back(std::move(level));
        }
    }
    return true;
}

bool gltf_model::SaveLodCache() const
{
    const auto path = LodCachePath();
    if (path.empty()) return false;

    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) return false;

    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) return false;

    const std::uint32_t magic = 0x444F4C52u; // 'RLOD'
    const std::uint32_t version = 1;
    const std::uint32_t primitive_count = static_cast<std::uint32_t>(primitives_.size());
    stream.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
    stream.write(reinterpret_cast<const char*>(&version), sizeof(version));
    stream.write(reinterpret_cast<const char*>(&primitive_count), sizeof(primitive_count));

    for (const Primitive& primitive : primitives_)
    {
        const std::uint32_t lod_count = static_cast<std::uint32_t>(primitive.lod_cache.size());
        stream.write(reinterpret_cast<const char*>(&lod_count), sizeof(lod_count));
        for (const LodCacheEntry& entry : primitive.lod_cache)
        {
            const std::uint32_t vertex_count = static_cast<std::uint32_t>(entry.vertices.size());
            const std::uint32_t index_count = static_cast<std::uint32_t>(entry.indices.size());
            stream.write(reinterpret_cast<const char*>(&vertex_count), sizeof(vertex_count));
            stream.write(reinterpret_cast<const char*>(&index_count), sizeof(index_count));
            stream.write(reinterpret_cast<const char*>(entry.vertices.data()),
                static_cast<std::streamsize>(vertex_count * sizeof(Vertex)));
            stream.write(reinterpret_cast<const char*>(entry.indices.data()),
                static_cast<std::streamsize>(index_count * sizeof(uint32_t)));
        }
    }
    return static_cast<bool>(stream);
}

void gltf_model::BuildLods(ID3D11Device* device)
{
    if (!device || primitives_.empty()) return;

    // LOD1以降の削減比率。LOD0は原型。
    // 建築物は平面が多くQEMがよく効くので、最終段は1/10まで落とせる。
    static constexpr float kLodRatios[]{ 0.5f, 0.25f, 0.1f };
    // 三角形が少ないプリミティブはLODを作っても効果がなく、
    // 形が崩れるだけなので閾値を設ける。
    static constexpr uint32_t kMinimumTriangles = 256;

    // プリミティブごとに独立して簡略化できるため、そのまま並列化できる。
    // Sponzaは405プリミティブあるので、ここが一番効く。
    ReplayEngine::Assets::ParallelLoader::Run(primitives_.size(),
        [&](std::size_t primitive_index)
    {
        Primitive& primitive = primitives_[primitive_index];
        if (primitive.index_count / 3 < kMinimumTriangles) return;
        if (primitive.source_vertices.empty() || primitive.source_indices.empty()) return;

        std::vector<ReplayEngine::Rendering::MeshSimplifier::Vertex> working;
        working.reserve(primitive.source_vertices.size());
        for (const Vertex& vertex : primitive.source_vertices)
            working.push_back({ vertex.position, vertex.normal, vertex.texcoord });

        std::vector<uint32_t> working_indices = primitive.source_indices;

        for (const float ratio : kLodRatios)
        {
            ReplayEngine::Rendering::MeshSimplifier::Options options{};
            // 直前のLODからさらに削るのではなく、常に原型からの比率で作る。
            // 段階的に削ると誤差が累積して形が崩れやすい。
            options.target_ratio = ratio;
            const auto simplified = ReplayEngine::Rendering::MeshSimplifier::Simplify(
                working, working_indices, options);

            // 減らせなかったらそれ以上のLODは作らない。
            if (simplified.result_triangles >= simplified.source_triangles) break;

            std::vector<Vertex> lod_vertices(simplified.vertices.size());
            for (size_t i = 0; i < simplified.vertices.size(); ++i)
            {
                lod_vertices[i].position = simplified.vertices[i].position;
                lod_vertices[i].normal = simplified.vertices[i].normal;
                lod_vertices[i].texcoord = simplified.vertices[i].texcoord;
            }

            LodLevel level{};
            if (!CreateLodBuffers(device, level, lod_vertices, simplified.indices)) break;
            primitive.lods.push_back(std::move(level));
            // 次回起動でQEMを回さずに済むよう、CPU側のコピーも残しておく。
            primitive.lod_cache.push_back({ std::move(lod_vertices), simplified.indices });
        }
    });
}

// シェーダー・入力レイアウト・定数バッファ・ダミーテクスチャ。
// glTF経路とメッシュキャッシュ経路の両方から呼ぶ。
bool gltf_model::PrepareDeviceResources(ID3D11Device* device)
{
    if (!device) return false;

    D3D11_INPUT_ELEMENT_DESC layout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(Vertex, position), D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(Vertex, normal), D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(Vertex, texcoord), D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    create_vs_from_cso(device, "static_mesh_vs.cso", vertex_shader_.GetAddressOf(),
        input_layout_.GetAddressOf(), layout, _countof(layout));
    create_ps_from_cso(device, "static_mesh_ps.cso", pixel_shader_.GetAddressOf());
    D3D11_BUFFER_DESC constant_desc{};
    constant_desc.ByteWidth = sizeof(Constants);
    constant_desc.Usage = D3D11_USAGE_DEFAULT;
    constant_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    if (FAILED(device->CreateBuffer(&constant_desc, nullptr, constant_buffer_.GetAddressOf()))) return false;
    // TAAのモーションベクター用の定数バッファ(b6)。
    constant_desc.ByteWidth = sizeof(motion_vectors::ObjectConstants);
    if (FAILED(device->CreateBuffer(&constant_desc, nullptr,
        motion_object_constant_buffer_.GetAddressOf()))) return false;
    make_dummy_texture(device, white_texture_.GetAddressOf(), 0xffffffff, 4);
    return vertex_shader_ && pixel_shader_ && input_layout_ && constant_buffer_;
}

bool gltf_model::Load(ID3D11Device* device, const std::string& filename)
{
    if (!device) { error_ = "Direct3D device is null"; return false; }
    std::string extension = std::filesystem::path(filename).extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (extension != ".gltf" && extension != ".glb")
    {
        error_ = "Unsupported model extension: " + extension;
        return false;
    }

    using Clock = std::chrono::steady_clock;
    const auto load_start = Clock::now();
    const auto elapsed_ms = [](Clock::time_point from)
    {
        return std::chrono::duration<double, std::milli>(Clock::now() - from).count();
    };

    tinygltf::TinyGLTF loader;
    tinygltf::Model model;
    std::string warning;

    // tinygltfは既定で全画像をここで直列にデコードする。2Kが数十枚あると
    // ここがロード時間の大半を占めるため、デコードは後回しにして
    // 生バイトのまま受け取る(as_is)。展開は下でParallelLoaderに任せる。
    loader.SetImageLoader([](tinygltf::Image* image, const int, std::string*,
        std::string*, int, int, const unsigned char* bytes, int size, void*) -> bool
    {
        if (!image || !bytes || size <= 0) return false;
        image->as_is = true;
        image->image.assign(bytes, bytes + size);
        return true;
    }, nullptr);
    const bool parsed = extension == ".glb"
        ? loader.LoadBinaryFromFile(&model, &error_, &warning, filename)
        : loader.LoadASCIIFromFile(&model, &error_, &warning, filename);
    if (!warning.empty()) OutputDebugStringA(("[glTF] " + warning + "\n").c_str());
    if (!parsed) return false;
    has_skins_ = !model.skins.empty();
    has_animations_ = !model.animations.empty();

    if (!PrepareDeviceResources(device)) return false;

    timings_.parse_ms = elapsed_ms(load_start);
    timings_.image_count = static_cast<int>(model.images.size());

    // 画像のデコード → ミップ生成 → GPUテクスチャ作成 を1枚単位で並列化する。
    // 各画像は独立しており ID3D11Device::Create系はスレッドセーフなので、
    // そのままワーカーへ分配できる。ロード時間の削減幅が最も大きい箇所。
    const auto image_start = Clock::now();
    std::vector<Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>> image_views(model.images.size());
    // ワーカー数を絞る。1枚あたりRGBA+ミップで数十MBの一時領域を使うため、
    // 全コアで走らせるとピークメモリが跳ね上がる。
    const int image_workers = (std::min)(4,
        ReplayEngine::Assets::ParallelLoader::DefaultWorkerCount());
    ReplayEngine::Assets::ParallelLoader::Run(model.images.size(), image_workers,
        [&](std::size_t i)
    {
        auto& image = model.images[i];
        if (image.image.empty()) return;

        std::vector<uint8_t> rgba;
        UINT width = 0;
        UINT height = 0;

        if (image.as_is)
        {
            // 遅延させたデコードをここで行う。RGBA固定で受け取る。
            int decoded_width = 0, decoded_height = 0, decoded_components = 0;
            unsigned char* decoded = stbi_load_from_memory(
                image.image.data(), static_cast<int>(image.image.size()),
                &decoded_width, &decoded_height, &decoded_components, 4);
            if (!decoded || decoded_width <= 0 || decoded_height <= 0)
            {
                if (decoded) stbi_image_free(decoded);
                return;
            }
            width = static_cast<UINT>(decoded_width);
            height = static_cast<UINT>(decoded_height);
            rgba.assign(decoded, decoded + static_cast<size_t>(width) * height * 4);
            stbi_image_free(decoded);
            // 圧縮データはもう不要。保持し続けるとピークメモリが1GB近く増える。
            image.image.clear();
            image.image.shrink_to_fit();
        }
        else
        {
            // 既にデコード済み(tinygltf既定経路)ならRGBAへ整えるだけ。
            if (image.width <= 0 || image.height <= 0) return;
            width = static_cast<UINT>(image.width);
            height = static_cast<UINT>(image.height);
            rgba.resize(static_cast<size_t>(width) * height * 4);
            const int components = image.component > 0 ? image.component : 4;
            for (size_t p = 0; p < static_cast<size_t>(width) * height; ++p)
            {
                rgba[p * 4 + 0] = image.image[p * components + 0];
                rgba[p * 4 + 1] = components > 1 ? image.image[p * components + 1] : rgba[p * 4 + 0];
                rgba[p * 4 + 2] = components > 2 ? image.image[p * components + 2] : rgba[p * 4 + 0];
                rgba[p * 4 + 3] = components > 3 ? image.image[p * components + 3] : 255;
            }
        }

        CreateTextureWithMipChain(device, rgba, width, height,
            image_views[i].GetAddressOf());
    });
    timings_.image_decode_ms = elapsed_ms(image_start);

    const auto geometry_start = Clock::now();

    materials_.resize((std::max)(size_t{ 1 }, model.materials.size()));
    for (size_t i = 0; i < model.materials.size(); ++i)
    {
        const auto& source = model.materials[i].pbrMetallicRoughness;
        if (source.baseColorFactor.size() == 4)
            materials_[i].base_color = { static_cast<float>(source.baseColorFactor[0]), static_cast<float>(source.baseColorFactor[1]),
                static_cast<float>(source.baseColorFactor[2]), static_cast<float>(source.baseColorFactor[3]) };
        // テクスチャ番号 → 画像番号 → 生成済みSRV の解決。
        const auto resolve = [&](int texture_index)
            -> Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>
        {
            if (texture_index < 0 ||
                texture_index >= static_cast<int>(model.textures.size())) return nullptr;
            const int image_index = model.textures[texture_index].source;
            if (image_index < 0 ||
                image_index >= static_cast<int>(image_views.size())) return nullptr;
            return image_views[image_index];
        };

        // キャッシュ経路で再読み込みできるよう、画像ファイルの場所も残す。
        const auto resolve_uri = [&](int texture_index) -> std::string
        {
            if (texture_index < 0 ||
                texture_index >= static_cast<int>(model.textures.size())) return {};
            const int image_index = model.textures[texture_index].source;
            if (image_index < 0 ||
                image_index >= static_cast<int>(model.images.size())) return {};
            return model.images[image_index].uri;
        };

        materials_[i].base_color_uri = resolve_uri(source.baseColorTexture.index);
        materials_[i].normal_uri = resolve_uri(model.materials[i].normalTexture.index);
        materials_[i].orm_uri = resolve_uri(source.metallicRoughnessTexture.index);
        if (materials_[i].orm_uri.empty())
            materials_[i].orm_uri = resolve_uri(model.materials[i].occlusionTexture.index);

        materials_[i].base_color_texture = resolve(source.baseColorTexture.index);
        materials_[i].normal_texture = resolve(model.materials[i].normalTexture.index);
        // glTFのmetallicRoughnessTextureは B=Metalness, G=Roughness。
        // occlusionTextureが別にある場合はRチャンネルにAOが入る想定で同じ扱いにする。
        materials_[i].occlusion_roughness_metalness_texture =
            resolve(source.metallicRoughnessTexture.index);
        if (!materials_[i].occlusion_roughness_metalness_texture)
            materials_[i].occlusion_roughness_metalness_texture =
                resolve(model.materials[i].occlusionTexture.index);
    }

    std::vector<XMMATRIX> globals(model.nodes.size(), XMMatrixIdentity());
    std::vector<bool> visited(model.nodes.size(), false);
    std::function<void(int, XMMATRIX)> visit = [&](int index, XMMATRIX parent)
    {
        if (index < 0 || index >= static_cast<int>(model.nodes.size())) return;
        globals[index] = NodeLocal(model.nodes[index]) * parent;
        visited[index] = true;
        for (int child : model.nodes[index].children) visit(child, globals[index]);
    };
    const int scene_index = model.defaultScene >= 0 ? model.defaultScene : 0;
    if (scene_index >= 0 && scene_index < static_cast<int>(model.scenes.size()))
        for (int root : model.scenes[scene_index].nodes) visit(root, XMMatrixIdentity());
    for (size_t i = 0; i < model.nodes.size(); ++i) if (!visited[i]) visit(static_cast<int>(i), XMMatrixIdentity());

    for (size_t node_index = 0; node_index < model.nodes.size(); ++node_index)
    {
        const auto& node = model.nodes[node_index];
        if (node.mesh < 0 || node.mesh >= static_cast<int>(model.meshes.size())) continue;
        for (const auto& source : model.meshes[node.mesh].primitives)
        {
            if (source.mode != TINYGLTF_MODE_TRIANGLES) continue;
            const auto position_it = source.attributes.find("POSITION");
            if (position_it == source.attributes.end()) continue;
            std::vector<float> positions, normals, texcoords;
            if (!ReadFloatVector(model, position_it->second, 3, positions)) continue;
            const auto normal_it = source.attributes.find("NORMAL");
            const auto texcoord_it = source.attributes.find("TEXCOORD_0");
            const bool has_normals = normal_it != source.attributes.end() && ReadFloatVector(model, normal_it->second, 3, normals);
            if (texcoord_it != source.attributes.end()) ReadFloatVector(model, texcoord_it->second, 2, texcoords);
            std::vector<Vertex> vertices(positions.size() / 3);
            for (size_t i = 0; i < vertices.size(); ++i)
            {
                vertices[i].position = { -positions[i * 3], positions[i * 3 + 1], positions[i * 3 + 2] };
                if (has_normals) vertices[i].normal = { -normals[i * 3], normals[i * 3 + 1], normals[i * 3 + 2] };
                if (texcoords.size() >= (i + 1) * 2) vertices[i].texcoord = { texcoords[i * 2], texcoords[i * 2 + 1] };
            }
            std::vector<uint32_t> indices;
            if (!ReadIndices(model, source.indices, indices))
            {
                indices.resize(vertices.size());
                for (size_t i = 0; i < indices.size(); ++i) indices[i] = static_cast<uint32_t>(i);
            }
            for (size_t i = 0; i + 2 < indices.size(); i += 3) std::swap(indices[i + 1], indices[i + 2]);
            if (!has_normals) GenerateNormals(vertices, indices);

            Primitive primitive{};
        // glTF�̉E����W�n����荞�ނƂ��A���_��X���Ŕ��]����B
        // �ړ��Ɖ�]�̐�������ۂ��߁A�m�[�h�ϊ����������Ŕ��]����B
            const XMMATRIX reflection = XMMatrixScaling(-1.0f, 1.0f, 1.0f);
            XMStoreFloat4x4(&primitive.node_transform,
                reflection * globals[node_index] * reflection);
            primitive.index_count = static_cast<uint32_t>(indices.size());
            primitive.vertex_count = static_cast<uint32_t>(vertices.size());
            primitive.material = source.material;
            // LOD生成に使う原型データ。BuildLods後に解放する。
            primitive.source_vertices = vertices;
            primitive.source_indices = indices;

            const XMMATRIX node_transform = XMLoadFloat4x4(&primitive.node_transform);

            // 視錐台カリング用のAABBを、ノード変換を適用した空間で求める。
            // 描画時はワールド行列だけを掛けて判定できる。
            {
                XMFLOAT3 minimum{ FLT_MAX, FLT_MAX, FLT_MAX };
                XMFLOAT3 maximum{ -FLT_MAX, -FLT_MAX, -FLT_MAX };
                for (const Vertex& vertex : vertices)
                {
                    XMFLOAT3 transformed{};
                    XMStoreFloat3(&transformed, XMVector3TransformCoord(
                        XMLoadFloat3(&vertex.position), node_transform));
                    minimum.x = (std::min)(minimum.x, transformed.x);
                    minimum.y = (std::min)(minimum.y, transformed.y);
                    minimum.z = (std::min)(minimum.z, transformed.z);
                    maximum.x = (std::max)(maximum.x, transformed.x);
                    maximum.y = (std::max)(maximum.y, transformed.y);
                    maximum.z = (std::max)(maximum.z, transformed.z);
                }
                if (!vertices.empty())
                {
                    primitive.bounds_minimum = minimum;
                    primitive.bounds_maximum = maximum;
                }
            }
            for (size_t index = 0; index + 2 < indices.size(); index += 3)
            {
                const uint32_t triangle_indices[3]
                {
                    indices[index], indices[index + 1], indices[index + 2]
                };
                if (triangle_indices[0] >= vertices.size() ||
                    triangle_indices[1] >= vertices.size() ||
                    triangle_indices[2] >= vertices.size()) continue;
                ReplayEngine::Physics::Triangle triangle{};
                for (int vertex_index = 0; vertex_index < 3; ++vertex_index)
                {
                    XMStoreFloat3(&triangle.vertices[vertex_index], XMVector3TransformCoord(
                        XMLoadFloat3(&vertices[triangle_indices[vertex_index]].position),
                        node_transform));
                }
                collision_triangles_.push_back(triangle);
            }
            D3D11_BUFFER_DESC desc{};
            desc.Usage = D3D11_USAGE_DEFAULT; desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
            desc.ByteWidth = static_cast<UINT>(vertices.size() * sizeof(Vertex));
            D3D11_SUBRESOURCE_DATA initial{ vertices.data(), 0, 0 };
            if (FAILED(device->CreateBuffer(&desc, &initial, primitive.vertex_buffer.GetAddressOf()))) continue;
            desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
            desc.ByteWidth = static_cast<UINT>(indices.size() * sizeof(uint32_t));
            initial.pSysMem = indices.data();
            if (FAILED(device->CreateBuffer(&desc, &initial, primitive.index_buffer.GetAddressOf()))) continue;
            primitives_.push_back(std::move(primitive));
        }
    }
    timings_.geometry_ms = elapsed_ms(geometry_start);
    timings_.total_ms = elapsed_ms(load_start);

    if (primitives_.empty()) { error_ = "glTF contains no supported triangle primitives"; return false; }
    return vertex_shader_ && pixel_shader_ && input_layout_ && constant_buffer_;
}

void gltf_model::render(ID3D11DeviceContext* context, const XMFLOAT4X4& world,
    const XMFLOAT4& tint, ID3D11PixelShader* alternative_pixel_shader,
    bool write_motion_vectors, bool depth_only)
{
    if (!loaded_ || !context) return;
    const motion_vectors::FrameContext& motion_frame = motion_vectors::Frame();
    const bool emit_motion = write_motion_vectors && motion_object_constant_buffer_ && !depth_only;
    const bool advance_motion_history =
        emit_motion && motion_frame_id_ != motion_frame.frame_id;
    if (emit_motion) previous_primitive_worlds_.resize(primitives_.size());
    size_t motion_primitive_index = 0;
    context->IASetInputLayout(input_layout_.Get());
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(vertex_shader_.Get(), nullptr, 0);
    // 深度プリパスではピクセルシェーダーを外す。これがオーバードロー削減の要。
    context->PSSetShader(depth_only ? nullptr
        : (alternative_pixel_shader ? alternative_pixel_shader : pixel_shader_.Get()), nullptr, 0);
    auto& culling = ReplayEngine::Rendering::Culling();
    // 生成スレッドが書き終わったかを1回だけ読む。trueになった後は
    // lods は変更されないため、以降のループ内はロック不要で安全。
    const bool lods_ready = lods_ready_.load();
    // 統計表示用。生成中かどうかと、使えるLOD段数を申告する。
    if (!lods_ready) culling.lod_building = true;
    else
    {
        for (const auto& primitive : primitives_)
        {
            culling.lod_available = (std::max)(culling.lod_available,
                static_cast<unsigned int>(primitive.lods.size()));
        }
    }
    for (const auto& primitive : primitives_)
    {
        // 視錐台の外にあるプリミティブは丸ごと飛ばす。Sponzaは405個の
        // プリミティブに分かれているため、これだけで頂点処理とドローコールが
        // 大幅に減る(通過率40%台=6割が無駄になっていた)。
        if (culling.enabled && culling.frustum.Valid())
        {
            ++culling.tested;
            if (!culling.frustum.IntersectsTransformedAabb(
                primitive.bounds_minimum, primitive.bounds_maximum, world))
            {
                ++culling.culled;
                // モーションベクターの履歴は描画しなくても進めないと、
                // 画面へ戻ってきた瞬間に誤った速度が出る。
                if (emit_motion)
                {
                    if (advance_motion_history &&
                        motion_primitive_index < previous_primitive_worlds_.size())
                    {
                        XMFLOAT4X4 skipped_world{};
                        XMStoreFloat4x4(&skipped_world,
                            XMLoadFloat4x4(&primitive.node_transform) * XMLoadFloat4x4(&world));
                        previous_primitive_worlds_[motion_primitive_index] = skipped_world;
                    }
                    ++motion_primitive_index;
                }
                continue;
            }
        }

        const Material* material = primitive.material >= 0 && primitive.material < static_cast<int>(materials_.size())
            ? &materials_[primitive.material] : &materials_[0];

        // 画面上の投影サイズからLODを選ぶ。遠くの小さいプリミティブは
        // 粗いメッシュへ差し替えて頂点処理を削る。
        // 生成中(lods_ready_==false)はLOD0で描く。
        const int lod_level = lods_ready
            ? culling.SelectLod(primitive.bounds_minimum,
                primitive.bounds_maximum, world, primitive.lods.size())
            : 0;
        culling.CountLodDraw(lod_level);

        ID3D11Buffer* vertex_buffer = primitive.vertex_buffer.Get();
        ID3D11Buffer* index_buffer = primitive.index_buffer.Get();
        UINT draw_index_count = primitive.index_count;
        UINT draw_vertex_count = primitive.vertex_count;
        if (lod_level > 0 && lod_level <= static_cast<int>(primitive.lods.size()))
        {
            const LodLevel& lod = primitive.lods[lod_level - 1];
            if (lod.vertex_buffer && lod.index_buffer)
            {
                vertex_buffer = lod.vertex_buffer.Get();
                index_buffer = lod.index_buffer.Get();
                draw_index_count = lod.index_count;
                draw_vertex_count = lod.vertex_count;
            }
        }

        UINT stride = sizeof(Vertex), offset = 0;
        context->IASetVertexBuffers(0, 1, &vertex_buffer, &stride, &offset);
        context->IASetIndexBuffer(index_buffer, DXGI_FORMAT_R32_UINT, 0);
        // t0=baseColor, t1=法線, t2=ORM。未設定のスロットは明示的にnullへ落として
        // 直前のマテリアルのテクスチャが残らないようにする。
        ID3D11ShaderResourceView* textures[3]{
            material->base_color_texture ? material->base_color_texture.Get()
                                         : white_texture_.Get(),
            material->normal_texture.Get(),
            material->occlusion_roughness_metalness_texture.Get() };
        if (!depth_only) context->PSSetShaderResources(0, 3, textures);
        Constants constants{};
        XMStoreFloat4x4(&constants.world,
            XMLoadFloat4x4(&primitive.node_transform) * XMLoadFloat4x4(&world));
        XMStoreFloat4(&constants.material_color,
            XMLoadFloat4(&tint) * XMLoadFloat4(&material->base_color));
        context->UpdateSubresource(constant_buffer_.Get(), 0, nullptr, &constants, 0, 0);
        context->VSSetConstantBuffers(0, 1, constant_buffer_.GetAddressOf());
        context->PSSetConstantBuffers(0, 1, constant_buffer_.GetAddressOf());

        if (emit_motion)
        {
            // 剛体なのでプリミティブごとの前フレームのワールド行列を渡すだけでよい。
            const bool has_history = motion_history_valid_ &&
                motion_primitive_index < previous_primitive_worlds_.size();
            motion_vectors::ObjectConstants motion_object{};
            motion_object.previous_world = has_history
                ? previous_primitive_worlds_[motion_primitive_index] : constants.world;
            motion_object.previous_view_projection = motion_frame.previous_view_projection;
            motion_object.params = { motion_frame.enabled && has_history ? 1.0f : 0.0f,
                motion_frame.current_jitter.x, motion_frame.current_jitter.y, 0.0f };
            motion_object.params2 = { motion_frame.previous_jitter.x,
                motion_frame.previous_jitter.y, 0.0f, 0.0f };
            context->UpdateSubresource(
                motion_object_constant_buffer_.Get(), 0, nullptr, &motion_object, 0, 0);
            context->VSSetConstantBuffers(
                6, 1, motion_object_constant_buffer_.GetAddressOf());

            if (advance_motion_history)
                previous_primitive_worlds_[motion_primitive_index] = constants.world;
            ++motion_primitive_index;
        }

        // 深度プリパスは同じ形状を二度数えないよう統計から除く。
        if (!depth_only)
            ReplayEngine::Rendering::Stats().CountDrawIndexed(
                draw_index_count, draw_vertex_count);
        context->DrawIndexed(draw_index_count, 0, 0);
    }

    // 同一フレーム内で二度呼ばれても履歴は一度だけ進める。
    if (advance_motion_history)
    {
        motion_frame_id_ = motion_frame.frame_id;
        motion_history_valid_ = true;
    }
}
