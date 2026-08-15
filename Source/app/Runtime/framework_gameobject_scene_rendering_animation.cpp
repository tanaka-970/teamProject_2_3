// GameObject / Component 基盤のうち「Mesh cache / Animation 解決」を持つ。
// Cache解放と keyframe 解決の関数本体はそのまま移動している。
#include "framework.h"

#include "gltf_model.h"
#include "skinned_mesh.h"

#include "../../RePlayEngine/Components/Camera/CameraComponent.h"
#include "../../RePlayEngine/Components/Camera/CameraTargetComponent.h"
#include "../../RePlayEngine/Components/Camera/FollowTargetComponent.h"
#include "../../RePlayEngine/Components/Motion/MotionPlayerComponent.h"
#include "../../RePlayEngine/Components/Core/PropertyLinkComponent.h"
#include "../../RePlayEngine/Components/UI/UIEffectStackComponent.h"
#include "../../RePlayEngine/Components/UI/UISpriteAnimatorComponent.h"
#include "../../RePlayEngine/Components/UI/UITextComponent.h"
#include "../../RePlayEngine/Components/Rendering/LightComponents.h"
#include "../../RePlayEngine/Components/Rendering/MeshRendererComponent.h"
#include "../../RePlayEngine/Components/Rendering/MaterialOverrideDynamicProperties.h"
#include "../../RePlayEngine/Components/Rendering/PrimitiveMeshRendererComponent.h"
#include "../../RePlayEngine/Components/Rendering/SkinnedMeshRendererComponent.h"
#include "../../RePlayEngine/Components/Landscape/LandscapeComponent.h"
#include "../../RePlayEngine/Components/Landscape/LandscapeRendererComponent.h"
#include "../../RePlayEngine/Components/Landscape/LandscapeColliderComponent.h"
#include "../../RePlayEngine/Rendering/Shaders/BuiltInShaders.h"
#include "../../RePlayEngine/Rendering/ShaderStack/BuiltInShaderLayers.h"
#include "../../RePlayEngine/Object/Registry/BuiltInComponents.h"
#include "../../RePlayEngine/Project/ProjectSettingsSerializer.h"
#include "../../RePlayEngine/Rendering/Adapter/SceneRenderCollector.h"
#include "../../RePlayEngine/Motion/MotionBindingResolver.h"
#include "../../RePlayEngine/Motion/MotionEvaluator.h"
#include "../../RePlayEngine/UI/UILayout.h"
#include "../../RePlayEngine/Runtime/Events/EventBus.h"
#include "../../RePlayEngine/Scene/Serialization/PrefabSerializer.h"
#include "../../RePlayEngine/Scene/Serialization/SceneData.h"
#include "../../RePlayEngine/Scene/Serialization/SceneSerializer.h"
#include "../../RePlayEngine/Scripting/CSharp/CSharpScriptBackend.h"
#include "../../RePlayEngine/Scripting/Core/ScriptComponent.h"
#include "../../RePlayEngine/Scripting/Core/ScriptRuntime.h"
#include "../../RePlayEngine/Scripting/Core/ScriptTypeCatalog.h"
#include "../../RePlayEngine/Scripting/Core/ScriptTypes.h"
#include "../../game/Behaviours/ValidationBehaviours.h"

#include <commdlg.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>


void framework::clear_object_mesh_cache() noexcept
{
    object_mesh_cache.clear();
    // GameObject と Asset Browser が共有するGLB実体を、DeviceのLive Object確認前に解放する。
    stage_gltf_model.reset();
    gltf_model_cache.Clear();
    builtin_primitive_mesh_cache.clear();
    landscape_gpu_mesh_cache.clear();
    object_mesh_failures.clear();
}

void framework::clear_object_material_cache() noexcept
{
    object_material_cache.clear();
    object_material_failures.clear();
    object_shader_lighting_failures.clear();
}

const skinned_mesh::animation::keyframe* framework::resolve_object_keyframe(
    skinned_mesh& mesh, int clip_index, float animation_time, bool loop) const
{
    if (clip_index < 0) return nullptr;
    if (mesh.animation_clips.empty()) return nullptr;
    if (clip_index >= static_cast<int>(mesh.animation_clips.size())) return nullptr;

    const skinned_mesh::animation& clip = mesh.animation_clips.at(
        static_cast<std::size_t>(clip_index));
    if (clip.sequence.empty()) return nullptr;

    const float sampling_rate = clip.sampling_rate > 0.0f ? clip.sampling_rate : 60.0f;
    const float duration = static_cast<float>(clip.sequence.size()) / sampling_rate;

    // クリップ長を知っている Renderer 側で Loop / Clamp を確定する。
    // Animator は clip index と時間だけを持ち、mesh の実データへ依存しない。
    float time = animation_time;
    if (duration > 0.0f)
    {
        if (loop)
        {
            time = std::fmod(time, duration);
            if (time < 0.0f) time += duration;
        }
        else
        {
            time = (std::max)(0.0f, (std::min)(duration, time));
        }
    }

    int frame = static_cast<int>(time * sampling_rate);
    if (frame < 0) frame = 0;
    if (frame >= static_cast<int>(clip.sequence.size()))
    {
        frame = static_cast<int>(clip.sequence.size()) - 1;
    }
    return &clip.sequence.at(static_cast<std::size_t>(frame));
}

const skinned_mesh::animation::keyframe* framework::resolve_render_item_keyframe(
    skinned_mesh& mesh, const ReplayEngine::Rendering::RenderItem& item,
    skinned_mesh::animation::keyframe& blended_keyframe) const
{
    if (!item.skinned) return nullptr;

    const skinned_mesh::animation::keyframe* current = resolve_object_keyframe(
        mesh, item.clip_index, item.animation_time, item.animation_loop);
    if (current == nullptr || item.previous_clip_index < 0 ||
        item.animation_blend_factor >= 1.0f)
    {
        return current;
    }

    const skinned_mesh::animation::keyframe* previous = resolve_object_keyframe(
        mesh, item.previous_clip_index, item.previous_animation_time,
        item.previous_animation_loop);
    if (previous == nullptr || previous->nodes.size() != current->nodes.size())
        return current;

    const skinned_mesh::animation::keyframe* sources[2]{ previous, current };
    mesh.blend_animations(sources,
        (std::max)(0.0f, (std::min)(1.0f, item.animation_blend_factor)),
        blended_keyframe);
    mesh.update_animation(blended_keyframe);
    return &blended_keyframe;
}
