#include "PrimitiveMeshRendererComponent.h"

#include "MaterialOverrideDynamicProperties.h"
#include "../../Object/GameObject/GameObject.h"

namespace ReplayEngine::Components
{
    const std::vector<Reflection::PropertyDesc>*
        PrimitiveMeshRendererComponent::DynamicProperties() const noexcept
    {
        return MaterialOverrideDynamicProperties(*this);
    }

    void PrimitiveMeshRendererComponent::PrepareMaterialMotion(
        const Rendering::MaterialAsset* material,
        const Rendering::ShaderPropertySchema* schema)
    {
        PrepareMaterialMotionProperties(*this, material, schema);
    }

    void PrimitiveMeshRendererComponent::OnMotionPropertyApplied(const char* property_name)
    {
        MarkMaterialMotionProperty(*this, property_name);
    }

    const char* PrimitiveMeshRendererComponent::BuiltinAssetId() const noexcept
    {
        switch (static_cast<PrimitiveType>(primitive_type))
        {
        case Plane:    return "builtin:plane";
        case Cube:     return "builtin:cube";
        case Sphere:   return "builtin:sphere";
        case Capsule:  return "builtin:capsule";
        case Cylinder: return "builtin:cylinder";
        case Quad:     return "builtin:quad";
        default:       return nullptr;
        }
    }

    bool PrimitiveMeshRendererComponent::BuildRenderItem(const Core::GameObject& owner,
        Rendering::RenderItem& out) const
    {
        if (!ShouldRender()) return false;

        const char* builtin_id = BuiltinAssetId();
        if (builtin_id == nullptr) return false;

        out.owner = owner.ID();
        out.mesh_asset = builtin_id;
        out.material_asset = material_asset;
        out.world = owner.GetTransform().WorldMatrixFloat4x4();
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
        out.double_sided =
            primitive_type == static_cast<int>(Plane) ||
            primitive_type == static_cast<int>(Quad);
        out.skinned = false;
        out.clip_index = -1;
        out.animation_time = 0.0f;
        out.animation_playing = false;
        return true;
    }
}
