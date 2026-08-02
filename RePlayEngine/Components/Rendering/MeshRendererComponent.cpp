#include "MeshRendererComponent.h"

#include "../../Object/GameObject/GameObject.h"

namespace ReplayEngine::Components
{
    bool MeshRendererComponent::BuildRenderItem(const Core::GameObject& owner,
        Rendering::RenderItem& out) const
    {
        if (!ShouldRender()) return false;

        out.owner = owner.ID();
        out.mesh_asset = mesh_asset;
        out.material_asset = material_asset;
        constexpr float radians_per_degree = DirectX::XM_PI / 180.0f;
        const DirectX::XMMATRIX adjustment = DirectX::XMMatrixScaling(
            local_scale_multiplier.x, local_scale_multiplier.y, local_scale_multiplier.z) *
            DirectX::XMMatrixRotationRollPitchYaw(
                local_rotation_offset.x * radians_per_degree,
                local_rotation_offset.y * radians_per_degree,
                local_rotation_offset.z * radians_per_degree) *
            DirectX::XMMatrixTranslation(local_position_offset.x,
                local_position_offset.y, local_position_offset.z);
        const DirectX::XMFLOAT4X4 owner_world = owner.GetTransform().WorldMatrixFloat4x4();
        DirectX::XMStoreFloat4x4(&out.world, adjustment *
            DirectX::XMLoadFloat4x4(&owner_world));
        out.tint = tint;
        out.material_override = material_override;
        out.shading_model = shading_model;
        out.outline = outline;
        out.cast_shadow = cast_shadow;
        out.receive_shadow = receive_shadow;

        // 静的メッシュなのでアニメーション情報は運ばない。
        out.skinned = false;
        out.clip_index = -1;
        out.animation_time = 0.0f;
        out.animation_playing = false;
        return true;
    }
}
