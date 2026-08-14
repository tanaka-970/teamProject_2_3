// Editor Camera Preset UI のうち、読込・選択・保存の状態管理だけを持つ。
//
//   framework_editor_camera_presets.cpp    ... Preset state 管理（このファイル）
//   framework_editor_camera_preset_ui.cpp  ... Top menu と Preset manager UI

#include "framework.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>

void framework::ensure_editor_camera_presets_loaded()
{
    using namespace ReplayEngine::Editor;
    if (editor_camera_presets_loaded) return;
    editor_camera_presets_loaded = true;

    std::string load_error;
    editor_camera_presets = EditorCameraPresetStore::LoadAll(load_error);

    // Preset files が消されても Editor を起動不能にしない。
    if (editor_camera_presets.empty())
    {
        EditorCameraPreset fallback;
        fallback.id = EditorCameraPresetStore::MakeUniqueId();
        fallback.name = "My RePlay Hybrid";
        fallback.scope = EditorCameraPresetScope::Personal;
        std::string save_error;
        EditorCameraPresetStore::Save(fallback, save_error);
        editor_camera_presets.push_back(fallback);
    }

    std::string active_id;
    std::string active_error;
    const bool had_active_selection =
        EditorCameraPresetStore::LoadActivePresetId(active_id, active_error);

    int selected = -1;
    if (had_active_selection)
    {
        for (std::size_t i = 0; i < editor_camera_presets.size(); ++i)
        {
            if (editor_camera_presets[i].id == active_id)
            {
                selected = static_cast<int>(i);
                break;
            }
        }
    }

    // 初回は RePlay Hybrid を選ぶ。旧 speed setting があれば personal copy へ移行。
    if (selected < 0)
    {
        for (std::size_t i = 0; i < editor_camera_presets.size(); ++i)
        {
            if (editor_camera_presets[i].id == "builtin_replay_hybrid")
            {
                selected = static_cast<int>(i);
                break;
            }
        }
        if (selected < 0) selected = 0;

        float legacy_speed = 0.0f;
        std::string legacy_error;
        if (EditorCameraStateStore::LoadMoveSpeedPreference(legacy_speed, legacy_error) &&
            legacy_speed > 0.0f)
        {
            EditorCameraPreset migrated = EditorCameraPresetStore::DuplicateAsPersonal(
                editor_camera_presets[static_cast<std::size_t>(selected)], "My RePlay Hybrid");
            migrated.move_speed = legacy_speed;
            std::string save_error;
            if (EditorCameraPresetStore::Save(migrated, save_error))
            {
                editor_camera_presets.push_back(migrated);
                selected = static_cast<int>(editor_camera_presets.size() - 1);
            }
        }
    }

    active_editor_camera_preset_index = selected;
    editor_camera_presets[static_cast<std::size_t>(selected)].ApplyCameraSettings(editor_camera);
    std::string save_selection_error;
    EditorCameraPresetStore::SaveActivePresetId(
        editor_camera_presets[static_cast<std::size_t>(selected)].id,
        save_selection_error);
}

ReplayEngine::Editor::EditorCameraPreset& framework::active_editor_camera_preset()
{
    ensure_editor_camera_presets_loaded();
    if (active_editor_camera_preset_index < 0 ||
        active_editor_camera_preset_index >= static_cast<int>(editor_camera_presets.size()))
    {
        active_editor_camera_preset_index = 0;
    }
    return editor_camera_presets[static_cast<std::size_t>(active_editor_camera_preset_index)];
}

const ReplayEngine::Editor::EditorCameraPreset& framework::active_editor_camera_preset() const
{
    // const path は描画表示からのみ使用。preset 未読込の状態では先頭を返せないため、
    // 呼び出し側は ensure_editor_camera_presets_loaded() 後に使う。
    static const ReplayEngine::Editor::EditorCameraPreset fallback{};
    if (active_editor_camera_preset_index < 0 ||
        active_editor_camera_preset_index >= static_cast<int>(editor_camera_presets.size()))
        return fallback;
    return editor_camera_presets[static_cast<std::size_t>(active_editor_camera_preset_index)];
}

bool framework::switch_editor_camera_preset(const std::string& preset_id)
{
    using namespace ReplayEngine::Editor;
    ensure_editor_camera_presets_loaded();
    for (std::size_t i = 0; i < editor_camera_presets.size(); ++i)
    {
        if (editor_camera_presets[i].id != preset_id) continue;
        active_editor_camera_preset_index = static_cast<int>(i);
        editor_camera_presets[i].ApplyCameraSettings(editor_camera);
        editor_camera_controller.Cancel();
        std::string error;
        EditorCameraPresetStore::SaveActivePresetId(preset_id, error);
        return true;
    }
    return false;
}

bool framework::save_active_editor_camera_preset()
{
    using namespace ReplayEngine::Editor;
    auto& preset = active_editor_camera_preset();
    if (!preset.Editable()) return false;
    preset.CaptureCameraSettings(editor_camera);
    std::string error;
    return EditorCameraPresetStore::Save(preset, error);
}

bool framework::make_active_editor_camera_preset_personal_copy()
{
    using namespace ReplayEngine::Editor;
    auto& source = active_editor_camera_preset();
    if (source.Editable()) return true;

    EditorCameraPreset copy = EditorCameraPresetStore::DuplicateAsPersonal(
        source, source.name + " - My");
    std::string error;
    if (!EditorCameraPresetStore::Save(copy, error)) return false;
    editor_camera_presets.push_back(copy);
    return switch_editor_camera_preset(copy.id);
}
