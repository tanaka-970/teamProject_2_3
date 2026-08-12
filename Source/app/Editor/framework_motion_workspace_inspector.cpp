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

// Motion Inspector 描画の関数本体

void framework::draw_motion_inspector()
{
    if (!show_motion_inspector_panel) return;
    if (!ImGui::Begin("Motion インスペクター", &show_motion_inspector_panel))
    {
        ImGui::End();
        return;
    }

    if (!motion_editor_loaded)
    {
        ImGui::TextDisabled("Motion Asset が未選択です。");
        ImGui::End();
        return;
    }

    char name_buffer[256]{};
    strncpy_s(name_buffer, motion_editor_asset.name.c_str(), _TRUNCATE);
    if (ImGui::InputText("名前", name_buffer, IM_ARRAYSIZE(name_buffer)))
    {
        motion_edit_history.Begin(motion_editor_asset, "Motion名を変更");
        motion_editor_asset.name = name_buffer;
        motion_edit_history.Commit(motion_editor_asset);
        motion_editor_dirty = true;
    }
    float duration = motion_editor_asset.duration;
    if (ImGui::DragFloat("長さ", &duration, 0.01f, 0.0f, 3600.0f, "%.2f s"))
    {
        motion_edit_history.Begin(motion_editor_asset, "Motion長さを変更");
        motion_editor_asset.duration = (std::max)(0.0f, duration);
        motion_edit_history.Commit(motion_editor_asset);
        motion_editor_dirty = true;
    }

    ImGui::Separator();
    if (ImGui::Button("Event Track追加"))
    {
        stop_motion_preview();
        motion_edit_history.Begin(motion_editor_asset, "Event Trackを追加");
        MotionEventTrack event_track;
        ReplayEngine::Scene::Scene* scene = object_editor_context.GetScene();
        if (scene != nullptr)
        {
            if (ReplayEngine::Core::GameObject* selected =
                object_editor_context.Selection().ResolvePrimary(*scene))
            {
                event_track.object = selected->ID();
            }
        }
        motion_editor_asset.event_tracks.push_back(std::move(event_track));
        motion_selected_event_track =
            static_cast<int>(motion_editor_asset.event_tracks.size()) - 1;
        motion_selected_event = -1;
        motion_selected_track = -1;
        motion_selected_key = -1;
        motion_edit_history.Commit(motion_editor_asset);
        motion_editor_dirty = true;
    }

    if (motion_selected_event_track >= 0 &&
        motion_selected_event_track < static_cast<int>(motion_editor_asset.event_tracks.size()))
    {
        MotionEventTrack& event_track =
            motion_editor_asset.event_tracks[motion_selected_event_track];
        ImGui::Separator();
        ImGui::Text("Event Track %d", motion_selected_event_track + 1);
        ImGui::Text("送信先 ObjectID: %s", event_track.object.Valid()
            ? event_track.object.ToString().c_str() : "(なし / broadcast)");

        if (ImGui::Button("選択中Objectを送信先にする"))
        {
            ReplayEngine::Scene::Scene* scene = object_editor_context.GetScene();
            ReplayEngine::Core::GameObject* selected = scene != nullptr
                ? object_editor_context.Selection().ResolvePrimary(*scene) : nullptr;
            if (selected != nullptr)
            {
                motion_edit_history.Begin(motion_editor_asset, "Event送信先を変更");
                event_track.object = selected->ID();
                motion_edit_history.Commit(motion_editor_asset);
                motion_editor_dirty = true;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("送信先なし"))
        {
            motion_edit_history.Begin(motion_editor_asset, "Event送信先を解除");
            event_track.object = ReplayEngine::Core::ObjectID::Invalid();
            motion_edit_history.Commit(motion_editor_asset);
            motion_editor_dirty = true;
        }

        if (ImGui::Button("Event追加"))
        {
            motion_edit_history.Begin(motion_editor_asset, "Eventを追加");
            MotionEvent event;
            event.time = (std::max)(0.0f,
                (std::min)(motion_editor_asset.duration, motion_preview_time));
            event.name = "PlaySound";
            event_track.events.push_back(std::move(event));
            motion_editor_asset.SortKeys();
            motion_selected_event = -1;
            for (int i = 0; i < static_cast<int>(event_track.events.size()); ++i)
            {
                if (event_track.events[i].time == motion_preview_time)
                    motion_selected_event = i;
            }
            motion_edit_history.Commit(motion_editor_asset);
            motion_editor_dirty = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Event Track削除"))
        {
            stop_motion_preview();
            motion_edit_history.Begin(motion_editor_asset, "Event Trackを削除");
            motion_editor_asset.event_tracks.erase(
                motion_editor_asset.event_tracks.begin() + motion_selected_event_track);
            motion_selected_event_track = -1;
            motion_selected_event = -1;
            motion_edit_history.Commit(motion_editor_asset);
            motion_editor_dirty = true;
            ImGui::End();
            return;
        }

        if (motion_selected_event >= 0 &&
            motion_selected_event < static_cast<int>(event_track.events.size()))
        {
            MotionEvent& event = event_track.events[motion_selected_event];
            ImGui::Separator();
            ImGui::Text("Event %d", motion_selected_event + 1);
            float event_time = event.time;
            if (ImGui::DragFloat("Event時刻", &event_time, 0.01f, 0.0f,
                motion_editor_asset.duration))
            {
                motion_edit_history.Begin(motion_editor_asset, "Event時刻を変更");
                event.time = (std::max)(0.0f,
                    (std::min)(motion_editor_asset.duration, event_time));
                motion_editor_asset.SortKeys();
                motion_selected_event = -1;
                motion_edit_history.Commit(motion_editor_asset);
                motion_editor_dirty = true;
                ImGui::End();
                return;
            }

            char event_name[256]{};
            strncpy_s(event_name, event.name.c_str(), _TRUNCATE);
            if (ImGui::InputText("Event名", event_name, IM_ARRAYSIZE(event_name)))
            {
                motion_edit_history.Begin(motion_editor_asset, "Event名を変更");
                event.name = event_name;
                motion_edit_history.Commit(motion_editor_asset);
                motion_editor_dirty = true;
            }
            char event_parameter[512]{};
            strncpy_s(event_parameter, event.parameter.c_str(), _TRUNCATE);
            if (ImGui::InputText("Parameter", event_parameter,
                IM_ARRAYSIZE(event_parameter)))
            {
                motion_edit_history.Begin(motion_editor_asset, "Event Parameterを変更");
                event.parameter = event_parameter;
                motion_edit_history.Commit(motion_editor_asset);
                motion_editor_dirty = true;
            }
            if (ImGui::Button("Event削除"))
            {
                motion_edit_history.Begin(motion_editor_asset, "Eventを削除");
                event_track.events.erase(event_track.events.begin() + motion_selected_event);
                motion_selected_event = -1;
                motion_edit_history.Commit(motion_editor_asset);
                motion_editor_dirty = true;
            }
        }

        ImGui::TextDisabled("Event Track は値を持たないため Graph Editor には表示しません。");
        ImGui::End();
        return;
    }

    if (motion_selected_track < 0 ||
        motion_selected_track >= static_cast<int>(motion_editor_asset.tracks.size()))
    {
        ImGui::TextDisabled("Track または Event Track を選択してください。");
        ImGui::End();
        return;
    }

    MotionTrack& track = motion_editor_asset.tracks[motion_selected_track];
    ImGui::Separator();
    char track_name[256]{};
    strncpy_s(track_name, track.name.c_str(), _TRUNCATE);
    if (ImGui::InputText("Track名", track_name, IM_ARRAYSIZE(track_name)))
    {
        motion_edit_history.Begin(motion_editor_asset, "Track名を変更");
        track.name = track_name;
        motion_edit_history.Commit(motion_editor_asset);
        motion_editor_dirty = true;
    }
    if (ImGui::Checkbox("有効", &track.enabled))
    {
        motion_edit_history.Begin(motion_editor_asset, "Track有効を変更");
        motion_edit_history.Commit(motion_editor_asset);
        motion_editor_dirty = true;
    }
    MotionBlendMode blend_mode = track.blend_mode;
    if (DrawBlendModeCombo("Blend Mode", blend_mode))
    {
        motion_edit_history.Begin(motion_editor_asset, "Track Blend Modeを変更");
        track.blend_mode = blend_mode;
        motion_edit_history.Commit(motion_editor_asset);
        motion_editor_dirty = true;
    }

    int binding_origin = track.binding.origin;
    if (DrawMotionBindingOriginCombo("バインド起点", binding_origin) &&
        binding_origin != track.binding.origin)
    {
        motion_edit_history.Begin(motion_editor_asset, "Motionの起点を変更");
        track.binding.origin = binding_origin;
        motion_edit_history.Commit(motion_editor_asset);
        motion_editor_dirty = true;
    }
    if (track.binding.origin == static_cast<int>(MotionBindingOrigin::ChildPath))
    {
        char relative_path[512]{};
        strncpy_s(relative_path, track.binding.relative_path.c_str(), _TRUNCATE);
        if (ImGui::InputText("子への相対パス", relative_path,
            IM_ARRAYSIZE(relative_path)))
        {
            motion_edit_history.Begin(motion_editor_asset, "Motionの子パスを変更");
            track.binding.relative_path = relative_path;
            motion_edit_history.Commit(motion_editor_asset);
            motion_editor_dirty = true;
        }
    }

    ReplayEngine::Scene::Scene* scene = object_editor_context.GetScene();
    Component* bound_component = scene != nullptr
        ? ResolveBindingComponent(*scene, track.binding) : nullptr;
    ImGui::Text("ObjectID: %s", track.binding.object.ToString().c_str());
    ImGui::Text("Property: %s", track.binding.property.c_str());
    if (bound_component == nullptr) ImGui::TextColored(
        ImVec4(1.0f, 0.55f, 0.35f, 1.0f), "Binding未解決");

    if (ImGui::Button("Key追加"))
    {
        motion_edit_history.Begin(motion_editor_asset, "Keyを追加");
        MotionKeyframe key;
        key.time = motion_preview_time;
        const PropertyDesc* bound_desc = bound_component != nullptr
            ? FindPropertyForComponent(*bound_component, track.binding.property)
            : nullptr;
        key.value = bound_desc != nullptr
            ? bound_desc->Capture(*bound_component) : DefaultValueFor(track.value_type);
        key.easing = MotionEasing::Linear;
        track.keys.push_back(key);
        motion_editor_asset.SortKeys();
        motion_selected_key = -1;
        for (int i = 0; i < static_cast<int>(track.keys.size()); ++i)
        {
            if (track.keys[i].time == key.time)
            {
                motion_selected_key = i;
                break;
            }
        }
        motion_edit_history.Commit(motion_editor_asset);
        motion_editor_dirty = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Track削除"))
    {
        stop_motion_preview();
        motion_edit_history.Begin(motion_editor_asset, "Trackを削除");
        motion_editor_asset.tracks.erase(motion_editor_asset.tracks.begin() +
            motion_selected_track);
        motion_selected_track = -1;
        motion_selected_key = -1;
        motion_edit_history.Commit(motion_editor_asset);
        motion_editor_dirty = true;
        ImGui::End();
        return;
    }

    if (motion_selected_key >= 0 &&
        motion_selected_key < static_cast<int>(track.keys.size()))
    {
        MotionKeyframe& key = track.keys[motion_selected_key];
        ImGui::Separator();
        ImGui::Text("Key %d", motion_selected_key);
        float key_time = key.time;
        if (ImGui::DragFloat("時刻", &key_time, 0.01f, 0.0f,
            motion_editor_asset.duration))
        {
            motion_edit_history.Begin(motion_editor_asset, "Key時刻を変更");
            key.time = (std::max)(0.0f, key_time);
            motion_editor_asset.SortKeys();
            motion_edit_history.Commit(motion_editor_asset);
            motion_editor_dirty = true;
            motion_selected_key = -1;
            ImGui::End();
            return;
        }
        PropertyValue edited = key.value;
        if (DrawValueEditor("値", edited, track.value_type))
        {
            motion_edit_history.Begin(motion_editor_asset, "Key値を変更");
            key.value = edited;
            motion_edit_history.Commit(motion_editor_asset);
            motion_editor_dirty = true;
        }
        MotionEasing easing = key.easing;
        if (DrawEasingCombo("Easing", easing))
        {
            motion_edit_history.Begin(motion_editor_asset, "Easingを変更");
            key.easing = easing;
            motion_edit_history.Commit(motion_editor_asset);
            motion_editor_dirty = true;
        }
    }

    ImGui::End();
}
