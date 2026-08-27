#include "framework.h"

#include "../../RePlayEngine/Components/UI/UIImageComponent.h"
#include "../../RePlayEngine/Motion/MotionBindingResolver.h"
#include "../../RePlayEngine/Motion/MotionEvaluator.h"
#include "../../RePlayEngine/Object/GameObject/GameObject.h"
#include "../../RePlayEngine/Object/Registry/ComponentRegistry.h"
#include "../../RePlayEngine/Reflection/Registry/PropertyRegistry.h"
#include "../../RePlayEngine/Scene/Runtime/Scene.h"

#include "imgui/imgui_internal.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <functional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "framework_motion_workspaceInternal.h"
using namespace framework_motion_workspace::Detail;

// 編集操作とモーションプレビュー状態の関数本体

bool framework::open_motion_asset(const ReplayEngine::Assets::AssetRecord& asset)
{
    using ReplayEngine::Assets::AssetKind;
    if (asset.kind != AssetKind::Motion && asset.kind != AssetKind::Composition) return false;

    stop_motion_preview();
    motion_easing_curve_warning_guids.clear();
    const std::string extension = Lower(asset.source_path.extension().u8string());
    std::string error;
    if (extension == ReplayEngine::Motion::CompositionAsset::file_extension)
    {
        ReplayEngine::Motion::CompositionAsset composition;
        if (!ReplayEngine::Motion::CompositionAsset::LoadFromFile(
            asset.source_path, composition, error))
        {
            motion_editor_status = error;
            push_editor_log("Warning", error, asset.source_path);
            return false;
        }

        motion_editor_composition = std::move(composition);
        motion_composition_loaded = true;
        motion_editor_loaded = false;
        motion_editor_guid = asset.guid;
        motion_editor_path = asset.source_path;
        motion_editor_dirty = false;
        motion_edit_history.Clear();
        composition_asset_cache[motion_editor_guid] = motion_editor_composition;
        composition_asset_load_failures.erase(motion_editor_guid);
        set_editor_workspace(editor_workspace::motion);
        motion_editor_status = "Compositionを開きました: " + asset.display_name;
        return true;
    }

    MotionAsset motion;
    if (!MotionAsset::LoadFromFile(asset.source_path, motion, error))
    {
        motion_editor_status = error;
        push_editor_log("Warning", error, asset.source_path);
        return false;
    }

    motion_editor_asset = std::move(motion);
    motion_editor_guid = asset.guid;
    motion_editor_path = asset.source_path;
    motion_editor_loaded = true;
    motion_composition_loaded = false;
    motion_editor_dirty = false;
    motion_selected_track = motion_editor_asset.tracks.empty() ? -1 : 0;
    motion_selected_key = -1;
    motion_selected_keys.clear();
    motion_selected_event_track = -1;
    motion_selected_event = -1;
    motion_preview_time = 0.0f;
    motion_edit_history.Clear();
    motion_asset_cache[motion_editor_guid] = motion_editor_asset;
    motion_asset_load_failures.erase(motion_editor_guid);
    set_editor_workspace(editor_workspace::motion);
    motion_editor_status = "Motionを開きました: " + asset.display_name;
    return true;
}

bool framework::save_current_motion_asset()
{
    if (motion_composition_loaded)
    {
        if (motion_editor_path.empty())
        {
            motion_editor_status = "保存する Composition のパスがありません。";
            return false;
        }
        std::string error;
        if (!ReplayEngine::Motion::CompositionAsset::SaveToFile(
            motion_editor_path, motion_editor_composition, error))
        {
            motion_editor_status = error;
            push_editor_log("Warning", error, motion_editor_path);
            return false;
        }
        const ReplayEngine::Assets::AssetRecord& record = asset_database.Register(
            motion_editor_path, ReplayEngine::Assets::AssetKind::Composition);
        motion_editor_guid = record.guid;
        composition_asset_cache[motion_editor_guid] = motion_editor_composition;
        composition_asset_load_failures.erase(motion_editor_guid);
        std::string save_error;
        if (!asset_database.Save(save_error))
        {
            motion_editor_status = "Composition は保存しましたが AssetDatabase 保存に失敗: " + save_error;
            push_editor_log("Warning", motion_editor_status, motion_editor_path);
            return false;
        }
        selected_asset_guid = motion_editor_guid;
        motion_editor_dirty = false;
        motion_editor_status = "Compositionを保存しました: " + motion_editor_path.filename().u8string();
        return true;
    }

    if (!motion_editor_loaded)
    {
        motion_editor_status = "保存する Motion Asset がありません。";
        return false;
    }
    if (motion_editor_path.empty())
    {
        motion_editor_status = "Project Browser の Create > Motion Asset から作成してください。";
        return false;
    }

    motion_editor_asset.SortKeys();
    std::string error;
    if (!MotionAsset::SaveToFile(motion_editor_path, motion_editor_asset, error))
    {
        motion_editor_status = error;
        push_editor_log("Warning", error, motion_editor_path);
        return false;
    }

    const ReplayEngine::Assets::AssetRecord& record =
        asset_database.Register(motion_editor_path, ReplayEngine::Assets::AssetKind::Motion);
    motion_editor_guid = record.guid;
    motion_asset_cache[motion_editor_guid] = motion_editor_asset;
    motion_asset_load_failures.erase(motion_editor_guid);

    std::string save_error;
    if (!asset_database.Save(save_error))
    {
        motion_editor_status = "Motionは保存しましたが AssetDatabase 保存に失敗: " + save_error;
        push_editor_log("Warning", motion_editor_status, motion_editor_path);
        return false;
    }

    selected_asset_guid = motion_editor_guid;
    motion_editor_dirty = false;
    motion_editor_status = "Motionを保存しました: " + motion_editor_path.filename().u8string();
    return true;
}

bool framework::add_motion_key_at_preview_time()
{
    using namespace ReplayEngine::Motion;
    using ReplayEngine::Reflection::PropertyDesc;
    if (motion_selected_track < 0 ||
        motion_selected_track >= static_cast<int>(motion_editor_asset.tracks.size()))
        return false;

    MotionTrack& track = motion_editor_asset.tracks[motion_selected_track];
    motion_edit_history.Begin(motion_editor_asset, "Keyを追加");
    MotionKeyframe key;
    key.time = motion_preview_time;
    ReplayEngine::Scene::Scene* scene = object_editor_context.GetScene();
    ReplayEngine::Core::Component* bound_component = scene != nullptr
        ? ResolveBindingComponent(*scene, track.binding) : nullptr;
    const PropertyDesc* bound_desc = bound_component != nullptr
        ? FindPropertyForComponent(*bound_component, track.binding.property)
        : nullptr;
    key.value = bound_desc != nullptr
        ? bound_desc->Capture(*bound_component) : DefaultValueFor(track.value_type);
    key.easing = MotionEasing::Linear;
    track.keys.push_back(key);
    motion_editor_asset.SortKeys();
    motion_selected_key = -1;
    for (int index = 0; index < static_cast<int>(track.keys.size()); ++index)
    {
        if (track.keys[index].time == key.time)
        {
            motion_selected_key = index;
            break;
        }
    }
    motion_edit_history.Commit(motion_editor_asset);
    motion_editor_dirty = true;
    motion_editor_status = "Keyを追加しました";
    return true;
}

ReplayEngine::Motion::MotionTrack* framework::selected_motion_track()
{
    if (!motion_editor_loaded || motion_selected_track < 0 ||
        motion_selected_track >= static_cast<int>(motion_editor_asset.tracks.size()))
        return nullptr;
    return &motion_editor_asset.tracks[static_cast<std::size_t>(motion_selected_track)];
}

std::vector<int> framework::selected_motion_key_indices() const
{
    std::vector<int> indices = motion_selected_keys;
    if (indices.empty() && motion_selected_key >= 0) indices.push_back(motion_selected_key);
    return indices;
}

bool framework::copy_motion_keys()
{
    MotionTrack* track = selected_motion_track();
    if (track == nullptr)
    {
        motion_editor_status = "コピーするTrackが選択されていません";
        return false;
    }

    motion_key_clipboard.clear();
    const std::vector<int> indices = selected_motion_key_indices();
    for (const int index : indices)
    {
        if (index >= 0 && index < static_cast<int>(track->keys.size()))
            motion_key_clipboard.push_back(track->keys[static_cast<std::size_t>(index)]);
    }
    if (motion_key_clipboard.empty())
    {
        motion_editor_status = "コピーするKeyが選択されていません";
        return false;
    }

    motion_editor_status = "選択Keyをコピーしました";
    return true;
}

bool framework::paste_motion_keys()
{
    MotionTrack* track = selected_motion_track();
    if (track == nullptr || motion_key_clipboard.empty())
    {
        motion_editor_status = "貼り付けるKeyまたはTrackがありません";
        return false;
    }

    float first = motion_key_clipboard.front().time;
    for (const MotionKeyframe& key : motion_key_clipboard)
        first = (std::min)(first, key.time);

    motion_edit_history.Begin(motion_editor_asset, "Keyを貼り付け");
    for (MotionKeyframe key : motion_key_clipboard)
    {
        const float time = SnapMotionTime(motion_preview_time + (key.time - first),
            motion_editor_fps, motion_editor_frame_snap);
        key.time = (std::min)(motion_editor_asset.duration, (std::max)(0.0f, time));
        track->keys.push_back(std::move(key));
    }
    motion_editor_asset.SortKeys();
    motion_edit_history.Commit(motion_editor_asset);
    motion_editor_dirty = true;
    motion_selected_key = -1;
    motion_selected_keys.clear();
    motion_editor_status = "Keyを貼り付けました";
    return true;
}

bool framework::duplicate_motion_keys()
{
    MotionTrack* track = selected_motion_track();
    if (track == nullptr)
    {
        motion_editor_status = "複製するTrackが選択されていません";
        return false;
    }

    const std::vector<int> indices = selected_motion_key_indices();
    const std::size_t original_count = track->keys.size();
    bool has_valid_key = false;
    for (const int index : indices)
    {
        if (index >= 0 && index < static_cast<int>(original_count))
        {
            has_valid_key = true;
            break;
        }
    }
    if (!has_valid_key)
    {
        motion_editor_status = "複製するKeyが選択されていません";
        return false;
    }

    motion_edit_history.Begin(motion_editor_asset, "Keyを複製");
    const float frame_step = FrameStep(motion_editor_fps);
    for (const int index : indices)
    {
        if (index < 0 || index >= static_cast<int>(original_count)) continue;
        MotionKeyframe copy = track->keys[static_cast<std::size_t>(index)];
        const float time = SnapMotionTime(copy.time + frame_step, motion_editor_fps, true);
        copy.time = (std::min)(motion_editor_asset.duration, (std::max)(0.0f, time));
        track->keys.push_back(std::move(copy));
    }
    motion_editor_asset.SortKeys();
    motion_edit_history.Commit(motion_editor_asset);
    motion_editor_dirty = true;
    motion_selected_key = -1;
    motion_selected_keys.clear();
    motion_editor_status = "Keyを複製しました";
    return true;
}

bool framework::delete_motion_keys()
{
    MotionTrack* track = selected_motion_track();
    if (track == nullptr)
    {
        motion_editor_status = "削除するTrackが選択されていません";
        return false;
    }

    std::vector<int> indices = selected_motion_key_indices();
    indices.erase(std::remove_if(indices.begin(), indices.end(),
        [track](int index)
        {
            return index < 0 || index >= static_cast<int>(track->keys.size());
        }), indices.end());
    if (indices.empty())
    {
        motion_editor_status = "削除するKeyが選択されていません";
        return false;
    }

    std::sort(indices.rbegin(), indices.rend());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
    motion_edit_history.Begin(motion_editor_asset, "Keyを削除");
    for (const int index : indices)
        track->keys.erase(track->keys.begin() + index);
    motion_edit_history.Commit(motion_editor_asset);
    motion_editor_dirty = true;
    motion_selected_key = -1;
    motion_selected_keys.clear();
    motion_editor_status = "Keyを削除しました";
    return true;
}

bool framework::apply_motion_easing_to_selection(ReplayEngine::Motion::MotionEasing easing,
    const ReplayEngine::Reflection::AssetReference* curve)
{
    MotionTrack* track = selected_motion_track();
    if (track == nullptr)
    {
        motion_editor_status = "Easingを適用するTrackが選択されていません";
        return false;
    }

    const std::vector<int> indices = selected_motion_key_indices();
    bool has_valid_key = false;
    for (const int index : indices)
    {
        if (index >= 0 && index < static_cast<int>(track->keys.size()))
        {
            has_valid_key = true;
            break;
        }
    }
    if (!has_valid_key)
    {
        motion_editor_status = "Easingを適用するKeyが選択されていません";
        return false;
    }

    motion_edit_history.Begin(motion_editor_asset, "Easingを変更");
    for (const int index : indices)
    {
        if (index < 0 || index >= static_cast<int>(track->keys.size())) continue;
        MotionKeyframe& key = track->keys[static_cast<std::size_t>(index)];
        key.easing = easing;
        if (easing == ReplayEngine::Motion::MotionEasing::PresetCurve && curve != nullptr)
            key.easing_curve = *curve;
    }
    motion_edit_history.Commit(motion_editor_asset);
    motion_editor_dirty = true;
    motion_editor_status = std::string("選択KeyへEasingを適用しました: ") +
        ReplayEngine::Motion::ToString(easing);
    return true;
}

void framework::push_motion_curve_warning_once(const std::string& curve_error)
{
    if (curve_error.empty()) return;
    const std::string marker = "GUID: ";
    const std::size_t marker_pos = curve_error.find(marker);
    const std::string warning_key = marker_pos == std::string::npos
        ? curve_error : curve_error.substr(marker_pos + marker.size());
    if (!motion_easing_curve_warning_guids.insert(warning_key).second) return;
    push_editor_log("Warning", curve_error, motion_editor_path);
}

void framework::toggle_motion_preview_playback()
{
    if (!motion_editor_loaded)
    {
        motion_editor_status = "再生するMotion Assetがありません";
        return;
    }

    if (motion_preview_active)
    {
        stop_motion_preview();
        motion_editor_status = "Motionプレビューを停止しました";
        return;
    }

    capture_motion_preview_targets();
    motion_preview_active = true;
    motion_editor_status = "Motionプレビューを再生しました";
}

void framework::seek_motion_preview_time(float time)
{
    if (!motion_editor_loaded && !motion_composition_loaded) return;
    const float duration = motion_editor_loaded
        ? motion_editor_asset.duration : motion_editor_composition.duration;
    const float safe_time = SnapMotionTime(time, motion_editor_fps, false);
    motion_preview_time = (std::min)((std::max)(0.0f, safe_time),
        (std::max)(0.0f, duration));
    apply_motion_preview_time();
    motion_editor_status = "プレビュー時刻を移動しました";
}

void framework::step_motion_preview_frames(int frames)
{
    if (frames == 0 || (!motion_editor_loaded && !motion_composition_loaded)) return;
    const float duration = motion_editor_loaded
        ? motion_editor_asset.duration : motion_editor_composition.duration;
    const float stepped = SnapMotionTime(
        motion_preview_time + FrameStep(motion_editor_fps) * static_cast<float>(frames),
        motion_editor_fps, true);
    seek_motion_preview_time((std::min)((std::max)(0.0f, stepped),
        (std::max)(0.0f, duration)));
    motion_editor_status = frames > 0
        ? "1フレーム進めました" : "1フレーム戻しました";
}

bool framework::undo_motion_edit()
{
    stop_motion_preview();
    std::string label;
    if (!motion_edit_history.Undo(motion_editor_asset, label)) return false;
    motion_editor_asset.SortKeys();
    motion_selected_track = motion_editor_asset.tracks.empty()
        ? -1 : (std::min)(motion_selected_track,
            static_cast<int>(motion_editor_asset.tracks.size()) - 1);
    motion_selected_key = -1;
    motion_selected_keys.clear();
    motion_selected_event_track = -1;
    motion_selected_event = -1;
    motion_editor_dirty = true;
    if (!motion_editor_guid.empty())
        motion_asset_cache[motion_editor_guid] = motion_editor_asset;
    motion_editor_status = "Undo: " + label;
    return true;
}

bool framework::redo_motion_edit()
{
    stop_motion_preview();
    std::string label;
    if (!motion_edit_history.Redo(motion_editor_asset, label)) return false;
    motion_editor_asset.SortKeys();
    motion_selected_track = motion_editor_asset.tracks.empty()
        ? -1 : (std::min)(motion_selected_track,
            static_cast<int>(motion_editor_asset.tracks.size()) - 1);
    motion_selected_key = -1;
    motion_selected_keys.clear();
    motion_selected_event_track = -1;
    motion_selected_event = -1;
    motion_editor_dirty = true;
    if (!motion_editor_guid.empty())
        motion_asset_cache[motion_editor_guid] = motion_editor_asset;
    motion_editor_status = "Redo: " + label;
    return true;
}

void framework::stop_motion_preview()
{
    ReplayEngine::Scene::Scene* scene = object_editor_context.GetScene();
    if (scene != nullptr)
    {
        for (const MotionPreviewCapture& capture : motion_preview_captures)
        {
            ReplayEngine::Core::GameObject* object =
                scene->FindGameObjectByID(capture.object);
            if (object == nullptr) continue;
            Component* component = object->FindComponentByStableID(capture.component);
            if (component == nullptr) continue;
            PropertyRegistry::Apply(*component, capture.properties);
        }
    }

    motion_preview_captures.clear();
    motion_preview_active = false;
}

void framework::capture_motion_preview_targets()
{
    motion_preview_captures.clear();
    ReplayEngine::Scene::Scene* scene = object_editor_context.GetScene();
    if (scene == nullptr) return;

    auto capture_track = [&](const MotionTrack& track)
    {
        Component* component = ResolveBindingComponent(*scene, track.binding);
        if (component == nullptr || component->Owner() == nullptr) return;
        const auto exists = std::find_if(motion_preview_captures.begin(),
            motion_preview_captures.end(),
            [component](const MotionPreviewCapture& capture)
            {
                return capture.object == component->Owner()->ID() &&
                    capture.component == component->StableID();
            });
        if (exists != motion_preview_captures.end()) return;
        MotionPreviewCapture capture;
        capture.object = component->Owner()->ID();
        capture.component = component->StableID();
        PropertyRegistry::Capture(*component, capture.properties);
        motion_preview_captures.push_back(std::move(capture));
    };

    if (motion_editor_loaded)
    {
        for (const MotionTrack& track : motion_editor_asset.tracks) capture_track(track);
        return;
    }

    if (!motion_composition_loaded) return;
    std::unordered_set<std::string> recursion;
    std::function<void(const ReplayEngine::Motion::CompositionAsset&, int)> collect;
    collect = [&](const ReplayEngine::Motion::CompositionAsset& composition, int depth)
    {
        if (depth > 16) return;
        for (const auto& layer : composition.layers)
        {
            if (!layer.motion_guid.empty())
            {
                const auto* motion = resolve_motion_asset(layer.motion_guid);
                if (motion != nullptr)
                    for (const MotionTrack& track : motion->tracks) capture_track(track);
            }
            else if (!layer.composition_guid.empty() &&
                recursion.insert(layer.composition_guid).second)
            {
                const auto* nested = resolve_composition_asset(layer.composition_guid);
                if (nested != nullptr) collect(*nested, depth + 1);
                recursion.erase(layer.composition_guid);
            }
        }
    };
    collect(motion_editor_composition, 0);
}

void framework::apply_motion_preview_time()
{
    ReplayEngine::Scene::Scene* scene = object_editor_context.GetScene();
    if (scene == nullptr || (!motion_editor_loaded && !motion_composition_loaded)) return;
    if (motion_preview_captures.empty()) capture_motion_preview_targets();

    motion_mixer.BeginFrame();
    if (motion_editor_loaded)
    {
        for (const MotionTrack& track : motion_editor_asset.tracks)
        {
            PropertyValue value;
            std::string curve_error;
            if (!MotionEvaluator::EvaluateTrack(track, motion_preview_time, value,
                &asset_database, &curve_error))
            {
                push_motion_curve_warning_once(curve_error);
                continue;
            }
            push_motion_curve_warning_once(curve_error);
            const ReplayEngine::Motion::ResolvedMotionBinding binding =
                ReplayEngine::Motion::MotionBindingResolver::Resolve(*scene, track.binding);
            motion_mixer.Contribute(binding, value, 1.0f, track.blend_mode);
        }
        motion_mixer.Apply();
        return;
    }

    std::unordered_set<std::string> recursion;
    std::function<void(const ReplayEngine::Motion::CompositionAsset&, float, float, int)> apply;
    apply = [&](const ReplayEngine::Motion::CompositionAsset& composition,
        float composition_time, float parent_weight, int depth)
    {
        if (depth > 16 || parent_weight <= 0.0f) return;
        for (const auto& layer : composition.layers)
        {
            if (!layer.enabled || layer.weight <= 0.0f || composition_time < layer.in_time ||
                (layer.out_time >= 0.0f && composition_time > layer.out_time)) continue;
            const float source_time = (composition_time - layer.start_offset) * layer.time_scale;
            const float weight = parent_weight * layer.weight;
            if (!layer.motion_guid.empty())
            {
                const auto* motion = resolve_motion_asset(layer.motion_guid);
                if (motion == nullptr) continue;
                const float t = (std::max)(0.0f, (std::min)(motion->duration, source_time));
                for (const MotionTrack& track : motion->tracks)
                {
                    PropertyValue value;
                    std::string curve_error;
                    if (!MotionEvaluator::EvaluateTrack(track, t, value,
                        &asset_database, &curve_error))
                    {
                        push_motion_curve_warning_once(curve_error);
                        continue;
                    }
                    push_motion_curve_warning_once(curve_error);
                    const auto binding = ReplayEngine::Motion::MotionBindingResolver::Resolve(
                        *scene, track.binding);
                    motion_mixer.Contribute(binding, value, weight, track.blend_mode);
                }
            }
            else if (!layer.composition_guid.empty() &&
                recursion.insert(layer.composition_guid).second)
            {
                const auto* nested = resolve_composition_asset(layer.composition_guid);
                if (nested != nullptr)
                {
                    const float t = (std::max)(0.0f,
                        (std::min)(nested->duration, source_time));
                    apply(*nested, t, weight, depth + 1);
                }
                recursion.erase(layer.composition_guid);
            }
        }
    };
    apply(motion_editor_composition, motion_preview_time, 1.0f, 0);
    motion_mixer.Apply();
}
