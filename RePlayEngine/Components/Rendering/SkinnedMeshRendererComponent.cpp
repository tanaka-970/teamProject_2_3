#include "SkinnedMeshRendererComponent.h"

#include "AnimatorComponent.h"
#include "MaterialOverrideDynamicProperties.h"
#include "../../Object/GameObject/GameObject.h"

namespace ReplayEngine::Components
{
    namespace
    {
        // 既存 framework_render.cpp の fbx_coordinate_transform と同じ行列。
        // FBX の座標系を RePlayEngine の座標系へ合わせる。
        DirectX::XMMATRIX FbxCoordinateTransform() noexcept
        {
            const DirectX::XMFLOAT4X4 matrix
            {
                -1.0f, 0.0f,  0.0f, 0.0f,
                 0.0f, 0.0f, -1.0f, 0.0f,
                 0.0f, 1.0f,  0.0f, 0.0f,
                 0.0f, 0.0f,  0.0f, 1.0f
            };
            return DirectX::XMLoadFloat4x4(&matrix);
        }
    }

    const std::vector<Reflection::PropertyDesc>*
        SkinnedMeshRendererComponent::DynamicProperties() const noexcept
    {
        return MaterialOverrideDynamicProperties(*this);
    }

    void SkinnedMeshRendererComponent::PrepareMaterialMotion(
        const Rendering::MaterialAsset* material,
        const Rendering::ShaderPropertySchema* schema)
    {
        PrepareMaterialMotionProperties(*this, material, schema);
    }

    void SkinnedMeshRendererComponent::OnMotionPropertyApplied(const char* property_name)
    {
        MarkMaterialMotionProperty(*this, property_name);
    }

    bool SkinnedMeshRendererComponent::BuildRenderItem(const Core::GameObject& owner,
        Rendering::RenderItem& out) const
    {
        if (!ShouldRender()) return false;

        const Core::Transform& transform = owner.GetTransform();

        // 合成順:
        //   world = FbxC * ( Scale * Rotation(visual + object) * Translation )
        // 見た目の補正はここで足す。GameObject の Transform は論理的な向きのまま。
        const DirectX::XMFLOAT3 position = transform.LocalPosition();
        const DirectX::XMFLOAT3 euler = transform.LocalRotationEuler();
        const DirectX::XMFLOAT3 scale = transform.LocalScale();

        constexpr float radians_per_degree = DirectX::XM_PI / 180.0f;

        // 縮尺は GameObject の Scale × モデル補正倍率。
        const DirectX::XMMATRIX scaling = DirectX::XMMatrixScaling(
            scale.x * local_scale_multiplier.x,
            scale.y * local_scale_multiplier.y,
            scale.z * local_scale_multiplier.z);

        const DirectX::XMMATRIX rotation = DirectX::XMMatrixRotationRollPitchYaw(
            visual_rotation_offset.x * radians_per_degree + euler.x,
            visual_rotation_offset.y * radians_per_degree + euler.y,
            visual_rotation_offset.z * radians_per_degree + euler.z);

        const DirectX::XMMATRIX translation = DirectX::XMMatrixTranslation(
            position.x + local_position_offset.x,
            position.y + local_position_offset.y,
            position.z + local_position_offset.z);

        DirectX::XMMATRIX world = scaling * rotation * translation;

        // 親がいる場合は親のワールド行列を後ろから掛ける。
        // 補正は自分のローカル姿勢に対してのみ効かせたいので、この順序になる。
        world = world * transform.ParentWorldMatrix();

        if (apply_fbx_coordinate_transform) world = FbxCoordinateTransform() * world;

        DirectX::XMStoreFloat4x4(&out.world, world);

        out.owner = owner.ID();
        out.mesh_asset = mesh_asset;
        out.material_asset = material_asset;
        out.material_override = material_override;
        out.material_motion_fixed_mask = material_motion_state.fixed_active_mask;
        out.material_motion_properties = material_motion_state.active_values;
        out.tint = tint;
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
        out.skinned = true;

        // Animator があればクリップと時刻を運ぶ。無ければ Renderer 側の現在値を維持する。
        if (const auto* animator = owner.GetComponent<AnimatorComponent>())
        {
            if (animator->ActiveInHierarchy())
            {
                out.clip_index = animator->CurrentClip();
                out.animation_time = animator->AnimationTime();
                out.animation_playing = true;
                out.animation_loop = animator->CurrentLoop();
                out.previous_clip_index = animator->PreviousClip();
                out.previous_animation_time = animator->PreviousAnimationTime();
                out.previous_animation_loop = animator->PreviousLoop();
                out.animation_blend_factor = animator->BlendFactor();
            }
            else
            {
                // Animator を無効にしても描画は続く。姿勢が固定されるだけ。
                out.clip_index = animator->CurrentClip();
                out.animation_time = animator->AnimationTime();
                out.animation_playing = false;
                out.animation_loop = animator->CurrentLoop();
                out.previous_clip_index = animator->PreviousClip();
                out.previous_animation_time = animator->PreviousAnimationTime();
                out.previous_animation_loop = animator->PreviousLoop();
                out.animation_blend_factor = animator->BlendFactor();
            }
        }
        else
        {
            out.clip_index = -1;
            out.animation_time = 0.0f;
            out.animation_playing = false;
        }

        return true;
    }
}
