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
        out.world = owner.GetTransform().WorldMatrixFloat4x4();
        out.tint = tint;
        out.shading_model = shading_model;
        out.outline = outline;
        out.cast_shadow = cast_shadow;

        // 静的メッシュなのでアニメーション情報は運ばない。
        out.skinned = false;
        out.clip_index = -1;
        out.animation_time = 0.0f;
        out.animation_playing = false;
        return true;
    }
}
