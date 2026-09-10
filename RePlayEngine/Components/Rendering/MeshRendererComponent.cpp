#include "MeshRendererComponent.h"

#include "MaterialOverrideDynamicProperties.h"
#include "../../Object/GameObject/GameObject.h"

namespace ReplayEngine::Components
{
    const std::vector<Reflection::PropertyDesc>*
        MeshRendererComponent::DynamicProperties() const noexcept
    {
        return MaterialOverrideDynamicProperties(*this);
    }

    void MeshRendererComponent::PrepareMaterialMotion(
        const Rendering::MaterialAsset* material,
        const Rendering::ShaderPropertySchema* schema)
    {
        PrepareMaterialMotionProperties(*this, material, schema);
    }

    void MeshRendererComponent::OnMotionPropertyApplied(const char* property_name)
    {
        MarkMaterialMotionProperty(*this, property_name);
    }

    void MeshRendererComponent::OnSerialize(Reflection::PropertyBag& output) const
    {
        SerializeMaterialSlots(*this, output);
    }

    void MeshRendererComponent::OnDeserialize(const Reflection::PropertyBag& input)
    {
        DeserializeMaterialSlots(*this, input);
    }

    bool MeshRendererComponent::BuildRenderItem(const Core::GameObject& owner,
        Rendering::RenderItem& out) const
    {
        if (!ShouldRender()) return false;

        out.owner = owner.ID();
        out.mesh_asset = mesh_asset;
        out.material_asset = material_asset;
        out.material_slot_assets = nullptr;
        out.material_slots = nullptr;
        out.material_slot_count = 0;
        const int slot_count = ClampedMaterialSlotCount(*this);
        if (slot_count > 0)
        {
            for (int index = 0; index < slot_count; ++index)
            {
                material_slot_asset_view[static_cast<std::size_t>(index)] =
                    static_cast<std::size_t>(index) < material_slots.size()
                    ? &material_slots[static_cast<std::size_t>(index)].asset : nullptr;
            }
            out.material_slot_assets = material_slot_asset_view.data();
            for (int index = 0; index < slot_count; ++index)
            {
                material_slot_view[static_cast<std::size_t>(index)] =
                    static_cast<std::size_t>(index) < material_slots.size()
                    ? &material_slots[static_cast<std::size_t>(index)] : nullptr;
            }
            out.material_slots = material_slot_view.data();
            out.material_slot_count = static_cast<std::uint8_t>(slot_count);
        }
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
        out.material_motion_fixed_mask = material_motion_state.fixed_active_mask;
        out.material_motion_properties = material_motion_state.active_values;
        out.override_material_base_color =
            (material_motion_state.fixed_active_mask & MaterialMotionBaseColor) != 0
            ? material_motion_state.driven_base_color : material_base_color;
        out.override_material_metallic =
            (material_motion_state.fixed_active_mask & MaterialMotionMetallic) != 0
            ? material_motion_state.driven_metallic : material_metallic;
        out.override_material_roughness =
            (material_motion_state.fixed_active_mask & MaterialMotionRoughness) != 0
            ? material_motion_state.driven_roughness : material_roughness;
        out.override_material_ambient_occlusion =
            (material_motion_state.fixed_active_mask & MaterialMotionAmbientOcclusion) != 0
            ? material_motion_state.driven_ambient_occlusion : material_ambient_occlusion;
        out.override_material_emissive_color =
            (material_motion_state.fixed_active_mask & MaterialMotionEmissiveColor) != 0
            ? material_motion_state.driven_emissive_color : material_emissive_color;
        out.override_material_emissive_strength =
            (material_motion_state.fixed_active_mask & MaterialMotionEmissiveStrength) != 0
            ? material_motion_state.driven_emissive_strength : material_emissive_strength;
        out.override_material_double_sided =
            (material_motion_state.fixed_active_mask & MaterialMotionDoubleSided) != 0
            ? material_motion_state.driven_double_sided : material_double_sided;
        out.shading_model = shading_model;
        out.outline = outline;
        out.cast_shadow = cast_shadow;
        out.receive_shadow = receive_shadow;
        out.shadow_alpha_clip = shadow_alpha_clip;
        out.shadow_alpha_cutoff = shadow_alpha_cutoff;
        out.rendering_layer = (std::max)(0, (std::min)(31, rendering_layer));

        // 静的メッシュなのでアニメーション情報は運ばない。
        out.skinned = false;
        out.clip_index = -1;
        out.animation_time = 0.0f;
        out.animation_playing = false;
        return true;
    }
}
