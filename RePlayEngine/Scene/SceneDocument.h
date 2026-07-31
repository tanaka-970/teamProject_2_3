#pragma once

#include <DirectXMath.h>
#include "../Rendering/Materials/CharacterMaterialProfile.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ReplayEngine::Scene
{
    using EntityId = std::uint64_t;

    struct TransformData
    {
        DirectX::XMFLOAT3 position{ 0.0f, 0.0f, 0.0f };
        DirectX::XMFLOAT3 rotation{ 0.0f, 0.0f, 0.0f };
        DirectX::XMFLOAT3 scale{ 1.0f, 1.0f, 1.0f };
    };

    struct ModelRendererData
    {
        struct ShaderLayerData
        {
            std::uint32_t type = 3;
            std::uint32_t blend = 0;
            bool enabled = true;
            float opacity = 0.45f;
            float strength = 1.0f;
            float parameter = 6.0f;
            DirectX::XMFLOAT4 tint{ 1.0f, 1.0f, 1.0f, 1.0f };
        };

        std::string asset_guid;
        std::string asset_name;
        DirectX::XMFLOAT4 tint{ 1.0f, 1.0f, 1.0f, 1.0f };
        int shading_model = 1;
        float pixelate_grid = 6.0f;
        float pixelate_strength = 1.0f;
        bool outline = false;
        bool visible = true;
        std::vector<ShaderLayerData> shader_layers;
        std::optional<ReplayEngine::Rendering::CharacterMaterialProfile> character_material;
    };

    struct MeshColliderData
    {
        std::string cooked_path;
        std::uint64_t triangle_count = 0;
        float cell_size = 4.0f;
        bool enabled = true;
    };

    struct GravityData
    {
        DirectX::XMFLOAT3 direction{ 0.0f, -1.0f, 0.0f };
        float strength = 9.80665f;
        float scale = 1.0f;
        float terminal_speed = 55.0f;
        bool use_terminal_speed = true;
        bool enabled = true;
    };

    struct AnimationData
    {
        int clip_index = 0;
        float speed = 1.0f;
        bool loop = true;
        bool playing = true;
    };

    // 2D UI要素。将来のUIアニメーション(AEライク)ツールの土台。
    //
    // 設計意図:
    //   - Transformを anchor / position / scale / rotation / opacity へ分解する。
    //     sizeとscaleを分けているのは、親子でscaleを継承させるため。
    //     既存UIクラスのように実サイズ直指定だと階層変形が成立しない。
    //   - anchorは0..1の正規化座標。回転とスケールの基準点になる。
    //     AEのアンカーポイントに相当し、これが無いと端を軸にした開閉ができない。
    //   - 親子はEntityIdで参照する。ポインタで持つとvector再確保で壊れるため。
    //   - 後からキーフレームを載せられるよう、アニメート対象の値は
    //     すべてこの構造体の中に平坦に置いてある。
    struct UIElementData
    {
        // 親のEntityId。0ならルート(キャンバス直下)。
        EntityId parent = 0;
        // 同じ親の中での描画順。小さいほど奥。
        int order = 0;

        // --- Transform (アニメート対象) ---
        DirectX::XMFLOAT2 anchor{ 0.5f, 0.5f };    // 0..1 正規化
        DirectX::XMFLOAT2 position{ 0.0f, 0.0f };  // 親空間のピクセル
        DirectX::XMFLOAT2 size{ 100.0f, 100.0f };  // 元サイズ(ピクセル)
        DirectX::XMFLOAT2 scale{ 1.0f, 1.0f };     // 倍率。親から継承する
        float rotation = 0.0f;                     // degree
        float opacity = 1.0f;                      // 親から継承する

        // --- 見た目 ---
        // 画像はAssetDatabaseのGUIDで参照する。生パスを持つと
        // ファイル移動で壊れ、キャッシュ共有もできない。
        std::string sprite_guid;
        std::string sprite_name;
        DirectX::XMFLOAT4 color{ 1.0f, 1.0f, 1.0f, 1.0f };
        // UV切り抜き (x, y, w, h)。ピクセル単位。w<=0なら画像全体。
        DirectX::XMFLOAT4 uv_rect{ 0.0f, 0.0f, 0.0f, 0.0f };
        // 0=テクスチャのアルファ, 1=白抜き, 2=黒抜き (sprite::AlphaModeと対応)
        int alpha_mode = 0;
        bool visible = true;
    };

    struct SceneEntity
    {
        EntityId id = 0;
        std::string name{ "Entity" };
        std::string identifier{ "entity_001" };
        bool active = true;
        std::optional<TransformData> transform;
        std::optional<ModelRendererData> model_renderer;
        std::optional<MeshColliderData> mesh_collider;
        std::optional<GravityData> gravity;
        std::optional<AnimationData> animation;
        std::optional<UIElementData> ui_element;
    };

    class SceneDocument final
    {
    public:
        SceneEntity& CreateEntity(std::string name = "Entity");
        SceneEntity& ImportEntity(const SceneEntity& source);
        bool DestroyEntity(EntityId id);
        SceneEntity* Find(EntityId id) noexcept;
        const SceneEntity* Find(EntityId id) const noexcept;
        std::string MakeUniqueIdentifier(const std::string& seed, EntityId except_id = 0) const;
        bool SetIdentifier(EntityId id, const std::string& desired);
        void SetSceneName(std::string name);
        void Clear() noexcept;

        std::vector<SceneEntity>& Entities() noexcept { return entities_; }
        const std::vector<SceneEntity>& Entities() const noexcept { return entities_; }
        EntityId NextId() const noexcept { return next_id_; }
        const std::string& SceneName() const noexcept { return scene_name_; }
        const std::string& SceneIdentifier() const noexcept { return scene_identifier_; }
        void RebuildNextId();

    private:
        std::vector<SceneEntity> entities_;
        EntityId next_id_ = 1;
        std::string scene_name_{ "Main" };
        std::string scene_identifier_{ "main" };
    };
}
