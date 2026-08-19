#pragma once

#include <d3d11.h>
#include <wrl.h>
#include <DirectXMath.h>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>
#include "../../RePlayEngine/Physics/SphereCast.h"

// glTF 2.0�^GLB�̐ÓI���b�V�����Y�B�]���̃��^�f�[�^�����̉�������u��������B
// �X�L���ƃA�j���[�V�����͔\�͏��Ƃ��ĕێ����A���s���Đ��͕ʃR���|�[�l���g�ň����B
class gltf_model
{
public:
    explicit gltf_model(ID3D11Device* device, const std::string& filename);
    static void SetCacheRoot(std::filesystem::path root);
    // LOD生成スレッドを回収してから破棄する。
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

    // write_motion_vectors ��G-Buffer�p�X�ł̂� true �ɂ���B
    // �O�t���[���̃��[���h�s���VS(b6)�֍ڂ��A������1�t���[���i�߂�B
    void render(ID3D11DeviceContext* context,
        const DirectX::XMFLOAT4X4& world,
        const DirectX::XMFLOAT4& tint = { 1, 1, 1, 1 },
        ID3D11PixelShader* alternative_pixel_shader = nullptr,
        bool write_motion_vectors = false,
        // 深度プリパス用。ピクセルシェーダーとテクスチャを外して深度だけ描く。
        bool depth_only = false);

private:
    static const std::filesystem::path& CacheRoot();

    struct Vertex
    {
        DirectX::XMFLOAT3 position{};
        DirectX::XMFLOAT3 normal{ 0, 1, 0 };
        DirectX::XMFLOAT2 texcoord{};
    };

    // ディスクキャッシュ用のCPU側データ。保存が済んだら捨てる。
    struct LodCacheEntry
    {
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
    };

    // 自動LODの1段分。LOD0が原型で、番号が上がるほど粗い。
    struct LodLevel
    {
        Microsoft::WRL::ComPtr<ID3D11Buffer> vertex_buffer;
        Microsoft::WRL::ComPtr<ID3D11Buffer> index_buffer;
        uint32_t index_count = 0;
        uint32_t vertex_count = 0;
    };

    struct Primitive
    {
        Microsoft::WRL::ComPtr<ID3D11Buffer> vertex_buffer;
        Microsoft::WRL::ComPtr<ID3D11Buffer> index_buffer;
        DirectX::XMFLOAT4X4 node_transform{};
        uint32_t index_count = 0;
        uint32_t vertex_count = 0;
        int material = -1;
        // 視錐台カリング用のAABB。node_transform適用後(モデル空間)で保持する。
        DirectX::XMFLOAT3 bounds_minimum{ 0.0f, 0.0f, 0.0f };
        DirectX::XMFLOAT3 bounds_maximum{ 0.0f, 0.0f, 0.0f };
        // LOD1以降。空ならLOD0のみ。
        std::vector<LodLevel> lods;
        // LOD生成用に保持する原型データ。生成後に解放する。
        std::vector<Vertex> source_vertices;
        std::vector<uint32_t> source_indices;
        // ディスクキャッシュへ書き出すためのCPU側コピー。保存後に解放する。
        std::vector<LodCacheEntry> lod_cache;
    };

    struct Material
    {
        DirectX::XMFLOAT4 base_color{ 1, 1, 1, 1 };
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> base_color_texture;
        // 法線マップと glTF の ORM (R=Occlusion, G=Roughness, B=Metalness)。
        // G-Bufferのピクセルシェーダーが t1 / t2 で受け取る。
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> normal_texture;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> occlusion_roughness_metalness_texture;
        // キャッシュ経路ではglTFを読まないため、画像ファイルの場所を持っておく。
        // 空文字なら「テクスチャ無し」。
        std::string base_color_uri;
        std::string normal_uri;
        std::string orm_uri;
    };

    struct Constants
    {
        DirectX::XMFLOAT4X4 world;
        DirectX::XMFLOAT4 material_color;
    };

    bool Load(ID3D11Device* device, const std::string& filename);
    // シェーダー/入力レイアウト/定数バッファ。glTF経路とキャッシュ経路で共有。
    bool PrepareDeviceResources(ID3D11Device* device);
    // 各プリミティブのLODをQEMで生成する。プリミティブ単位で並列に走らせる。
    // ロードを止めないよう、キャッシュが無い初回はバックグラウンドで実行する。
    void BuildLods(ID3D11Device* device);
    // メッシュのディスクキャッシュ。glTF解析(3.4秒)とジオメトリ構築を丸ごと飛ばす。
    // 頂点・インデックス・AABB・マテリアル参照・LODを1ファイルにまとめる。
    std::filesystem::path MeshCachePath(const std::string& filename) const;
    bool LoadMeshCache(ID3D11Device* device, const std::string& filename);
    bool SaveMeshCache(const std::string& filename) const;
    // キャッシュ経路でテクスチャを読む。URIはglTFからの相対パス。
    void LoadTexturesFromUris(ID3D11Device* device, const std::string& gltf_filename);

    // LODのディスクキャッシュ。QEMは重いので2回目以降は読むだけにする。
    std::filesystem::path LodCachePath() const;
    bool LoadLodCache(ID3D11Device* device);
    bool SaveLodCache() const;
    // 生成した頂点/インデックスからGPUバッファを作る。
    bool CreateLodBuffers(ID3D11Device* device, LodLevel& level,
        const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices);

    std::vector<Primitive> primitives_;
    std::vector<Material> materials_;
    std::vector<ReplayEngine::Physics::Triangle> collision_triangles_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> white_texture_;
    // Normal未指定時にnullをSampleすると(-1,-1,-1)として扱われ、
    // モデル全体の法線が壊れるため、接空間の無変形Normalを共有する。
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> neutral_normal_texture_;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> vertex_shader_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> pixel_shader_;
    Microsoft::WRL::ComPtr<ID3D11InputLayout> input_layout_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> constant_buffer_;
    // TAA�̃��[�V�����x�N�^�[�p: b6=�O�t���[���̃��[���h/�r���[�ˉe�B
    Microsoft::WRL::ComPtr<ID3D11Buffer> motion_object_constant_buffer_;
    std::vector<DirectX::XMFLOAT4X4> previous_primitive_worlds_;
    unsigned long long motion_frame_id_{ 0 };
    bool motion_history_valid_{ false };
    // --- 自動LODの生成状態 -------------------------------------------------
    // LODが揃うまではLOD0で描く。lods_ready_ が true になった後は
    // primitives_[].lods は変更されないため、読み取り側はロック不要。
    LoadTimings timings_{};
    std::atomic<bool> lods_ready_{ false };
    std::thread lod_thread_;
    std::string source_filename_;
    std::string error_;
    bool loaded_ = false;
    bool has_skins_ = false;
    bool has_animations_ = false;
};
