// GameObject / Component 基盤のうち「Motion Asset / Binding / Player 更新」を持つ。
// 関数本体は分割前のまま移動し、Runtime 更新の順序と分岐は変更しない。
#include "framework.h"

#include "../../RePlayEngine/Components/Camera/CameraComponent.h"
#include "../../RePlayEngine/Components/Camera/CameraTargetComponent.h"
#include "../../RePlayEngine/Components/Camera/FollowTargetComponent.h"
#include "../../RePlayEngine/Components/Motion/MotionPlayerComponent.h"
#include "../../RePlayEngine/Components/Motion/CompositionPlayerComponent.h"
#include "../../RePlayEngine/Motion/CompositionAsset.h"
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
#include <functional>
#include <string>
#include <unordered_set>
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


const ReplayEngine::Motion::CompositionAsset* framework::resolve_composition_asset(
    const std::string& asset_guid)
{
    if (asset_guid.empty()) return nullptr;
    auto cached = composition_asset_cache.find(asset_guid);
    if (cached != composition_asset_cache.end()) return &cached->second;

    const ReplayEngine::Assets::AssetRecord* record = asset_database.FindByGuid(asset_guid);
    if (record == nullptr || record->kind != ReplayEngine::Assets::AssetKind::Composition)
    {
        if (composition_asset_load_failures.insert(asset_guid).second)
            push_editor_log("Warning", "Composition Assetを解決できません: " + asset_guid);
        return nullptr;
    }

    ReplayEngine::Motion::CompositionAsset asset;
    std::string error;
    const std::filesystem::path composition_path = content_path(record->source_path);
    if (!ReplayEngine::Motion::CompositionAsset::LoadFromFile(
        composition_path, asset, error))
    {
        if (composition_asset_load_failures.insert(asset_guid).second)
            push_editor_log("Warning", error, composition_path);
        return nullptr;
    }
    auto inserted = composition_asset_cache.emplace(asset_guid, std::move(asset));
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
    using ReplayEngine::Components::CompositionPlayerComponent;
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

    const auto crossed_once = [](double before, double after, double point)
    {
        if (after >= before) return point > before && point <= after;
        return point < before && point >= after;
    };

    const auto publish_composition_marker =
        [&](const ReplayEngine::Motion::CompositionMarker& marker,
            const CompositionPlayerComponent& player)
    {
        if (object_runtime_context == nullptr || marker.name.empty()) return;
        ReplayEngine::Runtime::EventRecord record;
        record.type = ReplayEngine::Runtime::EngineEvents::CompositionMarker;
        record.type_name = "CompositionMarker";
        record.source = object_runtime_context->Resolver().MakeHandle(player.Owner());
        record.frame_index = object_runtime_frame_index;
        record.payload.Set("name",
            ReplayEngine::Reflection::PropertyValue::MakeString(marker.name));
        record.payload.Set("time",
            ReplayEngine::Reflection::PropertyValue::MakeFloat(marker.time));
        record.payload.Set("composition",
            ReplayEngine::Reflection::PropertyValue::MakeString(player.composition.guid));
        object_runtime_context->Events().Publish(std::move(record));
    };

    const auto publish_composition_markers =
        [&](const ReplayEngine::Motion::CompositionAsset& asset,
            const CompositionPlayerComponent& player, float before,
            float delta_time)
    {
        if (object_runtime_context == nullptr || asset.markers.empty() ||
            asset.duration <= 0.0f || delta_time == 0.0f || player.speed == 0.0f)
        {
            return;
        }
        const double travel = static_cast<double>(delta_time) *
            static_cast<double>(player.speed);
        const double from = static_cast<double>(before);
        const double duration = static_cast<double>(asset.duration);
        if (!player.loop)
        {
            const double to = (std::max)(0.0, (std::min)(duration, from + travel));
            for (const auto& marker : asset.markers)
                if (crossed_once(from, to, marker.time))
                    publish_composition_marker(marker, player);
            return;
        }

        const double to = from + travel;
        for (const auto& marker : asset.markers)
        {
            const double mt = static_cast<double>(marker.time);
            if (travel > 0.0)
            {
                const long long first = static_cast<long long>(std::floor((from - mt) / duration)) + 1;
                const long long last = static_cast<long long>(std::floor((to - mt) / duration));
                publish_repeated(first, last, [&](long long) { publish_composition_marker(marker, player); });
            }
            else if (travel < 0.0)
            {
                const long long first = static_cast<long long>(std::ceil((to - mt) / duration));
                const long long last = static_cast<long long>(std::ceil((from - mt) / duration)) - 1;
                publish_repeated(first, last, [&](long long) { publish_composition_marker(marker, player); });
            }
        }
    };

    const auto publish_composition_motion_event =
        [&](const ReplayEngine::Motion::MotionEventTrack& track,
            const ReplayEngine::Motion::MotionEvent& event,
            const CompositionPlayerComponent& player)
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
        record.payload.Set("composition",
            ReplayEngine::Reflection::PropertyValue::MakeString(player.composition.guid));
        object_runtime_context->Events().Publish(std::move(record));
    };

    const auto publish_composition_motion_events =
        [&](const ReplayEngine::Motion::CompositionAsset& root_composition,
            const CompositionPlayerComponent& player, float before, float delta_time)
    {
        if (object_runtime_context == nullptr || root_composition.duration <= 0.0f ||
            delta_time == 0.0f || player.speed == 0.0f) return;

        const double duration = static_cast<double>(root_composition.duration);
        const double travel = static_cast<double>(delta_time) *
            static_cast<double>(player.speed);
        const double from = static_cast<double>(before);
        const double unwrapped_to = from + travel;
        constexpr double epsilon = 1.0e-8;

        const auto crossed_root_time = [&](double event_time, const auto& emit)
        {
            if (event_time < -epsilon || event_time > duration + epsilon) return;
            if (!player.loop)
            {
                const double to = (std::max)(0.0,
                    (std::min)(duration, unwrapped_to));
                if (crossed_once(from, to, event_time)) emit();
                return;
            }
            if (travel > 0.0)
            {
                const long long first = static_cast<long long>(std::floor(
                    (from - event_time) / duration)) + 1;
                const long long last = static_cast<long long>(std::floor(
                    (unwrapped_to - event_time + epsilon) / duration));
                publish_repeated(first, last, [&](long long) { emit(); });
            }
            else if (travel < 0.0)
            {
                const long long first = static_cast<long long>(std::ceil(
                    (unwrapped_to - event_time - epsilon) / duration));
                const long long last = static_cast<long long>(std::ceil(
                    (from - event_time) / duration)) - 1;
                publish_repeated(first, last, [&](long long) { emit(); });
            }
        };

        std::vector<std::pair<double, double>> root_intervals;
        std::vector<std::pair<double, double>> root_boundary_transitions;
        if (!player.loop)
        {
            const double to = (std::max)(0.0, (std::min)(duration, unwrapped_to));
            root_intervals.emplace_back(from, to);
        }
        else if (travel > 0.0)
        {
            double cursor = from;
            while (cursor < unwrapped_to - epsilon)
            {
                const double cycle = std::floor(cursor / duration);
                const double local_from = cursor - cycle * duration;
                const double boundary = (cycle + 1.0) * duration;
                const double segment_end = (std::min)(unwrapped_to, boundary);
                root_intervals.emplace_back(local_from, segment_end - cycle * duration);
                if (segment_end >= boundary - epsilon)
                    root_boundary_transitions.emplace_back(duration, 0.0);
                if (segment_end <= cursor + epsilon) break;
                cursor = segment_end;
            }
        }
        else if (travel < 0.0)
        {
            double cursor = from;
            while (cursor > unwrapped_to + epsilon)
            {
                const double cycle = std::ceil(cursor / duration) - 1.0;
                const double local_from = cursor - cycle * duration;
                const double boundary = cycle * duration;
                const double segment_end = (std::max)(unwrapped_to, boundary);
                root_intervals.emplace_back(local_from, segment_end - cycle * duration);
                if (segment_end <= boundary + epsilon)
                    root_boundary_transitions.emplace_back(0.0, duration);
                if (segment_end >= cursor - epsilon) break;
                cursor = segment_end;
            }
        }

        std::unordered_set<std::string> recursion;
        if (!player.composition.guid.empty()) recursion.insert(player.composition.guid);
        std::function<void(const ReplayEngine::Motion::CompositionAsset&, double, double,
            double, double, int)> visit;
        visit = [&](const ReplayEngine::Motion::CompositionAsset& composition,
            double root_offset, double root_scale, double valid_min, double valid_max, int depth)
        {
            if (depth > 16 || std::fabs(root_scale) < 1.0e-9) return;
            for (const ReplayEngine::Motion::CompositionMotionLayer& layer : composition.layers)
            {
                if (!layer.enabled || std::fabs(layer.time_scale) < 1.0e-9f) continue;
                const double layer_in = static_cast<double>(layer.in_time);
                const double layer_out = layer.out_time >= 0.0f
                    ? static_cast<double>(layer.out_time)
                    : static_cast<double>(composition.duration);
                const double root_a = root_offset + layer_in * root_scale;
                const double root_b = root_offset + layer_out * root_scale;
                const double layer_valid_min = (std::max)(valid_min,
                    (std::min)(root_a, root_b));
                const double layer_valid_max = (std::min)(valid_max,
                    (std::max)(root_a, root_b));
                if (layer_valid_max + epsilon < layer_valid_min) continue;

                const double source_root_offset = root_offset +
                    static_cast<double>(layer.start_offset) * root_scale;
                const double source_root_scale = root_scale /
                    static_cast<double>(layer.time_scale);
                if (!layer.motion_guid.empty())
                {
                    const ReplayEngine::Motion::MotionAsset* motion =
                        resolve_motion_asset(layer.motion_guid);
                    if (motion == nullptr) continue;
                    bool remap_active = false;
                    if (motion->time_remap.IsAssigned() && motion->duration > 0.0f)
                    {
                        std::string remap_error;
                        (void)MotionEvaluator::RemapMotionTime(*motion, 0.0f,
                            &asset_database, &remap_error);
                        remap_active = remap_error.empty();
                    }
                    for (const ReplayEngine::Motion::MotionEventTrack& track : motion->event_tracks)
                    {
                        for (const ReplayEngine::Motion::MotionEvent& event : track.events)
                        {
                            if (event.name.empty() || event.time < 0.0f ||
                                event.time > motion->duration) continue;
                            const double event_time = static_cast<double>(event.time);
                            if (!remap_active)
                            {
                                const double root_time = source_root_offset +
                                    event_time * source_root_scale;
                                if (root_time + epsilon < layer_valid_min ||
                                    root_time - epsilon > layer_valid_max) continue;
                                crossed_root_time(root_time, [&]()
                                {
                                    publish_composition_motion_event(track, event, player);
                                });
                                continue;
                            }
                            for (const auto& root_interval : root_intervals)
                            {
                                double root_from = root_interval.first;
                                double root_to = root_interval.second;
                                if (root_to >= root_from)
                                {
                                    root_from = (std::max)(root_from, layer_valid_min);
                                    root_to = (std::min)(root_to, layer_valid_max);
                                    if (root_to + epsilon < root_from) continue;
                                }
                                else
                                {
                                    root_from = (std::min)(root_from, layer_valid_max);
                                    root_to = (std::max)(root_to, layer_valid_min);
                                    if (root_from + epsilon < root_to) continue;
                                }
                                double source_from = (root_from - source_root_offset) /
                                    source_root_scale;
                                double source_to = (root_to - source_root_offset) /
                                    source_root_scale;
                                source_from = (std::max)(0.0,
                                    (std::min)(static_cast<double>(motion->duration), source_from));
                                source_to = (std::max)(0.0,
                                    (std::min)(static_cast<double>(motion->duration), source_to));
                                const double evaluated_from = static_cast<double>(
                                    MotionEvaluator::RemapMotionTime(*motion,
                                        static_cast<float>(source_from), &asset_database));
                                const double evaluated_to = static_cast<double>(
                                    MotionEvaluator::RemapMotionTime(*motion,
                                        static_cast<float>(source_to), &asset_database));
                                if (crossed_once(evaluated_from, evaluated_to, event_time))
                                    publish_composition_motion_event(track, event, player);
                            }
                            for (const auto& root_transition : root_boundary_transitions)
                            {
                                const double root_after = root_transition.second;
                                if (root_after + epsilon < layer_valid_min ||
                                    root_after - epsilon > layer_valid_max) continue;
                                double source_before = (root_transition.first - source_root_offset) /
                                    source_root_scale;
                                double source_after = (root_after - source_root_offset) /
                                    source_root_scale;
                                source_before = (std::max)(0.0,
                                    (std::min)(static_cast<double>(motion->duration), source_before));
                                source_after = (std::max)(0.0,
                                    (std::min)(static_cast<double>(motion->duration), source_after));
                                const double evaluated_before = static_cast<double>(
                                    MotionEvaluator::RemapMotionTime(*motion,
                                        static_cast<float>(source_before), &asset_database));
                                const double evaluated_after = static_cast<double>(
                                    MotionEvaluator::RemapMotionTime(*motion,
                                        static_cast<float>(source_after), &asset_database));
                                if (std::fabs(evaluated_before - evaluated_after) > epsilon &&
                                    std::fabs(event_time - evaluated_after) <= epsilon)
                                {
                                    publish_composition_motion_event(track, event, player);
                                }
                            }
                        }
                    }
                    continue;
                }

                if (!layer.composition_guid.empty())
                {
                    if (recursion.find(layer.composition_guid) != recursion.end()) continue;
                    const ReplayEngine::Motion::CompositionAsset* nested =
                        resolve_composition_asset(layer.composition_guid);
                    if (nested == nullptr) continue;
                    recursion.insert(layer.composition_guid);
                    visit(*nested, source_root_offset, source_root_scale,
                        layer_valid_min, layer_valid_max, depth + 1);
                    recursion.erase(layer.composition_guid);
                }
            }
        };
        visit(root_composition, 0.0, 1.0, 0.0, duration, 0);
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
        bool remap_active = false;
        if (asset.time_remap.IsAssigned())
        {
            std::string remap_error;
            (void)MotionEvaluator::RemapMotionTime(asset, 0.0f,
                &asset_database, &remap_error);
            remap_active = remap_error.empty();
        }
        if (!remap_active)
        {
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
            return;
        }

        std::vector<std::pair<double, double>> intervals;
        std::vector<double> boundary_points;
        const auto remap_time = [&](double raw_time)
        {
            return static_cast<double>(MotionEvaluator::RemapMotionTime(asset,
                static_cast<float>(raw_time), &asset_database));
        };
        const auto append_interval = [&](double raw_from, double raw_to)
        {
            intervals.emplace_back(remap_time(raw_from), remap_time(raw_to));
        };

        if (wrap_mode == MotionPlayerComponent::Loop)
        {
            const double travel = static_cast<double>(delta_time) *
                static_cast<double>(player.speed);
            const double from = static_cast<double>(before);
            const double to = from + travel;
            double cursor = from;
            if (travel > 0.0)
            {
                while (cursor < to - epsilon)
                {
                    const double cycle = std::floor(cursor / duration);
                    const double local_from = cursor - cycle * duration;
                    const double boundary = (cycle + 1.0) * duration;
                    const double segment_end = (std::min)(to, boundary);
                    const double local_to = segment_end - cycle * duration;
                    append_interval(local_from, local_to);
                    if (segment_end >= boundary - epsilon)
                    {
                        const double restart = remap_time(0.0);
                        if (std::fabs(restart - remap_time(duration)) > epsilon)
                            boundary_points.push_back(restart);
                    }
                    if (segment_end <= cursor + epsilon) break;
                    cursor = segment_end;
                }
            }
            else if (travel < 0.0)
            {
                while (cursor > to + epsilon)
                {
                    const double cycle = std::ceil(cursor / duration) - 1.0;
                    const double local_from = cursor - cycle * duration;
                    const double boundary = cycle * duration;
                    const double segment_end = (std::max)(to, boundary);
                    const double local_to = segment_end - cycle * duration;
                    append_interval(local_from, local_to);
                    if (segment_end <= boundary + epsilon)
                    {
                        const double restart = remap_time(duration);
                        if (std::fabs(restart - remap_time(0.0)) > epsilon)
                            boundary_points.push_back(restart);
                    }
                    if (segment_end >= cursor - epsilon) break;
                    cursor = segment_end;
                }
            }
        }
        else if (wrap_mode == MotionPlayerComponent::PingPong)
        {
            if (direction_before != 0)
            {
                const double period = duration * 2.0;
                const double phase_from = direction_before > 0
                    ? static_cast<double>(before)
                    : period - static_cast<double>(before);
                const double phase_to = phase_from + static_cast<double>(delta_time) *
                    std::fabs(static_cast<double>(player.speed));
                const auto phase_to_local = [&](double phase)
                {
                    double wrapped = std::fmod(phase, period);
                    if (wrapped < 0.0) wrapped += period;
                    return wrapped <= duration ? wrapped : period - wrapped;
                };
                double cursor = phase_from;
                while (cursor < phase_to - epsilon)
                {
                    const double boundary =
                        (std::floor(cursor / duration) + 1.0) * duration;
                    const double segment_end = (std::min)(phase_to, boundary);
                    append_interval(phase_to_local(cursor), phase_to_local(segment_end));
                    if (segment_end <= cursor + epsilon) break;
                    cursor = segment_end;
                }
            }
        }
        else
        {
            const double travel = static_cast<double>(delta_time) *
                static_cast<double>(player.speed);
            double after = static_cast<double>(before) + travel;
            after = (std::max)(0.0, (std::min)(duration, after));
            append_interval(static_cast<double>(before), after);
        }

        const auto crossed_interval = [&](double event_time, const std::pair<double, double>& interval)
        {
            if (interval.second > interval.first)
                return event_time > interval.first && event_time <= interval.second + epsilon;
            if (interval.second < interval.first)
                return event_time < interval.first && event_time + epsilon >= interval.second;
            return false;
        };

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
                for (const auto& interval : intervals)
                {
                    if (crossed_interval(event_time, interval))
                        publish_motion_event(track, event, player);
                }
                for (const double point : boundary_points)
                {
                    if (std::fabs(event_time - point) <= epsilon)
                        publish_motion_event(track, event, player);
                }
            }
        }
    };

    std::function<void(const ReplayEngine::Motion::CompositionAsset&,
        float, ReplayEngine::Core::GameObject*, float, int,
        std::unordered_set<std::string>&)> contribute_composition;
    contribute_composition =
        [&](const ReplayEngine::Motion::CompositionAsset& composition,
            float composition_time, ReplayEngine::Core::GameObject* owner,
            float parent_weight, int depth, std::unordered_set<std::string>& recursion)
    {
        if (owner == nullptr || depth > 16 || parent_weight <= 0.0f) return;
        for (const ReplayEngine::Motion::CompositionMotionLayer& layer : composition.layers)
        {
            if (!layer.enabled || layer.weight <= 0.0f) continue;
            if (composition_time < layer.in_time) continue;
            if (layer.out_time >= 0.0f && composition_time > layer.out_time) continue;

            const float source_time = (composition_time - layer.start_offset) * layer.time_scale;
            const float layer_weight = parent_weight * layer.weight;
            if (!layer.motion_guid.empty())
            {
                const ReplayEngine::Motion::MotionAsset* motion =
                    resolve_motion_asset(layer.motion_guid);
                if (motion == nullptr) continue;
                const float t = (std::max)(0.0f,
                    (std::min)(motion->duration, source_time));
                const float evaluated_time = MotionEvaluator::RemapMotionTime(*motion, t,
                    &asset_database);
                for (const MotionTrack& track : motion->tracks)
                {
                    PropertyValue value;
                    ReplayEngine::Motion::MotionTrackEvaluationContext evaluation_context;
                    evaluation_context.time = evaluated_time;
                    evaluation_context.raw_time = t;
                    evaluation_context.duration = motion->duration;
                    evaluation_context.database = &asset_database;
                    if (!MotionEvaluator::EvaluateTrackWithContext(track, evaluation_context, value))
                        continue;
                    const ReplayEngine::Motion::ResolvedMotionBinding binding =
                        MotionBindingResolver::Resolve(scene, track.binding, owner);
                    if (!binding.Valid()) continue;
                    motion_mixer.Contribute(binding, value, layer_weight, track.blend_mode);
                }
                continue;
            }

            if (!layer.composition_guid.empty())
            {
                if (recursion.find(layer.composition_guid) != recursion.end()) continue;
                const ReplayEngine::Motion::CompositionAsset* nested =
                    resolve_composition_asset(layer.composition_guid);
                if (nested == nullptr) continue;
                recursion.insert(layer.composition_guid);
                const float nested_time = (std::max)(0.0f,
                    (std::min)(nested->duration, source_time));
                contribute_composition(*nested, nested_time, owner, layer_weight,
                    depth + 1, recursion);
                recursion.erase(layer.composition_guid);
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
                !component->ActiveInHierarchy())
            {
                continue;
            }

            if (component->TypeID() == CompositionPlayerComponent::StaticTypeID())
            {
                CompositionPlayerComponent& player =
                    static_cast<CompositionPlayerComponent&>(*component);
                const ReplayEngine::Motion::CompositionAsset* composition =
                    resolve_composition_asset(player.composition.guid);
                if (composition == nullptr || !player.ShouldContribute()) continue;

                const float player_delta_time = player.ignore_time_scale
                    ? unscaled_delta_time : scaled_delta_time;
                const float previous_time = player.time;
                if (player.state == CompositionPlayerComponent::Playing)
                {
                    publish_composition_markers(*composition, player,
                        previous_time, player_delta_time);
                    publish_composition_motion_events(*composition, player,
                        previous_time, player_delta_time);
                    player.Advance(composition->duration, player_delta_time);
                }
                std::unordered_set<std::string> recursion;
                if (!player.composition.guid.empty()) recursion.insert(player.composition.guid);
                contribute_composition(*composition, player.time, player.Owner(),
                    (std::max)(0.0f, player.weight), 0, recursion);
                continue;
            }

            if (component->TypeID() != MotionPlayerComponent::StaticTypeID()) continue;

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
            const float evaluated_time = MotionEvaluator::RemapMotionTime(*asset, player.time,
                &asset_database);
            for (const MotionTrack& track : asset->tracks)
            {
                PropertyValue value;
                ReplayEngine::Motion::MotionTrackEvaluationContext evaluation_context;
                evaluation_context.time = evaluated_time;
                evaluation_context.raw_time = player.time;
                evaluation_context.duration = asset->duration;
                evaluation_context.database = &asset_database;
                if (!MotionEvaluator::EvaluateTrackWithContext(track, evaluation_context, value))
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

