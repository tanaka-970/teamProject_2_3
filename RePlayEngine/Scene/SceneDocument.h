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
