// glTFモデルのディスクキャッシュ。
//
// glTFの解析(accessorから1頂点ずつ展開)とジオメトリ構築で3.7秒かかっていた。
// 展開済みの頂点/インデックス/AABB/マテリアル参照/LODをそのままバイナリで
// 落としておけば、2回目以降はファイルを読んでGPUバッファを作るだけで済む。
//
// テクスチャは別扱い。画像ファイルのURIだけ保存して、読み込み時に
// 並列デコードする(画像はモデルより差し替え頻度が高いため)。

#include "gltf_model.h"

#include "../../RePlayEngine/Assets/ParallelLoader.h"
#include "../../RePlayEngine/Assets/TextureCompressor.h"
#include "texture.h"

#include <atomic>
#include <cmath>
#include <fstream>
#include <map>

using namespace DirectX;

namespace
{
    constexpr std::uint32_t kMeshCacheMagic = 0x48534D52u;  // 'RMSH'
    // v3: v2のTexture URIに加え alphaMode / alphaCutoff を保存する。
    // 派生キャッシュなので旧v2は安全に無効化され、元glTFから一度だけ再生成される。
    constexpr std::uint32_t kMeshCacheVersion = 3;

    // 文字列は長さ+本体で書く。
    void WriteString(std::ofstream& stream, const std::string& text)
    {
        const std::uint32_t length = static_cast<std::uint32_t>(text.size());
        stream.write(reinterpret_cast<const char*>(&length), sizeof(length));
        if (length > 0) stream.write(text.data(), length);
    }

    bool ReadString(std::ifstream& stream, std::string& text)
    {
        std::uint32_t length = 0;
        stream.read(reinterpret_cast<char*>(&length), sizeof(length));
        if (!stream || length > (1u << 20)) return false;
        text.resize(length);
        if (length > 0) stream.read(text.data(), length);
        return static_cast<bool>(stream);
    }

    template<class T>
    void WriteVector(std::ofstream& stream, const std::vector<T>& values)
    {
        const std::uint32_t count = static_cast<std::uint32_t>(values.size());
        stream.write(reinterpret_cast<const char*>(&count), sizeof(count));
        if (count > 0)
            stream.write(reinterpret_cast<const char*>(values.data()),
                static_cast<std::streamsize>(count * sizeof(T)));
    }

    template<class T>
    bool ReadVector(std::ifstream& stream, std::vector<T>& values, std::uint32_t limit)
    {
        std::uint32_t count = 0;
        stream.read(reinterpret_cast<char*>(&count), sizeof(count));
        if (!stream || count > limit) return false;
        values.resize(count);
        if (count > 0)
            stream.read(reinterpret_cast<char*>(values.data()),
                static_cast<std::streamsize>(count * sizeof(T)));
        return static_cast<bool>(stream);
    }
}

std::filesystem::path gltf_model::MeshCachePath(const std::string& filename) const
{
    if (filename.empty()) return {};

    // パス + ファイルサイズ + 更新時刻 でキーを作る。
    // モデルを差し替えたら自動で作り直されるようにする。
    std::uint64_t hash = 1469598103934665603ull;
    const auto mix = [&hash](std::uint64_t value)
    {
        for (int byte = 0; byte < 8; ++byte)
        {
            hash ^= (value >> (byte * 8)) & 0xFFull;
            hash *= 1099511628211ull;
        }
    };

    for (const unsigned char character : filename)
    {
        hash ^= character;
        hash *= 1099511628211ull;
    }

    std::error_code error;
    const auto size = std::filesystem::file_size(filename, error);
    if (!error) mix(static_cast<std::uint64_t>(size));
    const auto write_time = std::filesystem::last_write_time(filename, error);
    if (!error)
        mix(static_cast<std::uint64_t>(write_time.time_since_epoch().count()));

    char name[64]{};
    sprintf_s(name, "%016llx.replaymesh", static_cast<unsigned long long>(hash));
    return CacheRoot() / "meshes" / name;
}

bool gltf_model::SaveMeshCache(const std::string& filename) const
{
    const auto path = MeshCachePath(filename);
    if (path.empty() || primitives_.empty()) return false;

    // 参照中のTextureに再読込URIが無い状態をキャッシュすると、次回起動だけ
    // 白Textureへ落ちる。DDS生成に失敗した場合はメッシュキャッシュ自体を作らず、
    // 次回もGLB本体から読み直して復旧を試みる。
    for (const Material& material : materials_)
    {
        if ((material.base_color_texture && material.base_color_uri.empty()) ||
            (material.normal_texture && material.normal_uri.empty()) ||
            (material.occlusion_roughness_metalness_texture && material.orm_uri.empty()))
            return false;
    }

    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) return false;

    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) return false;

    stream.write(reinterpret_cast<const char*>(&kMeshCacheMagic), sizeof(kMeshCacheMagic));
    stream.write(reinterpret_cast<const char*>(&kMeshCacheVersion), sizeof(kMeshCacheVersion));

    // --- マテリアル ---
    const std::uint32_t material_count = static_cast<std::uint32_t>(materials_.size());
    stream.write(reinterpret_cast<const char*>(&material_count), sizeof(material_count));
    for (const Material& material : materials_)
    {
        stream.write(reinterpret_cast<const char*>(&material.base_color),
            sizeof(material.base_color));
        WriteString(stream, material.base_color_uri);
        WriteString(stream, material.normal_uri);
        WriteString(stream, material.orm_uri);
        stream.write(reinterpret_cast<const char*>(&material.alpha_mode),
            sizeof(material.alpha_mode));
        stream.write(reinterpret_cast<const char*>(&material.alpha_cutoff),
            sizeof(material.alpha_cutoff));
    }

    // --- プリミティブ ---
    const std::uint32_t primitive_count = static_cast<std::uint32_t>(primitives_.size());
    stream.write(reinterpret_cast<const char*>(&primitive_count), sizeof(primitive_count));
    for (const Primitive& primitive : primitives_)
    {
        stream.write(reinterpret_cast<const char*>(&primitive.node_transform),
            sizeof(primitive.node_transform));
        stream.write(reinterpret_cast<const char*>(&primitive.bounds_minimum),
            sizeof(primitive.bounds_minimum));
        stream.write(reinterpret_cast<const char*>(&primitive.bounds_maximum),
            sizeof(primitive.bounds_maximum));
        stream.write(reinterpret_cast<const char*>(&primitive.material),
            sizeof(primitive.material));
        WriteVector(stream, primitive.source_vertices);
        WriteVector(stream, primitive.source_indices);
    }

    // --- 当たり判定用の三角形 ---
    WriteVector(stream, collision_triangles_);

    return static_cast<bool>(stream);
}

bool gltf_model::LoadMeshCache(ID3D11Device* device, const std::string& filename)
{
    const auto path = MeshCachePath(filename);
    if (path.empty() || !std::filesystem::exists(path)) return false;

    std::ifstream stream(path, std::ios::binary);
    if (!stream) return false;

    std::uint32_t magic = 0, version = 0;
    stream.read(reinterpret_cast<char*>(&magic), sizeof(magic));
    stream.read(reinterpret_cast<char*>(&version), sizeof(version));
    if (!stream || magic != kMeshCacheMagic || version != kMeshCacheVersion) return false;

    // --- マテリアル ---
    std::uint32_t material_count = 0;
    stream.read(reinterpret_cast<char*>(&material_count), sizeof(material_count));
    if (!stream || material_count > 4096) return false;
    materials_.assign((std::max)(size_t{ 1 }, static_cast<size_t>(material_count)), Material{});
    for (std::uint32_t i = 0; i < material_count; ++i)
    {
        stream.read(reinterpret_cast<char*>(&materials_[i].base_color),
            sizeof(materials_[i].base_color));
        if (!ReadString(stream, materials_[i].base_color_uri)) return false;
        if (!ReadString(stream, materials_[i].normal_uri)) return false;
        if (!ReadString(stream, materials_[i].orm_uri)) return false;
        stream.read(reinterpret_cast<char*>(&materials_[i].alpha_mode),
            sizeof(materials_[i].alpha_mode));
        stream.read(reinterpret_cast<char*>(&materials_[i].alpha_cutoff),
            sizeof(materials_[i].alpha_cutoff));
        if (!stream || materials_[i].alpha_mode < 0 || materials_[i].alpha_mode > 2 ||
            !std::isfinite(materials_[i].alpha_cutoff))
            return false;
    }

    // --- プリミティブ ---
    std::uint32_t primitive_count = 0;
    stream.read(reinterpret_cast<char*>(&primitive_count), sizeof(primitive_count));
    if (!stream || primitive_count == 0 || primitive_count > 65536) return false;

    primitives_.clear();
    primitives_.resize(primitive_count);
    for (std::uint32_t i = 0; i < primitive_count; ++i)
    {
        Primitive& primitive = primitives_[i];
        stream.read(reinterpret_cast<char*>(&primitive.node_transform),
            sizeof(primitive.node_transform));
        stream.read(reinterpret_cast<char*>(&primitive.bounds_minimum),
            sizeof(primitive.bounds_minimum));
        stream.read(reinterpret_cast<char*>(&primitive.bounds_maximum),
            sizeof(primitive.bounds_maximum));
        stream.read(reinterpret_cast<char*>(&primitive.material),
            sizeof(primitive.material));
        if (!stream) return false;
        if (!ReadVector(stream, primitive.source_vertices, 1u << 24)) return false;
        if (!ReadVector(stream, primitive.source_indices, 1u << 26)) return false;
        if (primitive.source_vertices.empty() || primitive.source_indices.empty()) return false;
    }

    if (!ReadVector(stream, collision_triangles_, 1u << 24)) return false;

    // --- GPUバッファを作る。プリミティブ単位で並列化できる ---
    if (device == nullptr)
    {
        for (Primitive& primitive : primitives_)
        {
            primitive.index_count = static_cast<uint32_t>(primitive.source_indices.size());
            primitive.vertex_count = static_cast<uint32_t>(primitive.source_vertices.size());
        }
        return true;
    }

    std::atomic<bool> failed{ false };
    ReplayEngine::Assets::ParallelLoader::Run(primitives_.size(), [&](std::size_t i)
    {
        Primitive& primitive = primitives_[i];
        D3D11_BUFFER_DESC desc{};
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        desc.ByteWidth = static_cast<UINT>(primitive.source_vertices.size() * sizeof(Vertex));
        D3D11_SUBRESOURCE_DATA initial{ primitive.source_vertices.data(), 0, 0 };
        if (FAILED(device->CreateBuffer(&desc, &initial,
            primitive.vertex_buffer.GetAddressOf()))) { failed.store(true); return; }

        desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
        desc.ByteWidth = static_cast<UINT>(primitive.source_indices.size() * sizeof(uint32_t));
        initial.pSysMem = primitive.source_indices.data();
        if (FAILED(device->CreateBuffer(&desc, &initial,
            primitive.index_buffer.GetAddressOf()))) { failed.store(true); return; }

        primitive.index_count = static_cast<uint32_t>(primitive.source_indices.size());
        primitive.vertex_count = static_cast<uint32_t>(primitive.source_vertices.size());
    });

    // LODキャッシュが既にあるならQEMを回さないので、読み込んだCPU側コピー
    // (Sponzaで約250MB)はここで返してしまう。物理メモリの空きが少ない環境で
    // 落ちる原因になっていた。
    if (std::filesystem::exists(LodCachePath()))
    {
        for (Primitive& primitive : primitives_)
        {
            primitive.source_vertices.clear();
            primitive.source_vertices.shrink_to_fit();
            primitive.source_indices.clear();
            primitive.source_indices.shrink_to_fit();
        }
    }

    return !failed.load();
}


bool gltf_model::StaticPrimitiveInfoAt(std::size_t index,
    StaticPrimitiveInfo& out) const
{
    if (!loaded_ || has_skins_ || has_animations_ || index >= primitives_.size())
        return false;
    const Primitive& primitive = primitives_[index];
    out = {};
    out.node_transform = primitive.node_transform;
    out.material = primitive.material;
    if (primitive.material >= 0 &&
        primitive.material < static_cast<int>(materials_.size()))
    {
        const Material& material = materials_[static_cast<std::size_t>(primitive.material)];
        out.embedded_base_color = material.base_color;
        out.alpha_mode = material.alpha_mode;
        out.alpha_cutoff = material.alpha_cutoff;
        const std::filesystem::path base_directory =
            std::filesystem::path(source_filename_).parent_path();
        if (!material.base_color_uri.empty())
            out.embedded_base_color_texture =
                base_directory / std::filesystem::path(material.base_color_uri);
        if (!material.normal_uri.empty())
            out.embedded_normal_texture =
                base_directory / std::filesystem::path(material.normal_uri);
        if (!material.orm_uri.empty())
            out.embedded_orm_texture =
                base_directory / std::filesystem::path(material.orm_uri);
    }
    return true;
}


bool gltf_model::ExportStaticPrimitives(
    std::vector<StaticPrimitiveExport>& out) const
{
    out.clear();
    if (!loaded_ || has_skins_ || has_animations_ || primitives_.empty()) return false;

    const auto append_exports = [this, &out](const std::vector<Primitive>& primitives,
        const std::vector<Material>& materials) -> bool
    {
        try
        {
            out.reserve(primitives.size());
            const std::filesystem::path base_directory =
                std::filesystem::path(source_filename_).parent_path();
            for (const Primitive& primitive : primitives)
            {
                if (primitive.source_vertices.empty() || primitive.source_indices.empty())
                    return false;
                StaticPrimitiveExport exported;
                exported.vertices = primitive.source_vertices;
                exported.indices = primitive.source_indices;
                exported.node_transform = primitive.node_transform;
                exported.material = primitive.material;
                if (primitive.material >= 0 &&
                    primitive.material < static_cast<int>(materials.size()))
                {
                    const Material& material = materials[static_cast<std::size_t>(primitive.material)];
                    exported.embedded_base_color = material.base_color;
                    exported.alpha_mode = material.alpha_mode;
                    exported.alpha_cutoff = material.alpha_cutoff;
                    if (!material.base_color_uri.empty())
                        exported.embedded_base_color_texture =
                            base_directory / std::filesystem::path(material.base_color_uri);
                    if (!material.normal_uri.empty())
                        exported.embedded_normal_texture =
                            base_directory / std::filesystem::path(material.normal_uri);
                    if (!material.orm_uri.empty())
                        exported.embedded_orm_texture =
                            base_directory / std::filesystem::path(material.orm_uri);
                }
                out.push_back(std::move(exported));
            }
            return !out.empty();
        }
        catch (...)
        {
            out.clear();
            return false;
        }
    };

    bool all_sources_resident = true;
    for (const Primitive& primitive : primitives_)
    {
        if (primitive.source_vertices.empty() || primitive.source_indices.empty())
        {
            all_sources_resident = false;
            break;
        }
    }
    if (all_sources_resident) return append_exports(primitives_, materials_);

// glTF Runtime は LOD/Cache 処理後に大きな CPU Geometry を意図的に解放する。
// DX12 のために数百 MB を保持させてはいけない。DX12 Mesh Cache がこの Model を初めて
// 必要としたときに、展開済みの replaymesh Cache を一度だけ読む。
    const std::filesystem::path cache_path = MeshCachePath(source_filename_);
    if (cache_path.empty() || !std::filesystem::exists(cache_path)) return false;
    std::ifstream stream(cache_path, std::ios::binary);
    if (!stream) return false;

    std::uint32_t magic = 0, version = 0;
    stream.read(reinterpret_cast<char*>(&magic), sizeof(magic));
    stream.read(reinterpret_cast<char*>(&version), sizeof(version));
    if (!stream || magic != kMeshCacheMagic || version != kMeshCacheVersion) return false;

    std::uint32_t material_count = 0;
    stream.read(reinterpret_cast<char*>(&material_count), sizeof(material_count));
    if (!stream || material_count > 4096) return false;
    std::vector<Material> materials((std::max)(size_t{ 1 },
        static_cast<size_t>(material_count)));
    for (std::uint32_t i = 0; i < material_count; ++i)
    {
        stream.read(reinterpret_cast<char*>(&materials[i].base_color),
            sizeof(materials[i].base_color));
        if (!ReadString(stream, materials[i].base_color_uri)) return false;
        if (!ReadString(stream, materials[i].normal_uri)) return false;
        if (!ReadString(stream, materials[i].orm_uri)) return false;
        stream.read(reinterpret_cast<char*>(&materials[i].alpha_mode),
            sizeof(materials[i].alpha_mode));
        stream.read(reinterpret_cast<char*>(&materials[i].alpha_cutoff),
            sizeof(materials[i].alpha_cutoff));
        if (!stream || materials[i].alpha_mode < 0 || materials[i].alpha_mode > 2 ||
            !std::isfinite(materials[i].alpha_cutoff))
            return false;
    }

    std::uint32_t primitive_count = 0;
    stream.read(reinterpret_cast<char*>(&primitive_count), sizeof(primitive_count));
    if (!stream || primitive_count == 0 || primitive_count > 65536) return false;
    std::vector<Primitive> primitives(primitive_count);
    for (std::uint32_t i = 0; i < primitive_count; ++i)
    {
        Primitive& primitive = primitives[i];
        stream.read(reinterpret_cast<char*>(&primitive.node_transform),
            sizeof(primitive.node_transform));
        stream.read(reinterpret_cast<char*>(&primitive.bounds_minimum),
            sizeof(primitive.bounds_minimum));
        stream.read(reinterpret_cast<char*>(&primitive.bounds_maximum),
            sizeof(primitive.bounds_maximum));
        stream.read(reinterpret_cast<char*>(&primitive.material),
            sizeof(primitive.material));
        if (!stream) return false;
        if (!ReadVector(stream, primitive.source_vertices, 1u << 24)) return false;
        if (!ReadVector(stream, primitive.source_indices, 1u << 26)) return false;
    }
    return append_exports(primitives, materials);
}

void gltf_model::LoadTexturesFromUris(ID3D11Device* device, const std::string& gltf_filename)
{
    if (!device) return;

    // URIはglTFファイルからの相対パス。実体を絶対パスへ直してから読む。
    const auto base_directory = std::filesystem::path(gltf_filename).parent_path();

    // 同じURIが複数マテリアルから参照されるので、一度読んだものは共有する。
    std::map<std::string, Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>> loaded;
    std::vector<std::string> unique_uris;
    const auto collect = [&](const std::string& uri)
    {
        if (uri.empty() || loaded.count(uri)) return;
        loaded.emplace(uri, nullptr);
        unique_uris.push_back(uri);
    };
    for (const Material& material : materials_)
    {
        collect(material.base_color_uri);
        collect(material.normal_uri);
        collect(material.orm_uri);
    }

    // load_texture_from_file は内部でミューテックス保護され、
    // 同名の .dds があればそちらを優先する(BC圧縮版を置ける)。
    std::vector<Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>> views(unique_uris.size());
    // 2Kテクスチャは1枚でRGBA+ミップに約21MB使う。並列度を上げると
    // ピークメモリが跳ねるため2本に留める。
    const int workers = (std::min)(2, ReplayEngine::Assets::ParallelLoader::DefaultWorkerCount());
    ReplayEngine::Assets::ParallelLoader::Run(unique_uris.size(), workers, [&](std::size_t i)
    {
        const std::filesystem::path uri_path(unique_uris[i]);
        const auto full_path = (uri_path.is_absolute()
            ? uri_path : base_directory / uri_path).lexically_normal();
        std::error_code error;
        if (!std::filesystem::exists(full_path, error)) return;

        // .dds が無ければ初回だけBC圧縮版を作る。以降はデコード不要になり、
        // VRAMもBC1で1/8、BC5で1/4に収まる。
        auto dds_path = full_path;
        dds_path.replace_extension(".dds");
        if (!std::filesystem::exists(dds_path, error))
        {
            ReplayEngine::Assets::TextureCompressor::Compress(full_path);
        }

        // load_texture_from_file は同名の .dds があればそちらを優先する。
        D3D11_TEXTURE2D_DESC description{};
        load_texture_from_file(device, full_path.wstring().c_str(),
            views[i].GetAddressOf(), &description);
    });

    for (size_t i = 0; i < unique_uris.size(); ++i) loaded[unique_uris[i]] = views[i];

    for (Material& material : materials_)
    {
        if (!material.base_color_uri.empty())
            material.base_color_texture = loaded[material.base_color_uri];
        if (!material.normal_uri.empty())
            material.normal_texture = loaded[material.normal_uri];
        if (!material.orm_uri.empty())
            material.occlusion_roughness_metalness_texture = loaded[material.orm_uri];
    }
}
