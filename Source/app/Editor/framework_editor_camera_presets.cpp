#include "framework.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace
{
    using namespace ReplayEngine::Editor;

    std::string ChordText(const EditorCameraKeyChord& chord)
    {
        if (chord.Empty()) return "None";
        std::string text;
        if (chord.control) text += "Ctrl+";
        if (chord.shift) text += "Shift+";
        if (chord.alt) text += "Alt+";
        text += EditorCameraPresetStore::KeyName(chord.key);
        return text;
    }

    std::string GestureText(const EditorCameraMouseGesture& gesture)
    {
        if (gesture.Empty()) return "None";
        std::string text;
        if (gesture.control) text += "Ctrl+";
        if (gesture.shift) text += "Shift+";
        if (gesture.alt) text += "Alt+";
        text += EditorCameraPresetStore::MouseButtonName(gesture.button);
        return text;
    }

#ifdef USE_IMGUI
    bool DrawKeyChordEditor(const char* label, EditorCameraKeyChord& chord)
    {
        bool changed = false;
        ImGui::PushID(label);
        ImGui::TextUnformatted(label);
        ImGui::SameLine(210.0f);
        ImGui::SetNextItemWidth(130.0f);
        const char* preview = EditorCameraPresetStore::KeyName(chord.key);
        if (ImGui::BeginCombo("##Key", preview))
        {
            for (std::size_t i = 0; i < static_cast<std::size_t>(EditorCameraKey::Count); ++i)
            {
                const auto key = static_cast<EditorCameraKey>(i);
                const bool selected = chord.key == key;
                if (ImGui::Selectable(EditorCameraPresetStore::KeyName(key), selected))
                {
                    chord.key = key;
                    changed = true;
                }
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        changed |= ImGui::Checkbox("Shift", &chord.shift);
        ImGui::SameLine();
        changed |= ImGui::Checkbox("Ctrl", &chord.control);
        ImGui::SameLine();
        changed |= ImGui::Checkbox("Alt", &chord.alt);
        ImGui::PopID();
        return changed;
    }

    bool DrawMouseGestureEditor(const char* label, EditorCameraMouseGesture& gesture)
    {
        bool changed = false;
        ImGui::PushID(label);
        ImGui::TextUnformatted(label);
        ImGui::SameLine(210.0f);
        ImGui::SetNextItemWidth(130.0f);
        const char* preview = EditorCameraPresetStore::MouseButtonName(gesture.button);
        if (ImGui::BeginCombo("##Mouse", preview))
        {
            const EditorCameraMouseButton values[] =
            {
                EditorCameraMouseButton::None,
                EditorCameraMouseButton::Left,
                EditorCameraMouseButton::Middle,
                EditorCameraMouseButton::Right,
            };
            for (const auto value : values)
            {
                const bool selected = gesture.button == value;
                if (ImGui::Selectable(EditorCameraPresetStore::MouseButtonName(value), selected))
                {
                    gesture.button = value;
                    changed = true;
                }
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        changed |= ImGui::Checkbox("Shift", &gesture.shift);
        ImGui::SameLine();
        changed |= ImGui::Checkbox("Ctrl", &gesture.control);
        ImGui::SameLine();
        changed |= ImGui::Checkbox("Alt", &gesture.alt);
        ImGui::PopID();
        return changed;
    }

    bool DrawModifierEditor(const char* label, EditorCameraModifier& modifier)
    {
        bool changed = false;
        ImGui::TextUnformatted(label);
        ImGui::SameLine(210.0f);
        ImGui::SetNextItemWidth(130.0f);
        if (ImGui::BeginCombo((std::string("##") + label).c_str(),
            EditorCameraPresetStore::ModifierName(modifier)))
        {
            const EditorCameraModifier values[] =
            {
                EditorCameraModifier::None,
                EditorCameraModifier::Shift,
                EditorCameraModifier::Control,
                EditorCameraModifier::Alt,
            };
            for (const auto value : values)
            {
                const bool selected = modifier == value;
                if (ImGui::Selectable(EditorCameraPresetStore::ModifierName(value), selected))
                {
                    modifier = value;
                    changed = true;
                }
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        return changed;
    }
#endif
}

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

void framework::draw_editor_camera_top_menu()
{
#ifdef USE_IMGUI
    using namespace ReplayEngine::Editor;
    ensure_editor_camera_presets_loaded();
    auto& preset = active_editor_camera_preset();

    ImGui::TextDisabled(u8"操作プリセット");
    if (ImGui::BeginMenu(preset.name.c_str()))
    {
        for (const auto& item : editor_camera_presets)
        {
            std::string label = item.name;
            label += item.scope == EditorCameraPresetScope::Shared ? "  [Shared]" : "  [Personal]";
            if (ImGui::MenuItem(label.c_str(), nullptr, item.id == preset.id))
                switch_editor_camera_preset(item.id);
        }
        ImGui::EndMenu();
    }
    if (ImGui::MenuItem(u8"プリセット管理...")) show_camera_preset_manager = true;

    ImGui::Separator();
    ImGui::TextDisabled(u8"移動速度（active preset）");
    ImGui::Text(u8"現在: %.3f", editor_camera.move_speed);
    const float speed_presets[] =
    {
        0.5f, 1.0f, 2.5f, 5.0f, 10.0f, 25.0f,
        50.0f, 100.0f, 250.0f, 500.0f, 1000.0f
    };
    for (const float speed : speed_presets)
    {
        char label[32]{};
        if (speed < 10.0f) std::snprintf(label, sizeof(label), "%.1f", speed);
        else std::snprintf(label, sizeof(label), "%.0f", speed);
        if (ImGui::MenuItem(label, nullptr,
            std::fabs(editor_camera.move_speed - speed) < 0.0001f))
        {
            if (!active_editor_camera_preset().Editable())
                make_active_editor_camera_preset_personal_copy();
            editor_camera.move_speed = speed;
            save_active_editor_camera_preset();
        }
    }

    ImGui::Separator();
    ImGui::TextDisabled(u8"自由入力（上限なし）");
    ImGui::SetNextItemWidth(180.0f);
    float speed = editor_camera.move_speed;
    if (ImGui::DragFloat("##DebugCameraMoveSpeed", &speed, 0.25f, 0.0f, 0.0f, "%.3f"))
    {
        if (speed > 0.0f && std::isfinite(speed))
        {
            if (!active_editor_camera_preset().Editable())
                make_active_editor_camera_preset_personal_copy();
            editor_camera.move_speed = speed;
            save_active_editor_camera_preset();
        }
    }

    ImGui::Separator();
    ImGui::TextDisabled(u8"他人の Shared preset は選択だけ。編集時は My copy を作ります");
#endif
}

void framework::draw_editor_camera_preset_manager()
{
#ifdef USE_IMGUI
    if (!show_camera_preset_manager) return;
    using namespace ReplayEngine::Editor;
    ensure_editor_camera_presets_loaded();

    if (!ImGui::Begin(u8"カメラ操作プリセット", &show_camera_preset_manager))
    {
        ImGui::End();
        return;
    }

    auto& preset = active_editor_camera_preset();
    ImGui::TextDisabled(u8"Active preset");
    ImGui::SetNextItemWidth(320.0f);
    if (ImGui::BeginCombo("##ActiveCameraPreset", preset.name.c_str()))
    {
        for (const auto& item : editor_camera_presets)
        {
            std::string label = item.name;
            label += item.scope == EditorCameraPresetScope::Shared ? "  [Shared]" : "  [Personal]";
            const bool selected = item.id == preset.id;
            if (ImGui::Selectable(label.c_str(), selected)) switch_editor_camera_preset(item.id);
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    if (ImGui::Button(u8"複製して自分用にする"))
        make_active_editor_camera_preset_personal_copy();
    ImGui::SameLine();
    if (ImGui::Button(u8"新規プリセット"))
    {
        EditorCameraPreset created = EditorCameraPresetStore::DuplicateAsPersonal(
            active_editor_camera_preset(), "My Camera");
        std::string error;
        if (EditorCameraPresetStore::Save(created, error))
        {
            editor_camera_presets.push_back(created);
            switch_editor_camera_preset(created.id);
        }
    }

    // switch / duplicate で vector が増える可能性があるので、reference を push 後へ持ち越さない。
    const bool current_editable = active_editor_camera_preset().Editable();
    ImGui::SameLine();
    if (current_editable && ImGui::Button(u8"チーム共有コピーを作る"))
    {
        const EditorCameraPreset source_copy = active_editor_camera_preset();
        EditorCameraPreset shared;
        std::string error;
        if (EditorCameraPresetStore::PublishSharedCopy(source_copy,
            source_copy.name + " Shared", shared, error))
        {
            editor_camera_presets.push_back(shared);
        }
    }
    ImGui::SameLine();
    if (active_editor_camera_preset().Editable() && ImGui::Button(u8"削除"))
    {
        const EditorCameraPreset deleting = active_editor_camera_preset();
        const std::string deleting_id = deleting.id;
        std::string error;
        if (EditorCameraPresetStore::DeletePersonal(deleting, error))
        {
            editor_camera_presets.erase(std::remove_if(editor_camera_presets.begin(),
                editor_camera_presets.end(), [&](const EditorCameraPreset& item)
            {
                return item.id == deleting_id;
            }), editor_camera_presets.end());
            active_editor_camera_preset_index = -1;
            if (!editor_camera_presets.empty())
                switch_editor_camera_preset(editor_camera_presets.front().id);
        }
    }

    auto& edit = active_editor_camera_preset();
    ImGui::Separator();

    if (!edit.Editable())
    {
        ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.25f, 1.0f),
            u8"Shared preset は読み取り専用。『複製して自分用にする』で自由編集できます。");
        ImGui::Text(u8"名前: %s", edit.name.c_str());
        ImGui::Text(u8"Look: %s", GestureText(edit.look).c_str());
        ImGui::Text(u8"Orbit: %s", GestureText(edit.orbit).c_str());
        ImGui::Text(u8"Pan: %s", GestureText(edit.pan).c_str());
        ImGui::Text(u8"Dolly: %s", GestureText(edit.dolly).c_str());
        ImGui::Text(u8"Gizmo Move/Rotate/Scale: %s / %s / %s",
            ChordText(edit.gizmo_move).c_str(), ChordText(edit.gizmo_rotate).c_str(),
            ChordText(edit.gizmo_scale).c_str());
        ImGui::End();
        return;
    }

    bool changed = false;
    static std::array<char, 128> name_buffer{};
    static std::string buffered_id;
    if (buffered_id != edit.id)
    {
        buffered_id = edit.id;
        std::snprintf(name_buffer.data(), name_buffer.size(), "%s", edit.name.c_str());
    }
    ImGui::TextUnformatted(u8"プリセット名");
    ImGui::SameLine(210.0f);
    ImGui::SetNextItemWidth(320.0f);
    if (ImGui::InputText("##CameraPresetName", name_buffer.data(), name_buffer.size(),
        ImGuiInputTextFlags_EnterReturnsTrue))
    {
        if (name_buffer[0] != '\0')
        {
            edit.name = name_buffer.data();
            changed = true;
        }
    }

    if (ImGui::CollapsingHeader(u8"移動・速度", ImGuiTreeNodeFlags_DefaultOpen))
    {
        const float previous_speed = edit.move_speed;
        changed |= ImGui::DragFloat(u8"移動速度（上限なし）", &edit.move_speed,
            0.25f, 0.0f, 0.0f, "%.3f");
        if (!(edit.move_speed > 0.0f) || !std::isfinite(edit.move_speed)) edit.move_speed = previous_speed;
        changed |= ImGui::DragFloat(u8"高速倍率", &edit.fast_multiplier, 0.1f, 0.01f, 100.0f, "%.2f");
        changed |= ImGui::DragFloat(u8"低速倍率", &edit.slow_multiplier, 0.01f, 0.001f, 1.0f, "%.3f");
        changed |= DrawModifierEditor(u8"高速移動 modifier", edit.fast_modifier);
        changed |= DrawModifierEditor(u8"低速移動 modifier", edit.slow_modifier);
        changed |= ImGui::Checkbox(u8"Look中でなくてもキーボード移動", &edit.keyboard_fly_without_look);
        changed |= ImGui::Checkbox(u8"Q/E はワールド上下へ移動", &edit.world_vertical_move);
        changed |= ImGui::Checkbox(u8"Look中 Wheel = 移動速度変更", &edit.wheel_changes_speed_while_look);
        changed |= DrawKeyChordEditor(u8"前へ", edit.move_forward);
        changed |= DrawKeyChordEditor(u8"後ろへ", edit.move_back);
        changed |= DrawKeyChordEditor(u8"左へ", edit.move_left);
        changed |= DrawKeyChordEditor(u8"右へ", edit.move_right);
        changed |= DrawKeyChordEditor(u8"上へ", edit.move_up);
        changed |= DrawKeyChordEditor(u8"下へ", edit.move_down);
    }

    if (ImGui::CollapsingHeader(u8"マウスナビゲーション", ImGuiTreeNodeFlags_DefaultOpen))
    {
        changed |= DrawMouseGestureEditor(u8"Look / 視点回転", edit.look);
        changed |= DrawMouseGestureEditor(u8"Orbit / 注視点回転", edit.orbit);
        changed |= DrawMouseGestureEditor(u8"Pan / 平行移動", edit.pan);
        changed |= DrawMouseGestureEditor(u8"Dolly / ドラッグズーム", edit.dolly);
        changed |= ImGui::DragFloat(u8"回転感度", &edit.mouse_sensitivity, 0.01f, 0.001f, 5.0f, "%.3f");
        changed |= ImGui::DragFloat(u8"Pan感度", &edit.pan_sensitivity, 0.01f, 0.001f, 20.0f, "%.3f");
        changed |= ImGui::DragFloat(u8"Zoom/Dolly感度", &edit.zoom_sensitivity, 0.01f, 0.001f, 20.0f, "%.3f");
        changed |= ImGui::Checkbox(u8"Look X反転", &edit.invert_look_x); ImGui::SameLine();
        changed |= ImGui::Checkbox(u8"Look Y反転", &edit.invert_look_y);
        changed |= ImGui::Checkbox(u8"Orbit X反転", &edit.invert_orbit_x); ImGui::SameLine();
        changed |= ImGui::Checkbox(u8"Orbit Y反転", &edit.invert_orbit_y);
        changed |= ImGui::Checkbox(u8"Pan X反転", &edit.invert_pan_x); ImGui::SameLine();
        changed |= ImGui::Checkbox(u8"Pan Y反転", &edit.invert_pan_y);
        changed |= ImGui::Checkbox(u8"Dolly反転", &edit.invert_dolly);
    }

    if (ImGui::CollapsingHeader(u8"キーボード回転・View Snap", ImGuiTreeNodeFlags_DefaultOpen))
    {
        changed |= ImGui::DragFloat(u8"キーボード回転速度 deg/s",
            &edit.keyboard_rotation_degrees, 1.0f, 1.0f, 1440.0f, "%.1f");
        changed |= DrawKeyChordEditor(u8"Yaw 左", edit.rotate_yaw_left);
        changed |= DrawKeyChordEditor(u8"Yaw 右", edit.rotate_yaw_right);
        changed |= DrawKeyChordEditor(u8"Pitch 上", edit.rotate_pitch_up);
        changed |= DrawKeyChordEditor(u8"Pitch 下", edit.rotate_pitch_down);
        changed |= DrawKeyChordEditor(u8"Roll 左", edit.rotate_roll_left);
        changed |= DrawKeyChordEditor(u8"Roll 右", edit.rotate_roll_right);
        ImGui::Separator();
        changed |= DrawKeyChordEditor(u8"Front View", edit.view_front);
        changed |= DrawKeyChordEditor(u8"Back View", edit.view_back);
        changed |= DrawKeyChordEditor(u8"Right View", edit.view_right);
        changed |= DrawKeyChordEditor(u8"Left View", edit.view_left);
        changed |= DrawKeyChordEditor(u8"Top View", edit.view_top);
        changed |= DrawKeyChordEditor(u8"Bottom View", edit.view_bottom);
        changed |= DrawKeyChordEditor(u8"Focus Selection", edit.focus);
    }

    if (ImGui::CollapsingHeader(u8"Gizmo ショートカット", ImGuiTreeNodeFlags_DefaultOpen))
    {
        changed |= DrawKeyChordEditor(u8"Move Gizmo", edit.gizmo_move);
        changed |= DrawKeyChordEditor(u8"Rotate Gizmo", edit.gizmo_rotate);
        changed |= DrawKeyChordEditor(u8"Scale Gizmo", edit.gizmo_scale);
    }

    if (ImGui::CollapsingHeader(u8"表示設定"))
    {
        changed |= ImGui::DragFloat(u8"FOV", &edit.field_of_view_degrees, 0.5f, 5.0f, 170.0f, "%.1f");
        changed |= ImGui::DragFloat(u8"Near Clip", &edit.near_clip, 0.01f, 0.001f, 10.0f, "%.3f");
        changed |= ImGui::DragFloat(u8"Far Clip", &edit.far_clip, 10.0f, 10.0f, 1000000.0f, "%.0f");
    }

    if (changed)
    {
        edit.Sanitize();
        edit.ApplyCameraSettings(editor_camera);
        save_active_editor_camera_preset();
    }

    ImGui::Separator();
    ImGui::TextDisabled(u8"追加済み: Maya/Unity/Unreal/Blender/Hybrid、Dolly、Keyboard Rotate、Roll、6方向View Snap");
    ImGui::TextDisabled(u8"Active preset の選択は Saved/Editor に保存。Shared preset は Editor/CameraPresets から読みます。");
    ImGui::End();
#else
    (void)show_camera_preset_manager;
#endif
}
