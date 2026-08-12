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
#include <string>
#include <utility>
#include <vector>

#include "framework_motion_workspaceInternal.h"
using namespace framework_motion_workspace::Detail;

// 編集操作とモーションプレビュー状態の関数本体

bool framework::open_motion_asset(const ReplayEngine::Assets::AssetRecord& asset)
{
    using ReplayEngine::Assets::AssetKind;
    if (asset.kind != AssetKind::Motion) return false;

    stop_motion_preview();
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

    for (const MotionTrack& track : motion_editor_asset.tracks)
    {
        Component* component = ResolveBindingComponent(*scene, track.binding);
        if (component == nullptr || component->Owner() == nullptr) continue;

        const auto exists = std::find_if(motion_preview_captures.begin(),
            motion_preview_captures.end(),
            [component](const MotionPreviewCapture& capture)
            {
                return capture.object == component->Owner()->ID() &&
                    capture.component == component->StableID();
            });
        if (exists != motion_preview_captures.end()) continue;

        MotionPreviewCapture capture;
        capture.object = component->Owner()->ID();
        capture.component = component->StableID();
        PropertyRegistry::Capture(*component, capture.properties);
        motion_preview_captures.push_back(std::move(capture));
    }
}

void framework::apply_motion_preview_time()
{
    ReplayEngine::Scene::Scene* scene = object_editor_context.GetScene();
    if (scene == nullptr || !motion_editor_loaded) return;
    if (motion_preview_captures.empty()) capture_motion_preview_targets();

    motion_mixer.BeginFrame();
    for (const MotionTrack& track : motion_editor_asset.tracks)
    {
        PropertyValue value;
        if (!MotionEvaluator::EvaluateTrack(track, motion_preview_time, value)) continue;
        const ReplayEngine::Motion::ResolvedMotionBinding binding =
            ReplayEngine::Motion::MotionBindingResolver::Resolve(*scene, track.binding);
        motion_mixer.Contribute(binding, value, 1.0f, track.blend_mode);
    }
    motion_mixer.Apply();
}
