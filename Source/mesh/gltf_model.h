#pragma once

#include <DirectXMath.h>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>
#include "../../RePlayEngine/Physics/SphereCast.h"

// glTF 2.0�^GLB�̐ÓI���b�V�����Y�B�]���̃��^�f�[�^�����̉�������u��������B
// �X�L���ƃA�j���[�V�����͔\�͏��Ƃ��ĕێ����A���s���Đ��͕ʃR���|�[�l���g�ň����B
class gltf_model
{
public:
    explicit gltf_model(const std::string& filename);
    static void SetCacheRoot(std::filesystem::path root);
    ~gltf_model();
    gltf_model(const gltf_model&) = delete;
    gltf_model& operator=(const gltf_model&) = delete;

    // LODが使える状態かどうか。UI表示用。
    bool LodsReady() const noexcept { return lods_ready_.load(); }

    // 読み込み時間の内訳(ミリ秒)。どこが遅いのか分からないと改善できないため、
    // 段階ごとに計測して保持する。
    struct LoadTimings
    {
        double parse_ms = 0.0;          // glTF本体の構文解析 + bin読み込み
        double image_decode_ms = 0.0;    // 画像のデコード(PNG展開)
        double texture_upload_ms = 0.0;  // ミップ生成 + GPUテクスチャ作成
        double geometry_ms = 0.0;        // 頂点/インデックスバッファ作成
        double lod_cache_ms = 0.0;       // LODキャッシュの読み込み
        double total_ms = 0.0;
        int image_count = 0;
        bool lod_from_cache = false;
        bool mesh_from_cache = false;    // メッシュキャッシュが使われたか
    };
    const LoadTimings& Timings() const noexcept { return timings_; }

    bool IsLoaded() const noexcept { return loaded_; }
    bool HasSkins() const noexcept { return has_skins_; }
    bool HasAnimations() const noexcept { return has_animations_; }
    const std::string& Error() const noexcept { return error_; }
    size_t PrimitiveCount() const noexcept { return primitives_.size(); }
    // 全Primitiveを含むモデル空間AABB。描画時と同じnode_transform適用済み。
    bool ComputeBounds(DirectX::XMFLOAT3& minimum,
        DirectX::XMFLOAT3& maximum) const noexcept;
    const std::vector<ReplayEngine::Physics::Triangle>& CollisionTriangles() const noexcept
    {
        return collision_triangles_;
    }

    // API に依存しない Static Geometry の Export。DX12 移行 Backend が利用する。
    // CPU Geometry が解放済みの場合は .replaymesh Cache から同じデータを読む。
    struct StaticVertex
    {
        DirectX::XMFLOAT3 position{};
        DirectX::XMFLOAT3 normal{ 0, 1, 0 };
        DirectX::XMFLOAT2 texcoord{};
    };
    struct StaticPrimitiveInfo
    {
        DirectX::XMFLOAT4X4 node_transform{};
        DirectX::XMFLOAT4 embedded_base_color{ 1, 1, 1, 1 };
        std::filesystem::path embedded_base_color_texture;
        std::filesystem::path embedded_normal_texture;
        std::filesystem::path embedded_orm_texture;
        int material = -1;
        int alpha_mode = 0;
        float alpha_cutoff = 0.5f;
    };
    struct StaticPrimitiveExport : StaticPrimitiveInfo
    {
        std::vector<StaticVertex> vertices;
        std::vector<std::uint32_t> indices;
    };
    std::size_t StaticPrimitiveCount() const noexcept { return primitives_.size(); }
    bool StaticPrimitiveInfoAt(std::size_t index, StaticPrimitiveInfo& out) const;
    bool ExportStaticPrimitives(std::vector<StaticPrimitiveExport>& out) const;

    // write_motion_vectors ��G-Buffer�p�X�ł̂� true �ɂ���B
    // �O�t���[���̃��[���h�s���VS(b6)�֍ڂ��A������1�t���[���i�߂�B
    // アルファ抜きを宣言した Material が 1 つでもあるか。
    bool HasAlphaMaskMaterials() const noexcept;

private:
    static const std::filesystem::path& CacheRoot();

    using Vertex = StaticVertex;

    struct Primitive
    {
        DirectX::XMFLOAT4X4 node_transform{};
        uint32_t index_count = 0;
        uint32_t vertex_count = 0;
        int material = -1;
        // 視錐台カリング用のAABB。node_transform適用後(モデル空間)で保持する。
        DirectX::XMFLOAT3 bounds_minimum{ 0.0f, 0.0f, 0.0f };
        DirectX::XMFLOAT3 bounds_maximum{ 0.0f, 0.0f, 0.0f };
        // LOD生成用に保持する原型データ。生成後に解放する。
        std::vector<Vertex> source_vertices;
        std::vector<uint32_t> source_indices;
    };

    struct Material
    {
        DirectX::XMFLOAT4 base_color{ 1, 1, 1, 1 };
        // 法線マップと glTF の ORM (R=Occlusion, G=Roughness, B=Metalness)。
        // G-Bufferのピクセルシェーダーが t1 / t2 で受け取る。
        // キャッシュ経路ではglTFを読まないため、画像ファイルの場所を持っておく。
        // 空文字なら「テクスチャ無し」。
        std::string base_color_uri;
        std::string normal_uri;
        std::string orm_uri;
        // glTF の alphaMode。0=OPAQUE 1=MASK 2=BLEND。影のアルファ抜きに使う。
        int alpha_mode = 0;
        float alpha_cutoff = 0.5f;
    };

    bool Load(const std::string& filename);
    // メッシュのディスクキャッシュ。glTF解析(3.4秒)とジオメトリ構築を丸ごと飛ばす。
    // 頂点・インデックス・AABB・マテリアル参照・LODを1ファイルにまとめる。
    std::filesystem::path MeshCachePath(const std::string& filename) const;
    bool LoadMeshCache(const std::string& filename);
    bool SaveMeshCache(const std::string& filename) const;


    std::vector<Primitive> primitives_;
    std::vector<Material> materials_;
    std::vector<ReplayEngine::Physics::Triangle> collision_triangles_;
    // --- 自動LODの生成状態 -------------------------------------------------
    // LODが揃うまではLOD0で描く。lods_ready_ が true になった後は
    // primitives_[].lods は変更されないため、読み取り側はロック不要。
    LoadTimings timings_{};
    std::atomic<bool> lods_ready_{ false };
    std::string source_filename_;
    std::string error_;
    bool loaded_ = false;
    bool has_skins_ = false;
    bool has_animations_ = false;
};
