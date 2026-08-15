#pragma once
#include <d3d11.h>
#include <cereal/archives/binary.hpp>
#include <cereal/types/memory.hpp>
#include <cereal/types/vector.hpp>
#include <cereal/types/set.hpp>
#include <cereal/types/unordered_map.hpp>
#include <cereal/types/string.hpp>
#include <wrl.h>
#include <directxmath.h>
#include <vector>
#include <string>
#include <filesystem>
#ifndef REPLAY_ENABLE_FBX_IMPORTER
#define REPLAY_ENABLE_FBX_IMPORTER 0
#endif

#if REPLAY_ENABLE_FBX_IMPORTER
#include <fbxsdk.h>
#endif
#include <map>
#include <unordered_map>
namespace DirectX
{
    // XMFLOAT2 のシリアライズ
    template<class T>
    void serialize(T& archive, DirectX::XMFLOAT2& v)
    {
        archive(
            cereal::make_nvp("x", v.x),
            cereal::make_nvp("y", v.y)
        );
    }

    // XMFLOAT3 のシリアライズ
    template<class T>
    void serialize(T& archive, DirectX::XMFLOAT3& v)
    {
        archive(
            cereal::make_nvp("x", v.x),
            cereal::make_nvp("y", v.y),
            cereal::make_nvp("z", v.z)
        );
    }

    // XMFLOAT4 のシリアライズ
    template<class T>
    void serialize(T& archive, DirectX::XMFLOAT4& v)
    {
        archive(
            cereal::make_nvp("x", v.x),
            cereal::make_nvp("y", v.y),
            cereal::make_nvp("z", v.z),
            cereal::make_nvp("w", v.w)
        );
    }

    // XMFLOAT4X4 (行列) のシリアライズ
    template<class T>
    void serialize(T& archive, DirectX::XMFLOAT4X4& m)
    {
        archive(
            cereal::make_nvp("_11", m._11), cereal::make_nvp("_12", m._12), cereal::make_nvp("_13", m._13), cereal::make_nvp("_14", m._14),
            cereal::make_nvp("_21", m._21), cereal::make_nvp("_22", m._22), cereal::make_nvp("_23", m._23), cereal::make_nvp("_24", m._24),
            cereal::make_nvp("_31", m._31), cereal::make_nvp("_32", m._32), cereal::make_nvp("_33", m._33), cereal::make_nvp("_34", m._34),
            cereal::make_nvp("_41", m._41), cereal::make_nvp("_42", m._42), cereal::make_nvp("_43", m._43), cereal::make_nvp("_44", m._44)
        );
    }
}
struct scene
{
    struct node
    {
        uint64_t unique_id{ 0 };
        std::string name;
        int32_t attribute{ 0 };
        int64_t parent_index{ -1 };
        template<class T>
        void serialize(T& archive) {
            archive(unique_id, name, attribute, parent_index); // 資料P.2 48行目通りに
        }
    };

    std::vector<node> nodes;
    // ★追加: scene 本体のシリアライズ
    template<class T>
    void serialize(T& archive)
    {
        archive(nodes);
    }

    int64_t indexof(uint64_t unique_id) const
    {
        int64_t index{ 0 };
        for (const node& node : nodes)
        {
            if (node.unique_id == unique_id)
            {
                return index;
            }
            ++index;
        }
        return -1;
    }
};

// UNIT24 手順2: skeleton 構造体の定義 [cite: 83-142]
struct skeleton
{
    struct bone
    {
        uint64_t unique_id{ 0 };
        std::string name;
        int64_t parent_index{ -1 };
        int64_t node_index{ 0 };
        DirectX::XMFLOAT4X4 offset_transform{ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };

        bool is_orphan() const { return parent_index < 0; };

       // UNIT30 手順4: bone のシリアライズ [cite: 138-146]
            template<class T>
        void serialize(T& archive)
        {
            archive(unique_id, name, parent_index, node_index, offset_transform);
        }
    };

    std::vector<bone> bones; // [cite: 115]

    int64_t indexof(uint64_t unique_id) const // [cite: 117]
    {
        int64_t index{ 0 }; // [cite: 121]
        for (const bone& bone : bones) // [cite: 123]
        {
            if (bone.unique_id == unique_id)
            {
                return index; // [cite: 131]
            }
            ++index; // [cite: 135]
        }
        return -1; // [cite: 139]
    }
    template<class T>
    void serialize(T& archive)
    {
        archive(bones);
    }
};

class skinned_mesh
{
public:
    static const int MAX_BONE_INFLUENCES{ 4 }; // 最大影響ボーン数

    struct vertex
    {
        DirectX::XMFLOAT3 position;
        DirectX::XMFLOAT3 normal;
        DirectX::XMFLOAT4 tangent{ 1, 0, 0, 1 };
        DirectX::XMFLOAT2 texcoord;
        // UNIT22 追加項目
        float bone_weights[MAX_BONE_INFLUENCES]{ 1, 0, 0, 0 }; // ウェイト
        uint32_t bone_indices[MAX_BONE_INFLUENCES]{}; // ボーン番号
        // glTF Morph Target 0。cerealへは書かないため既存FBXキャッシュの
        // バイナリ形式は変わらず、読み込んだFBXでは常に0になる。
        DirectX::XMFLOAT3 morph_position{};
        DirectX::XMFLOAT3 morph_normal{};

        template<class T>
        void serialize(T& archive)
        {
            archive(position, normal, tangent, texcoord, bone_weights, bone_indices);
        }
    };

    static const int MAX_BONES{ 256 };
    // skinned_mesh.h
    //U25
    struct animation
    {
        std::string name;                // アニメーション名 [cite: 16]
        float sampling_rate{ 0 };       // サンプリングレート [cite: 18]

        struct keyframe
        {
            struct node
            {
                DirectX::XMFLOAT4X4 global_transform{ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
                // 追加項目 [cite: 323, 325]
                DirectX::XMFLOAT3 scaling{ 1, 1, 1 };      // スケーリング [cite: 327]
                DirectX::XMFLOAT4 rotation{ 0, 0, 0, 1 }; // 回転（クォータニオン） [cite: 329]
                DirectX::XMFLOAT3 translation{ 0, 0, 0 }; // 平行移動
                float morph_weight = 0.0f;

                template<class T>
                void serialize(T& archive)
                {
                    archive(global_transform, scaling, rotation, translation);
                }
            };
            std::vector<node> nodes;

            template<class T>
            void serialize(T& archive)
            {
                archive(nodes);
            }
        };
        std::vector<keyframe> sequence; // キーフレームの配列（アニメーション本体） 

        template<class T>
        void serialize(T& archive)
        {
            archive(name, sampling_rate, sequence);
        }
    };
    std::vector<animation> animation_clips;

    struct constants
    {
        DirectX::XMFLOAT4X4 world;
        DirectX::XMFLOAT4 material_color;
        // UNIT23 手順2 *6 追加 [cite: 18-19]
        DirectX::XMFLOAT4X4 bone_transforms[MAX_BONES]{ { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 } };
        // GLB 内蔵 Material を使う描画だけが参照する。w=0 の既存
        // FBX/.cereal 経路では Shader 側も従来の b9/t0/t1 をそのまま使う。
        DirectX::XMFLOAT4 gltf_pbr{ 0.0f, 0.55f, 1.0f, 0.0f };
        DirectX::XMFLOAT4 gltf_emissive{ 0.0f, 0.0f, 0.0f, 0.0f };
        DirectX::XMFLOAT4 gltf_alpha{ 0.0f, 0.5f, 0.0f, 1.0f };
        DirectX::XMFLOAT4 gltf_morph{}; // x=target0 weight
    };

    // TAAのモーションベクター用に、前フレームのボーン姿勢だけを載せる定数バッファ。
    // 16KBあるためG-Bufferパス(モーションベクターを書く描画)だけでバインドする。
    struct motion_bone_constants
    {
        DirectX::XMFLOAT4X4 bone_transforms[MAX_BONES]{
            { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 } };
    };

    struct mesh
    {
        uint64_t unique_id{ 0 };
        std::string name;
        int64_t node_index{ 0 };
        std::vector<vertex> vertices;
        std::vector<uint32_t> indices;

        // モーションベクター用の前フレーム姿勢。フレームIDで多重更新を防ぐ。
        DirectX::XMFLOAT4X4 previous_world{ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
        std::vector<DirectX::XMFLOAT4X4> previous_bone_transforms;
        unsigned long long motion_frame_id{ 0 };
        bool motion_history_valid{ false };


        DirectX::XMFLOAT3 bounding_box[2]
        {
            { +D3D11_FLOAT32_MAX, +D3D11_FLOAT32_MAX, +D3D11_FLOAT32_MAX },
            { -D3D11_FLOAT32_MAX, -D3D11_FLOAT32_MAX, -D3D11_FLOAT32_MAX }
        };

        struct subset
        {
            uint64_t material_unique_id{ 0 }; // マテリアル識別用ID
            std::string material_name;      // マテリアル名 [cite: 14]
            uint32_t start_index_location{ 0 }; // 描画開始インデックス [cite: 17]
            uint32_t index_count{ 0 };        // インデックス数 [cite: 19]

            template<class T>
            void serialize(T& archive) 
            {
                archive(material_unique_id, material_name, start_index_location, index_count); // 資料P.3 75行目通りに
            }

        };
        std::vector<subset> subsets; // メッシュ内のサブセット配列 [cite: 21]

        // UNIT24 手順3: メンバ変数(bind_pose)を追加する 
        skeleton bind_pose;

  
    public:
        // ユニット21
        DirectX::XMFLOAT4X4 default_global_transform{ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
        float default_morph_weight = 0.0f;
        template<class T>
        void serialize(T& archive)
        {
            archive(unique_id, name, node_index, subsets, default_global_transform,
                bind_pose, bounding_box, vertices, indices);
        }
    private:
        Microsoft::WRL::ComPtr<ID3D11Buffer> vertex_buffer;
        Microsoft::WRL::ComPtr<ID3D11Buffer> index_buffer;
        friend class skinned_mesh;

    };

    std::vector<mesh> meshes;

    struct material
    {
        uint64_t unique_id{ 0 };
        std::string name;

        DirectX::XMFLOAT4 Ka{ 0.2f, 0.2f, 0.2f, 1.0f };
        DirectX::XMFLOAT4 Kd{ 0.8f, 0.8f, 0.8f, 1.0f };
        DirectX::XMFLOAT4 Ks{ 1.0f, 1.0f, 1.0f, 1.0f };

        std::string texture_filenames[4];
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> shader_resource_views[4];
        template<class T>
        void serialize(T& archive)
        {
            archive(unique_id, name, Ka, Kd, Ks, texture_filenames);
        }
    };

    std::unordered_map<uint64_t, material> materials;

    // 既存 cereal の material レイアウトを変えると全 FBX キャッシュが壊れる。
    // glTF 固有値は別表に置き、GLB の直接 import 時だけ使う。
    struct gltf_material_info
    {
        float metallic = 1.0f;
        float roughness = 1.0f;
        float occlusion = 1.0f;
        DirectX::XMFLOAT3 emissive{ 0.0f, 0.0f, 0.0f };
        float emissive_strength = 1.0f;
        int alpha_mode = 0; // OPAQUE=0, MASK=1, BLEND=2
        float alpha_cutoff = 0.5f;
        bool double_sided = false;
        bool unlit = false;
        float normal_scale = 1.0f;
    };

    const gltf_material_info* GltfMaterial(uint64_t unique_id) const noexcept;
    bool IsGltf() const noexcept { return imported_gltf_; }
    bool HasAnimations() const noexcept { return !animation_clips.empty(); }
    bool HasDoubleSidedMaterials() const noexcept;

#if REPLAY_ENABLE_FBX_IMPORTER
    void fetch_materials(FbxScene* fbx_scene, std::unordered_map<uint64_t, material>& materials);
#endif

private:
    Microsoft::WRL::ComPtr<ID3D11VertexShader> vertex_shader;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> pixel_shader;
    Microsoft::WRL::ComPtr<ID3D11InputLayout> input_layout;
    Microsoft::WRL::ComPtr<ID3D11Buffer> constant_buffer;
    // モーションベクター用: b6=前フレームのワールド/ビュー射影、b8=前フレームのボーン。
    Microsoft::WRL::ComPtr<ID3D11Buffer> motion_object_constant_buffer;
    Microsoft::WRL::ComPtr<ID3D11Buffer> motion_bone_constant_buffer;

public:
    skinned_mesh(ID3D11Device* device, const char* fbx_filename,
       bool triangulate = false, float sampling_rate = 0,
       bool create_device_resources = true);
    skinned_mesh(ID3D11Device* device, const std::filesystem::path& filename,
       bool triangulate = false, float sampling_rate = 0,
       bool create_device_resources = true);
    virtual ~skinned_mesh() = default;

    // UNIT24 手順4: skeleton 情報を抽出する関数 [cite: 145-146]
#if REPLAY_ENABLE_FBX_IMPORTER
    void fetch_skeleton(FbxMesh* fbx_mesh, skeleton& bind_pose);
    //U25
    void fetch_animations(FbxScene* fbx_scene, std::vector<animation>& animation_clips
    , float sampling_rate);
    //U28
    void fetch_meshes(FbxScene* fbx_scene, std::vector<mesh>& meshes);
#endif
    bool append_animations(const char* animation_filename, float sampling_rate);

    void blend_animations(const animation::keyframe* keyframes[6],
        float factor, animation::keyframe& keyframe);
    //U27
    void update_animation(animation::keyframe& keyframe);
    void create_com_objects(ID3D11Device* device, const char* fbx_filename);
    void render(ID3D11DeviceContext* immediate_context,
        const DirectX::XMFLOAT4X4& world, const DirectX::XMFLOAT4& material_color,
        const animation::keyframe* keyframe,
        ID3D11PixelShader* alternative_pixel_shader = nullptr,
        ID3D11VertexShader* alternative_vertex_shader = nullptr,
        ID3D11InputLayout* alternative_input_layout = nullptr,
        bool bind_pixel_shader = true, // 引数追加
        // trueのときだけ前フレーム姿勢をVSへ載せ、履歴を更新する。
        // G-Bufferパスで1回だけ渡すこと(複数回渡すと前フレーム姿勢が壊れる)。
        bool write_motion_vectors = false,
        // Material Asset が明示されていない GLB だけ true。
        // false は従来 FBX と完全に同じ Shader 経路を保つ。
        bool use_embedded_gltf_materials = false);
protected:
    scene scene_view;
private:
    bool import_gltf(ID3D11Device* device, const std::filesystem::path& filename,
        float sampling_rate);
    std::unordered_map<uint64_t, gltf_material_info> gltf_materials_;
    bool imported_gltf_ = false;
public:
	// 追加: デバイスリソースの明示解放
	void release_device_resources();
};
