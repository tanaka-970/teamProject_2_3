// GameObject / Component 基盤のうち「Motion Asset / Binding / Player 更新」を持つ。
// 関数本体は分割前のまま移動し、Runtime 更新の順序と分岐は変更しない。
#include "framework.h"

#include "../../RePlayEngine/Components/Camera/CameraComponent.h"
#include "../../RePlayEngine/Components/Camera/CameraTargetComponent.h"
#include "../../RePlayEngine/Components/Camera/FollowTargetComponent.h"
#include "../../RePlayEngine/Components/Motion/MotionPlayerComponent.h"
#include "../../RePlayEngine/Components/Core/PropertyLinkComponent.h"
#include "../../RePlayEngine/Components/UI/UIEffectStackComponent.h"
#include "../../RePlayEngine/Components/UI/UISpriteAnimatorComponent.h"
#include "../../RePlayEngine/Components/UI/UITextComponent.h"
#include "../../RePlayEngine/Components/Rendering/LightComponents.h"
#include "../../RePlayEngine/Components/Rendering/ScreenEffectStackComponent.h"
#include "../../RePlayEngine/Components/Rendering/ModelEffectStackComponent.h"
#include "../../RePlayEngine/Components/Rendering/MeshRendererComponent.h"
#include "../../RePlayEngine/Components/Rendering/PrimitiveMeshRendererComponent.h"
#include "../../RePlayEngine/Components/Rendering/SkinnedMeshRendererComponent.h"
#include "../../RePlayEngine/Components/Landscape/LandscapeComponent.h"
#include "../../RePlayEngine/Components/Landscape/LandscapeRendererComponent.h"
#include "../../RePlayEngine/Object/Registry/BuiltInComponents.h"
#include "../../RePlayEngine/Project/ProjectSettingsSerializer.h"
#include "../../RePlayEngine/Rendering/Adapter/SceneRenderCollector.h"
#include "../../RePlayEngine/Motion/MotionBindingResolver.h"
#include "../../RePlayEngine/Motion/MotionEvaluator.h"
#include "../../RePlayEngine/UI/UILayout.h"
#include "../../RePlayEngine/Runtime/Events/EventBus.h"
#include "../../RePlayEngine/Scene/Serialization/SceneData.h"
#include "../../RePlayEngine/Scene/Serialization/SceneSerializer.h"
#include "../../RePlayEngine/Scripting/CSharp/CSharpScriptBackend.h"
#include "../../RePlayEngine/Scripting/Core/ScriptComponent.h"
#include "../../RePlayEngine/Scripting/Core/ScriptRuntime.h"
#include "../../RePlayEngine/Scripting/Core/ScriptTypeCatalog.h"
#include "../../RePlayEngine/Scripting/Core/ScriptTypes.h"
#include "../../game/Behaviours/ValidationBehaviours.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace
{
    namespace SceneSerialization = ReplayEngine::Scene::Serialization;
}

const ReplayEngine::Motion::MotionAsset* framework::resolve_motion_asset(
    const std::string& asset_guid)
{
    if (asset_guid.empty()) return nullptr;

    auto cached = motion_asset_cache.find(asset_guid);
    if (cached != motion_asset_cache.end()) return &cached->second;

    const ReplayEngine::Assets::AssetRecord* record =
        asset_database.FindByGuid(asset_guid);
    if (record == nullptr || record->kind != ReplayEngine::Assets::AssetKind::Motion)
    {
        if (motion_asset_load_failures.insert(asset_guid).second)
        {
            push_editor_log("Warning",
                "Motion Assetを解決できません: " + asset_guid);
        }
        return nullptr;
    }

    ReplayEngine::Motion::MotionAsset asset;
    std::string error;
    const std::filesystem::path motion_path = content_path(record->source_path);
    if (!ReplayEngine::Motion::MotionAsset::LoadFromFile(motion_path,
        asset, error))
    {
        if (motion_asset_load_failures.insert(asset_guid).second)
        {
            push_editor_log("Warning", error, motion_path);
        }
        return nullptr;
    }

    auto inserted = motion_asset_cache.emplace(asset_guid, std::move(asset));
    return &inserted.first->second;
}

void framework::prepare_material_motion_bindings(ReplayEngine::Scene::Scene& scene)
{
    using ReplayEngine::Components::MeshRendererComponent;
    using ReplayEngine::Components::PrimitiveMeshRendererComponent;
    using ReplayEngine::Components::SkinnedMeshRendererComponent;
    using ReplayEngine::Rendering::MaterialAsset;
    using ReplayEngine::Rendering::ShaderID;
    using ReplayEngine::Rendering::ShaderPropertySchema;

    auto resolve_schema = [this](const MaterialAsset* material)
        -> const ShaderPropertySchema*
    {
        if (material == nullptr || material->shader_guid.empty()) return nullptr;
        ShaderID shader_id;
        if (!ShaderID::TryParse(material->shader_guid, shader_id) || !shader_id.IsValid())
            return nullptr;
        const auto* entry = shader_library.Catalog().Find(shader_id);
        return entry != nullptr && entry->schema ? entry->schema.get() : nullptr;
    };

    for (std::size_t object_index = 0; object_index < scene.GameObjectCount(); ++object_index)
    {
        ReplayEngine::Core::GameObject* object = scene.GameObjectAt(object_index);
        if (object == nullptr || object->PendingDestroy()) continue;

        for (std::size_t component_index = 0;
            component_index < object->ComponentCount(); ++component_index)
        {
            ReplayEngine::Core::Component* component = object->ComponentAt(component_index);
            if (component == nullptr || component->PendingDestroy()) continue;

            if (component->TypeID() == MeshRendererComponent::StaticTypeID())
            {
                auto& renderer = static_cast<MeshRendererComponent&>(*component);
                const MaterialAsset* material = resolve_object_material(renderer.material_asset);
                renderer.PrepareMaterialMotion(material, resolve_schema(material));
            }
            else if (component->TypeID() == SkinnedMeshRendererComponent::StaticTypeID())
            {
                auto& renderer = static_cast<SkinnedMeshRendererComponent&>(*component);
                const MaterialAsset* material = resolve_object_material(renderer.material_asset);
                renderer.PrepareMaterialMotion(material, resolve_schema(material));
            }
            else if (component->TypeID() == PrimitiveMeshRendererComponent::StaticTypeID())
            {
                auto& renderer = static_cast<PrimitiveMeshRendererComponent&>(*component);
                const MaterialAsset* material = resolve_object_material(renderer.material_asset);
                renderer.PrepareMaterialMotion(material, resolve_schema(material));
            }
        }
    }
}

void framework::prepare_ui_effect_shader_schemas(ReplayEngine::Scene::Scene& scene)
{
    using ReplayEngine::Assets::AssetKind;
    using ReplayEngine::Components::UIEffectStackComponent;
    using ReplayEngine::Components::ScreenEffectStackComponent;
    using ReplayEngine::Components::ModelEffectStackComponent;
    using ReplayEngine::Rendering::ShaderCatalog;
    using ReplayEngine::Rendering::ShaderDomain;

    const auto normalize = [](std::filesystem::path path)
    {
        std::error_code error;
        std::filesystem::path absolute = path.is_absolute()
            ? path : std::filesystem::absolute(path, error);
        if (error) absolute = path;
        error.clear();
        const std::filesystem::path canonical =
            std::filesystem::weakly_canonical(absolute, error);
        return error ? absolute.lexically_normal() : canonical.lexically_normal();
    };

    const auto resolve_schema =
        [&](const ReplayEngine::UI::UIEffect& effect)
            -> ReplayEngine::Rendering::ShaderPropertySchemaRef
    {
        ReplayEngine::Rendering::ShaderPropertySchemaRef schema;
        const ReplayEngine::Assets::AssetRecord* record =
            asset_database.FindByGuid(effect.custom_shader);
        if (record == nullptr || record->kind != AssetKind::Shader) return schema;

        const std::filesystem::path source = normalize(content_path(record->source_path));
        for (const ShaderCatalog::Entry& entry : shader_library.Catalog().All())
        {
            if (entry.info.domain != ShaderDomain::PostProcess) continue;
            if (normalize(entry.info.source_path) != source) continue;
            schema = entry.schema;
            break;
        }
        return schema;
    };

    const auto prepare_stack = [&](auto* stack)
    {
        if (stack == nullptr) return;
        for (std::size_t effect_index = 0; effect_index < stack->effects.size();
            ++effect_index)
        {
            stack->SetCustomShaderSchema(effect_index,
                resolve_schema(stack->effects[effect_index]));
        }
    };

    for (std::size_t object_index = 0; object_index < scene.GameObjectCount(); ++object_index)
    {
        ReplayEngine::Core::GameObject* object = scene.GameObjectAt(object_index);
        if (object == nullptr || object->PendingDestroy()) continue;
        prepare_stack(object->GetComponent<UIEffectStackComponent>());
        prepare_stack(object->GetComponent<ScreenEffectStackComponent>());
        prepare_stack(object->GetComponent<ModelEffectStackComponent>());
    }
}

void framework::evaluate_motion_players(ReplayEngine::Scene::Scene& scene,
    float scaled_delta_time, float unscaled_delta_time)
{
    using ReplayEngine::Components::MotionPlayerComponent;
    using ReplayEngine::Motion::MotionBindingResolver;
    using ReplayEngine::Motion::MotionEvaluator;
    using ReplayEngine::Motion::MotionTrack;
    using ReplayEngine::Reflection::PropertyValue;

    auto capture_snapshot =
        [&](const ReplayEngine::Motion::MotionAsset& asset,
            MotionPlayerComponent& player)
    {
        std::vector<MotionPlayerComponent::SnapshotValue> values;
        values.reserve(asset.tracks.size());
        for (const MotionTrack& track : asset.tracks)
        {
            const ReplayEngine::Motion::ResolvedMotionBinding binding =
                MotionBindingResolver::Resolve(scene, track.binding, player.Owner());
            if (!binding.Valid()) continue;
            MotionPlayerComponent::SnapshotValue snapshot;
            snapshot.binding = track.binding;
            snapshot.value = binding.property->Capture(*binding.component);
            values.push_back(std::move(snapshot));
        }
        player.StoreSnapshot(std::move(values));
    };

    auto contribute_restore =
        [&](MotionPlayerComponent& player)
    {
        for (const MotionPlayerComponent::SnapshotValue& snapshot :
            player.SnapshotValues())
        {
            const ReplayEngine::Motion::ResolvedMotionBinding binding =
                MotionBindingResolver::Resolve(scene, snapshot.binding, player.Owner());
            motion_mixer.Contribute(binding, snapshot.value, 1.0f,
                ReplayEngine::Motion::MotionBlendMode::Override);
        }
        player.ConsumeStopRestoreRequest();
    };

    // Event は「current == event.time」で見ない。
    // フレーム間に通過した再生座標を展開し、その区間へ Event が何回入ったかを数える。
    // これにより 1 フレームで複数周しても、Loop/PingPong の端でも取りこぼさない。
    const auto publish_motion_event =
        [&](const ReplayEngine::Motion::MotionEventTrack& track,
            const ReplayEngine::Motion::MotionEvent& event,
            const MotionPlayerComponent& player)
    {
        if (object_runtime_context == nullptr || event.name.empty()) return;

        ReplayEngine::Runtime::ObjectHandle target =
            ReplayEngine::Runtime::ObjectHandle::None();
        if (track.object.Valid())
        {
            target = object_runtime_context->Resolver().FindByObjectID(track.object);
            if (target.IsEmpty()) return;
        }

        ReplayEngine::Runtime::EventRecord record;
        record.type = ReplayEngine::Runtime::EngineEvents::MotionEvent;
        record.type_name = "MotionEvent";
        record.source = object_runtime_context->Resolver().MakeHandle(player.Owner());
        record.target = target;
        record.frame_index = object_runtime_frame_index;
        record.payload.Set("name",
            ReplayEngine::Reflection::PropertyValue::MakeString(event.name));
        record.payload.Set("parameter",
            ReplayEngine::Reflection::PropertyValue::MakeString(event.parameter));
        record.payload.Set("time",
            ReplayEngine::Reflection::PropertyValue::MakeFloat(event.time));
        object_runtime_context->Events().Publish(std::move(record));
    };

    const auto publish_repeated = [](long long first, long long last,
        const auto& callback)
    {
        if (last < first) return;
        for (long long i = first; i <= last; ++i) callback(i);
    };

    auto publish_motion_events =
        [&](const ReplayEngine::Motion::MotionAsset& asset,
            const MotionPlayerComponent& player, float before, float delta_time,
            int direction_before)
    {
        if (object_runtime_context == nullptr || asset.event_tracks.empty() ||
            asset.duration <= 0.0f || delta_time <= 0.0f || player.speed == 0.0f)
        {
            return;
        }

        const double duration = static_cast<double>(asset.duration);
        const int wrap_mode = player.RuntimeWrapMode();
        const double epsilon = 1.0e-9;

        for (const ReplayEngine::Motion::MotionEventTrack& track : asset.event_tracks)
        {
            for (const ReplayEngine::Motion::MotionEvent& event : track.events)
            {
                if (event.name.empty() || event.time < 0.0f ||
                    event.time > asset.duration)
                {
                    continue;
                }

                const double event_time = static_cast<double>(event.time);
                auto emit = [&](long long) { publish_motion_event(track, event, player); };

                if (wrap_mode == MotionPlayerComponent::Loop)
                {
                    const double travel = static_cast<double>(delta_time) *
                        static_cast<double>(player.speed);
                    const double from = static_cast<double>(before);
                    const double to = from + travel;
                    if (travel > 0.0)
                    {
                        const long long first = static_cast<long long>(std::floor(
                            (from - event_time) / duration)) + 1;
                        const long long last = static_cast<long long>(std::floor(
                            (to - event_time + epsilon) / duration));
                        publish_repeated(first, last, emit);
                    }
                    else if (travel < 0.0)
                    {
                        const long long first = static_cast<long long>(std::ceil(
                            (to - event_time - epsilon) / duration));
                        const long long last = static_cast<long long>(std::ceil(
                            (from - event_time) / duration)) - 1;
                        publish_repeated(first, last, emit);
                    }
                    continue;
                }

                if (wrap_mode == MotionPlayerComponent::PingPong)
                {
                    if (direction_before == 0) continue;
                    const double period = duration * 2.0;
                    const double phase_from = direction_before > 0
                        ? static_cast<double>(before)
                        : period - static_cast<double>(before);
                    const double phase_to = phase_from +
                        static_cast<double>(delta_time) * std::fabs(
                            static_cast<double>(player.speed));

                    auto emit_phase_series = [&](double base)
                    {
                        const long long first = static_cast<long long>(std::floor(
                            (phase_from - base) / period)) + 1;
                        const long long last = static_cast<long long>(std::floor(
                            (phase_to - base + epsilon) / period));
                        publish_repeated(first, last, emit);
                    };

                    emit_phase_series(event_time);
                    // 端点は往路/復路が同じ位相になる。二重発火させない。
                    if (event_time > 0.0 && event_time < duration)
                        emit_phase_series(period - event_time);
                    continue;
                }

                // Once / ClampForever は端をまたがないので単一区間。
                const double travel = static_cast<double>(delta_time) *
                    static_cast<double>(player.speed);
                double after = static_cast<double>(before) + travel;
                after = (std::max)(0.0, (std::min)(duration, after));
                if (travel > 0.0)
                {
                    if (event_time > static_cast<double>(before) &&
                        event_time <= after + epsilon)
                    {
                        emit(0);
                    }
                }
                else if (travel < 0.0)
                {
                    if (event_time < static_cast<double>(before) &&
                        event_time + epsilon >= after)
                    {
                        emit(0);
                    }
                }
            }
        }
    };

    motion_mixer.BeginFrame();

    for (std::size_t object_index = 0; object_index < scene.GameObjectCount();
        ++object_index)
    {
        ReplayEngine::Core::GameObject* object = scene.GameObjectAt(object_index);
        if (object == nullptr || object->PendingDestroy() ||
            !object->ActiveInHierarchy())
        {
            continue;
        }

        for (std::size_t component_index = 0;
            component_index < object->ComponentCount(); ++component_index)
        {
            ReplayEngine::Core::Component* component =
                object->ComponentAt(component_index);
            if (component == nullptr || component->PendingDestroy() ||
                !component->ActiveInHierarchy() ||
                component->TypeID() != MotionPlayerComponent::StaticTypeID())
            {
                continue;
            }

            MotionPlayerComponent& player =
                static_cast<MotionPlayerComponent&>(*component);

            const ReplayEngine::Motion::MotionAsset* asset =
                resolve_motion_asset(player.motion.guid);
            if (asset == nullptr) continue;

            const float player_delta_time = player.ignore_time_scale
                ? unscaled_delta_time : scaled_delta_time;
            player.AdvanceTriggerDelay(player_delta_time);

            if (player.HasStopRestoreRequest())
            {
                contribute_restore(player);
                continue;
            }

            if (!player.ShouldContribute()) continue;

            if (player.NeedsSnapshot())
            {
                capture_snapshot(*asset, player);
            }

            const float previous_motion_time = player.time;
            const int previous_playback_direction = player.PlaybackDirection();
            player.Advance(asset->duration, player_delta_time);
            publish_motion_events(*asset, player, previous_motion_time, player_delta_time,
                previous_playback_direction);
            if (player.HasStopRestoreRequest())
            {
                contribute_restore(player);
                continue;
            }

            const float blend_alpha = player.BlendInAlpha();
            for (const MotionTrack& track : asset->tracks)
            {
                PropertyValue value;
                if (!MotionEvaluator::EvaluateTrack(track, player.time, value))
                    continue;

                const ReplayEngine::Motion::ResolvedMotionBinding binding =
                    MotionBindingResolver::Resolve(scene, track.binding, player.Owner());
                if (!binding.Valid()) continue;

                if (blend_alpha < 1.0f)
                {
                    if (const PropertyValue* base = player.SnapshotFor(track.binding))
                    {
                        value = PropertyValue::Lerp(*base, value, blend_alpha);
                    }
                }
                motion_mixer.Contribute(binding, value, player.weight,
                    track.blend_mode);
            }
        }
    }

    motion_mixer.Apply();
}

