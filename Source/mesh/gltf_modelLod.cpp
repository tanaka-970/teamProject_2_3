#include "gltf_model.h"

#include "../../RePlayEngine/Assets/ParallelLoader.h"
#include "../../RePlayEngine/Rendering/MeshSimplifier.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <utility>

using namespace DirectX;

namespace
{
    std::filesystem::path& GltfCacheRootStorage()
    {
        static std::filesystem::path root =
            std::filesystem::path("resources") / ".replay_cache";
        return root;
    }

}

void gltf_model::SetCacheRoot(std::filesystem::path root)
{
    if (root.empty()) root = std::filesystem::path("resources") / ".replay_cache";
    GltfCacheRootStorage() = std::move(root);
}

const std::filesystem::path& gltf_model::CacheRoot()
{
    return GltfCacheRootStorage();
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
    return CacheRoot() / "lods" / name;
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
