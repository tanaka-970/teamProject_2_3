#include "PrimitiveMeshRendererComponent.h"

#include "../../Object/GameObject/GameObject.h"

namespace ReplayEngine::Components
{
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
