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
#include "../../RePlayEngine/Assets/TextureCompressor.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <filesystem>
#include <chrono>
#include <functional>
#include <utility>
#include "gltf_modelInternal.h"

using namespace DirectX;
using namespace gltf_model_detail;

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
    // 0xAABBGGRR = RGBA(128,128,255,255)。Normal Map未指定の正しい既定値。
    make_dummy_texture(device, neutral_normal_texture_.GetAddressOf(), 0xffff8080, 4);
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

    // GLB内蔵画像にはURIが無い。メッシュキャッシュだけ保存すると次回起動時に
    // 画像を再取得できないため、用途別BC形式のDDSを既存gltf Cache配下へ置く。
    // モデルのパス・サイズ・更新時刻を含むMeshCacheキーを流用するので、
    // GLB差し替え時には古いTextureを誤って読むことがない。
    struct EmbeddedTextureCache
    {
        bool base_color_used = false;
        bool normal_used = false;
        bool orm_used = false;
        std::string base_color_uri;
        std::string normal_uri;
        std::string orm_uri;
    };
    std::vector<EmbeddedTextureCache> embedded_texture_cache(model.images.size());
    const auto image_index_of = [&model](int texture_index) -> int
    {
        if (texture_index < 0 ||
            texture_index >= static_cast<int>(model.textures.size())) return -1;
        const int image_index = model.textures[texture_index].source;
        return image_index >= 0 && image_index < static_cast<int>(model.images.size())
            ? image_index : -1;
    };
    for (const auto& material : model.materials)
    {
        const auto& pbr = material.pbrMetallicRoughness;
        if (const int image = image_index_of(pbr.baseColorTexture.index); image >= 0)
            embedded_texture_cache[image].base_color_used = true;
        if (const int image = image_index_of(material.normalTexture.index); image >= 0)
            embedded_texture_cache[image].normal_used = true;
        if (const int image = image_index_of(pbr.metallicRoughnessTexture.index); image >= 0)
            embedded_texture_cache[image].orm_used = true;
        if (const int image = image_index_of(material.occlusionTexture.index); image >= 0)
            embedded_texture_cache[image].orm_used = true;
    }
    const std::filesystem::path texture_cache_directory =
        CacheRoot() / "textures" / MeshCachePath(filename).stem();

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

        // 外部URIは従来どおり元画像の隣にDDSを作る。ここではGLB内蔵画像だけを
        // 既存TextureCompressorへ渡し、デコード済みRGBAを再利用する。
        if (image.uri.empty())
        {
            using Compressor = ReplayEngine::Assets::TextureCompressor;
            auto& cache = embedded_texture_cache[i];
            const auto save = [&](const char* suffix, Compressor::Format format,
                std::string& output_uri)
            {
                std::filesystem::path path = texture_cache_directory /
                    ("image_" + std::to_string(i) + suffix + ".dds");
                std::error_code error;
                if (std::filesystem::exists(path, error) ||
                    Compressor::CompressRgba(rgba.data(), static_cast<int>(width),
                        static_cast<int>(height), path, format).succeeded)
                    output_uri = std::filesystem::absolute(path).lexically_normal().string();
            };
            if (cache.base_color_used)
                save("_base", Compressor::Format::Auto, cache.base_color_uri);
            if (cache.normal_used)
                save("_normal", Compressor::Format::BC5, cache.normal_uri);
            if (cache.orm_used)
                save("_orm", Compressor::Format::BC1, cache.orm_uri);
        }
    });
    timings_.image_decode_ms = elapsed_ms(image_start);

    const auto geometry_start = Clock::now();

    materials_.resize((std::max)(size_t{ 1 }, model.materials.size()));
    for (size_t i = 0; i < model.materials.size(); ++i)
    {
        const auto& source = model.materials[i].pbrMetallicRoughness;
        // アルファ抜きの宣言。影パスもここを見て抜くので、通常描画と食い違わない。
        materials_[i].alpha_mode = model.materials[i].alphaMode == "MASK" ? 1
            : (model.materials[i].alphaMode == "BLEND" ? 2 : 0);
        materials_[i].alpha_cutoff = static_cast<float>(model.materials[i].alphaCutoff);
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
        const auto resolve_uri = [&](int texture_index, const std::string& embedded_uri)
            -> std::string
        {
            if (texture_index < 0 ||
                texture_index >= static_cast<int>(model.textures.size())) return {};
            const int image_index = model.textures[texture_index].source;
            if (image_index < 0 ||
                image_index >= static_cast<int>(model.images.size())) return {};
            return model.images[image_index].uri.empty()
                ? embedded_uri : model.images[image_index].uri;
        };

        const auto cached_uri = [&](int texture_index,
            std::string EmbeddedTextureCache::* member) -> std::string
        {
            const int image_index = image_index_of(texture_index);
            return image_index >= 0 ? embedded_texture_cache[image_index].*member : std::string{};
        };

        materials_[i].base_color_uri = resolve_uri(source.baseColorTexture.index,
            cached_uri(source.baseColorTexture.index, &EmbeddedTextureCache::base_color_uri));
        materials_[i].normal_uri = resolve_uri(model.materials[i].normalTexture.index,
            cached_uri(model.materials[i].normalTexture.index, &EmbeddedTextureCache::normal_uri));
        materials_[i].orm_uri = resolve_uri(source.metallicRoughnessTexture.index,
            cached_uri(source.metallicRoughnessTexture.index, &EmbeddedTextureCache::orm_uri));
        if (materials_[i].orm_uri.empty())
            materials_[i].orm_uri = resolve_uri(model.materials[i].occlusionTexture.index,
                cached_uri(model.materials[i].occlusionTexture.index,
                    &EmbeddedTextureCache::orm_uri));

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
