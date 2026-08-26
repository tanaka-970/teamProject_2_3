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
#include <cmath>
#include <cstring>
#include <filesystem>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "framework_motion_workspaceInternal.h"
using namespace framework_motion_workspace::Detail;

namespace
{
    int MotionFrameAt(float time, int fps) noexcept
    {
        return static_cast<int>(std::round((std::max)(0.0f, time) *
            static_cast<float>((std::max)(1, fps))));
    }

    int ChannelCount(ReplayEngine::Reflection::PropertyType type) noexcept
    {
        using ReplayEngine::Reflection::PropertyType;
        switch (type)
        {
        case PropertyType::Vector2: return 2;
        case PropertyType::Vector3: return 3;
        case PropertyType::Vector4:
        case PropertyType::Quaternion:
        case PropertyType::Color: return 4;
        default: return 1;
        }
    }

    const char* ChannelLabel(ReplayEngine::Reflection::PropertyType type, int channel) noexcept
    {
        using ReplayEngine::Reflection::PropertyType;
        if (type == PropertyType::Color)
        {
            constexpr const char* labels[]{ "R", "G", "B", "A" };
            return labels[(std::max)(0, (std::min)(3, channel))];
        }
        constexpr const char* labels[]{ "X", "Y", "Z", "W" };
        return labels[(std::max)(0, (std::min)(3, channel))];
    }

    float ScalarChannel(const ReplayEngine::Reflection::PropertyValue& value,
        ReplayEngine::Reflection::PropertyType type, int channel) noexcept
    {
        using ReplayEngine::Reflection::PropertyType;
        switch (type)
        {
        case PropertyType::Bool: return value.AsBool() ? 1.0f : 0.0f;
        case PropertyType::Int:
        case PropertyType::Enum: return static_cast<float>(value.AsInt());
        case PropertyType::Float: return value.AsFloat();
        case PropertyType::Double: return static_cast<float>(value.AsDouble());
        case PropertyType::Vector2:
        {
            const DirectX::XMFLOAT2 v = value.AsVector2();
            return channel == 0 ? v.x : v.y;
        }
        case PropertyType::Vector3:
        {
            const DirectX::XMFLOAT3 v = value.AsVector3();
            if (channel == 0) return v.x;
            if (channel == 1) return v.y;
            return v.z;
        }
        case PropertyType::Vector4:
        case PropertyType::Quaternion:
        case PropertyType::Color:
        {
            const DirectX::XMFLOAT4 v = value.AsVector4();
            if (channel == 0) return v.x;
            if (channel == 1) return v.y;
            if (channel == 2) return v.z;
            return v.w;
        }
        default: return 0.0f;
        }
    }

    bool ContainsIndex(const std::vector<int>& values, int value)
    {
        return std::find(values.begin(), values.end(), value) != values.end();
    }

    void ToggleIndex(std::vector<int>& values, int value)
    {
        const auto found = std::find(values.begin(), values.end(), value);
        if (found == values.end()) values.push_back(value);
        else values.erase(found);
        std::sort(values.begin(), values.end());
    }

    bool BuildAutoSmoothBezier(const ReplayEngine::Motion::MotionTrack& track,
        int key_index, int channel, ReplayEngine::Motion::MotionBezierHandles& out) noexcept
    {
        const int count = static_cast<int>(track.keys.size());
        if (key_index < 0 || key_index + 1 >= count) return false;

        const ReplayEngine::Motion::MotionKeyframe& k0 = track.keys[static_cast<std::size_t>(key_index)];
        const ReplayEngine::Motion::MotionKeyframe& k1 = track.keys[static_cast<std::size_t>(key_index + 1)];
        const float dt = k1.time - k0.time;
        if (!(dt > 1.0e-6f)) return false;

        const float v0 = ScalarChannel(k0.value, track.value_type, channel);
        const float v1 = ScalarChannel(k1.value, track.value_type, channel);
        const float dv = v1 - v0;
        if (!std::isfinite(v0) || !std::isfinite(v1) || std::abs(dv) <= 1.0e-6f) return false;

        // Catmull-Rom style centered finite differences give a smooth tangent at each key.
        // They are converted into normalized cubic-Bezier derivatives for this segment.
        float start_slope = dv / dt;
        if (key_index > 0)
        {
            const ReplayEngine::Motion::MotionKeyframe& km1 =
                track.keys[static_cast<std::size_t>(key_index - 1)];
            const float span = k1.time - km1.time;
            if (span > 1.0e-6f)
            {
                start_slope = (v1 - ScalarChannel(km1.value, track.value_type, channel)) / span;
            }
        }

        float end_slope = dv / dt;
        if (key_index + 2 < count)
        {
            const ReplayEngine::Motion::MotionKeyframe& k2 =
                track.keys[static_cast<std::size_t>(key_index + 2)];
            const float span = k2.time - k0.time;
            if (span > 1.0e-6f)
            {
                end_slope = (ScalarChannel(k2.value, track.value_type, channel) - v0) / span;
            }
        }

        const float segment_slope = dv / dt;
        if (!std::isfinite(start_slope) || !std::isfinite(end_slope) ||
            !std::isfinite(segment_slope) || std::abs(segment_slope) <= 1.0e-6f) return false;

        const float normalized_start = start_slope / segment_slope;
        const float normalized_end = end_slope / segment_slope;
        constexpr float x1 = 1.0f / 3.0f;
        constexpr float x2 = 2.0f / 3.0f;
        // Do not clamp Y: anticipation/overshoot is intentional and the existing evaluator supports it.
        out.out_handle = { x1, x1 * normalized_start };
        out.in_handle = { x2, 1.0f - (1.0f - x2) * normalized_end };
        return std::isfinite(out.out_handle.y) && std::isfinite(out.in_handle.y);
    }
}

void framework::draw_motion_timeline()
{
    if (!show_motion_timeline_panel) return;
    if (!ImGui::Begin("タイムライン", &show_motion_timeline_panel))
    {
        ImGui::End();
        return;
    }

    if (ImGui::CollapsingHeader("コマンドガイド"))
    {
        ImGui::TextDisabled("文字入力中は Motion ショートカットを無効化します。");
        ImGui::BulletText("S          Keyを追加");
        ImGui::BulletText("Delete     選択Keyを削除");
        ImGui::BulletText("Ctrl+D     選択Keyを複製");
        ImGui::BulletText("Ctrl+C     選択Keyをコピー");
        ImGui::BulletText("Ctrl+V     Keyを貼り付け");
        ImGui::BulletText("Space      再生 / 停止");
        ImGui::BulletText("Home       先頭へ移動");
        ImGui::BulletText("End        末尾へ移動");
        ImGui::BulletText("PageUp     1フレーム戻る");
        ImGui::BulletText("PageDown   1フレーム進む");
        ImGui::BulletText("F9         EaseInOutCubicを一括適用");
        ImGui::Separator();
    }

    if (!motion_editor_loaded && motion_composition_loaded)
    {
        ImGui::SetNextItemWidth(86.0f);
        int fps = motion_editor_fps;
        if (ImGui::DragInt("FPS", &fps, 1.0f, 1, 240)) motion_editor_fps = (std::max)(1, fps);
        ImGui::SameLine(); ImGui::Checkbox("Frame表示", &motion_editor_display_frames);
        ImGui::SameLine(); ImGui::Checkbox("Snap", &motion_editor_frame_snap);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(120.0f);
        ImGui::SliderFloat("Zoom", &motion_timeline_zoom, 1.0f, 12.0f, "%.1fx");

        const float frame_step = FrameStep(motion_editor_fps);
        if (ImGui::Button("|< 1F")) step_motion_preview_frames(-1);
        ImGui::SameLine();
        if (ImGui::Button("1F >|")) step_motion_preview_frames(1);
        ImGui::SameLine();
        if (motion_editor_display_frames)
            ImGui::Text("Playhead: %dF", MotionFrameAt(motion_preview_time, motion_editor_fps));
        else ImGui::Text("Playhead: %.4fs", motion_preview_time);

        const float width = (std::max)(480.0f,
            (std::max)(1.0f, motion_editor_composition.duration) * 100.0f * motion_timeline_zoom);
        ImGui::BeginChild("##composition_timeline", ImVec2(0,0), false,
            ImGuiWindowFlags_HorizontalScrollbar);
        const ImVec2 ruler = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float duration = (std::max)(0.0001f, motion_editor_composition.duration);
        const int end_frame = (std::max)(1, MotionFrameAt(duration, motion_editor_fps));
        const int tick_frames = motion_timeline_zoom >= 6.0f ? 1 :
            (motion_timeline_zoom >= 3.0f ? (std::max)(1, motion_editor_fps / 10) :
                (std::max)(1, motion_editor_fps / 2));
        for (int frame = 0; frame <= end_frame; frame += tick_frames)
        {
            const float t = frame * frame_step;
            const float x = ruler.x + width * (t / duration);
            const bool major = frame % motion_editor_fps == 0;
            dl->AddLine(ImVec2(x, ruler.y), ImVec2(x, ruler.y + (major ? 12.f : 6.f)),
                IM_COL32(120,120,120,255));
        }
        for (const auto& marker : motion_editor_composition.markers)
        {
            const float x = ruler.x + width * (marker.time / duration);
            dl->AddLine(ImVec2(x, ruler.y), ImVec2(x, ruler.y + 18.f),
                IM_COL32(255,190,70,255), 2.0f);
            dl->AddText(ImVec2(x + 3.f, ruler.y), IM_COL32(255,210,100,255), marker.name.c_str());
        }
        ImGui::Dummy(ImVec2(width, 22.0f));
        for (int i = 0; i < static_cast<int>(motion_editor_composition.layers.size()); ++i)
        {
            auto& layer = motion_editor_composition.layers[i];
            ImGui::PushID(i);
            ImGui::Text("%s", layer.name.c_str());
            ImGui::SameLine(140.0f);
            const ImVec2 o = ImGui::GetCursorScreenPos();
            ImGui::InvisibleButton("##layerbar", ImVec2(width, 20.0f));
            const float in_t = (std::max)(0.0f, layer.in_time);
            const float out_t = layer.out_time < 0.0f ? duration :
                (std::min)(duration, layer.out_time);
            const float x0 = o.x + width * (in_t / duration);
            const float x1 = o.x + width * (out_t / duration);
            dl->AddRectFilled(ImVec2(x0, o.y + 2), ImVec2((std::max)(x0+2.f,x1), o.y+18),
                layer.enabled ? IM_COL32(65,150,210,210) : IM_COL32(90,90,90,150), 3.0f);
            const float start_x = o.x + width * ((std::max)(0.0f, layer.start_offset) / duration);
            dl->AddLine(ImVec2(start_x,o.y), ImVec2(start_x,o.y+20), IM_COL32(255,255,255,220), 1.5f);
            ImGui::PopID();
        }
        const float play_x = ruler.x + width * (motion_preview_time / duration);
        dl->AddLine(ImVec2(play_x, ruler.y), ImVec2(play_x, ImGui::GetCursorScreenPos().y),
            IM_COL32(255,80,80,255), 1.5f);
        ImGui::EndChild();
        ImGui::End();
        return;
    }

    if (!motion_editor_loaded)
    {
        ImGui::TextDisabled("Motion / Composition Asset が未選択です。");
        ImGui::End();
        return;
    }

    // ---- AE style time controls --------------------------------------------
    ImGui::SetNextItemWidth(86.0f);
    int fps = motion_editor_fps;
    if (ImGui::DragInt("FPS", &fps, 1.0f, 1, 240)) motion_editor_fps = (std::max)(1, fps);
    ImGui::SameLine();
    ImGui::Checkbox("Frame表示", &motion_editor_display_frames);
    ImGui::SameLine();
    ImGui::Checkbox("Snap", &motion_editor_frame_snap);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.0f);
    ImGui::SliderFloat("Zoom", &motion_timeline_zoom, 1.0f, 12.0f, "%.1fx");
    ImGui::SameLine();
    ImGui::Checkbox("Box Select", &motion_box_select_mode);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("ON中はTrack上をドラッグして範囲内のKeyをまとめて選択します。");

    const float frame_step = FrameStep(motion_editor_fps);
    if (ImGui::Button("|< 1F")) step_motion_preview_frames(-1);
    ImGui::SameLine();
    if (ImGui::Button("1F >|")) step_motion_preview_frames(1);
    ImGui::SameLine();
    if (motion_editor_display_frames)
        ImGui::Text("Playhead: %dF", MotionFrameAt(motion_preview_time, motion_editor_fps));
    else
        ImGui::Text("Playhead: %.4fs", motion_preview_time);

    if (motion_selected_track >= 0 &&
        motion_selected_track < static_cast<int>(motion_editor_asset.tracks.size()))
    {
        MotionTrack& selected_track = motion_editor_asset.tracks[motion_selected_track];
        if (ImGui::Button("+ Key @ Playhead"))
        {
            ReplayEngine::Reflection::PropertyValue value;
            if (!MotionEvaluator::EvaluateTrack(selected_track, motion_preview_time, value) &&
                !selected_track.keys.empty()) value = selected_track.keys.back().value;
            motion_edit_history.Begin(motion_editor_asset, "Keyを追加");
            MotionKeyframe key;
            key.time = SnapMotionTime(motion_preview_time, motion_editor_fps,
                motion_editor_frame_snap);
            key.value = value;
            selected_track.keys.push_back(std::move(key));
            motion_editor_asset.SortKeys();
            motion_edit_history.Commit(motion_editor_asset);
            motion_editor_dirty = true;
            motion_selected_key = -1;
            motion_selected_keys.clear();
        }
        ImGui::SameLine();
        if (ImGui::Button("Copy")) copy_motion_keys();
        ImGui::SameLine();
        if (ImGui::Button("Paste")) paste_motion_keys();
        ImGui::SameLine();
        if (ImGui::Button("Duplicate")) duplicate_motion_keys();
        ImGui::SameLine();
        if (ImGui::Button("Delete")) delete_motion_keys();
    }

    ImGui::Separator();
    ImGui::BeginChild("##motion_timeline_scroll", ImVec2(0.0f, 0.0f), false,
        ImGuiWindowFlags_HorizontalScrollbar);

    const float available = (std::max)(320.0f, ImGui::GetContentRegionAvail().x - 120.0f);
    const float width = (std::max)(available,
        (std::max)(motion_editor_asset.duration, 1.0f) * 100.0f * motion_timeline_zoom);
    const float marker_width = ImGui::CalcTextSize("◆").x +
        ImGui::GetStyle().FramePadding.x * 2.0f;
    const float marker_span = (std::max)(1.0f, width - marker_width);

    // time ruler + playhead
    ImGui::Dummy(ImVec2(110.0f, 1.0f));
    ImGui::SameLine();
    const ImVec2 ruler = ImGui::GetCursorScreenPos();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const int end_frame = (std::max)(1, MotionFrameAt(motion_editor_asset.duration,
        motion_editor_fps));
    const int tick_frames = motion_timeline_zoom >= 6.0f ? 1 :
        (motion_timeline_zoom >= 3.0f ? (std::max)(1, motion_editor_fps / 10) :
            (std::max)(1, motion_editor_fps / 2));
    for (int frame = 0; frame <= end_frame; frame += tick_frames)
    {
        const float time = frame * frame_step;
        const float x = ruler.x + marker_span * (time /
            (std::max)(motion_editor_asset.duration, 0.0001f));
        const bool major = frame % motion_editor_fps == 0;
        draw_list->AddLine(ImVec2(x, ruler.y), ImVec2(x, ruler.y + (major ? 10.0f : 5.0f)),
            IM_COL32(120, 120, 120, 255), 1.0f);
        if (major)
        {
            char label[32]{};
            if (motion_editor_display_frames) sprintf_s(label, "%dF", frame);
            else sprintf_s(label, "%.1fs", time);
            draw_list->AddText(ImVec2(x + 2.0f, ruler.y), IM_COL32(170,170,170,255), label);
        }
    }
    ImGui::Dummy(ImVec2(width, 16.0f));

    for (int track_index = 0;
        track_index < static_cast<int>(motion_editor_asset.tracks.size()); ++track_index)
    {
        MotionTrack& track = motion_editor_asset.tracks[track_index];
        ImGui::PushID(track_index);
        if (ImGui::Selectable(track.name.c_str(), motion_selected_track == track_index,
            ImGuiSelectableFlags_SpanAllColumns, ImVec2(110.0f, 0.0f)))
        {
            motion_selected_track = track_index;
            motion_selected_key = -1;
            motion_selected_keys.clear();
            motion_selected_event_track = -1;
            motion_selected_event = -1;
        }
        ImGui::SameLine();
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        draw_list->AddLine(origin, ImVec2(origin.x + width, origin.y),
            IM_COL32(100, 100, 100, 255), 1.0f);

        // Double click empty row = add a key at the clicked frame.
        ImGui::InvisibleButton("##trackrow", ImVec2(width, 16.0f));
        if (motion_box_select_mode && ImGui::IsItemActivated())
        {
            motion_box_selecting = true;
            motion_box_select_track = track_index;
            motion_box_select_start_x = ImGui::GetIO().MousePos.x;
            motion_box_select_current_x = motion_box_select_start_x;
        }
        if (motion_box_selecting && motion_box_select_track == track_index &&
            ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
        {
            motion_box_select_current_x = ImGui::GetIO().MousePos.x;
            const float x0 = (std::min)(motion_box_select_start_x, motion_box_select_current_x);
            const float x1 = (std::max)(motion_box_select_start_x, motion_box_select_current_x);
            draw_list->AddRectFilled(ImVec2(x0, origin.y - 7.0f),
                ImVec2(x1, origin.y + 10.0f), IM_COL32(80, 150, 255, 45));
            draw_list->AddRect(ImVec2(x0, origin.y - 7.0f),
                ImVec2(x1, origin.y + 10.0f), IM_COL32(100, 180, 255, 220));
        }
        if (motion_box_selecting && motion_box_select_track == track_index &&
            ImGui::IsItemDeactivated())
        {
            motion_box_select_current_x = ImGui::GetIO().MousePos.x;
            const float x0 = (std::min)(motion_box_select_start_x, motion_box_select_current_x);
            const float x1 = (std::max)(motion_box_select_start_x, motion_box_select_current_x);
            motion_selected_track = track_index;
            motion_selected_key = -1;
            motion_selected_keys.clear();
            for (int select_index = 0; select_index < static_cast<int>(track.keys.size()); ++select_index)
            {
                const float kt = motion_editor_asset.duration > 0.0f
                    ? track.keys[select_index].time / motion_editor_asset.duration : 0.0f;
                const float kx = origin.x + marker_span * kt;
                if (kx >= x0 && kx <= x1) motion_selected_keys.push_back(select_index);
            }
            if (!motion_selected_keys.empty()) motion_selected_key = motion_selected_keys.back();
            motion_box_selecting = false;
            motion_box_select_track = -1;
        }
        if (!motion_box_select_mode && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
        {
            const float normalized = (ImGui::GetIO().MousePos.x - origin.x) /
                (std::max)(1.0f, marker_span);
            const float time = SnapMotionTime(normalized * motion_editor_asset.duration,
                motion_editor_fps, motion_editor_frame_snap);
            ReplayEngine::Reflection::PropertyValue value;
            if (!MotionEvaluator::EvaluateTrack(track, time, value) && !track.keys.empty())
                value = track.keys.back().value;
            motion_edit_history.Begin(motion_editor_asset, "TimelineでKeyを追加");
            MotionKeyframe key; key.time = time; key.value = value;
            track.keys.push_back(std::move(key));
            motion_editor_asset.SortKeys();
            motion_edit_history.Commit(motion_editor_asset);
            motion_editor_dirty = true;
        }

        for (int key_index = 0; key_index < static_cast<int>(track.keys.size()); ++key_index)
        {
            MotionKeyframe& key = track.keys[key_index];
            const float t = motion_editor_asset.duration > 0.0f
                ? key.time / motion_editor_asset.duration : 0.0f;
            ImGui::SetCursorScreenPos(ImVec2(origin.x + marker_span * t, origin.y - 6.0f));
            ImGui::PushID(10000 + key_index);
            const bool multi_selected = motion_selected_track == track_index &&
                ContainsIndex(motion_selected_keys, key_index);
            ImGui::SmallButton(motion_selected_track == track_index &&
                (motion_selected_key == key_index || multi_selected) ? "◆" : "◇");
            if (!motion_box_select_mode && ImGui::IsItemClicked())
            {
                if (motion_selected_track != track_index)
                {
                    motion_selected_keys.clear();
                    motion_selected_track = track_index;
                }
                if (ImGui::GetIO().KeyCtrl)
                {
                    ToggleIndex(motion_selected_keys, key_index);
                    motion_selected_key = motion_selected_keys.empty() ? -1 : key_index;
                }
                else
                {
                    motion_selected_keys.clear();
                    motion_selected_keys.push_back(key_index);
                    motion_selected_key = key_index;
                }
                motion_selected_event_track = -1;
                motion_selected_event = -1;
            }
            if (!motion_box_select_mode && ImGui::IsItemActivated())
                motion_edit_history.Begin(motion_editor_asset, "TimelineでKeyを移動");
            if (!motion_box_select_mode && ImGui::IsItemActive() && ImGui::IsMouseDragging(0))
            {
                const float dt = ImGui::GetIO().MouseDelta.x / marker_span *
                    motion_editor_asset.duration;
                const std::vector<int> moving = !motion_selected_keys.empty()
                    ? motion_selected_keys : std::vector<int>{ key_index };
                for (const int moving_index : moving)
                {
                    if (moving_index < 0 || moving_index >= static_cast<int>(track.keys.size())) continue;
                    track.keys[moving_index].time = (std::min)(motion_editor_asset.duration,
                        SnapMotionTime(track.keys[moving_index].time + dt,
                            motion_editor_fps, motion_editor_frame_snap));
                }
                motion_editor_dirty = true;
            }
            if (ImGui::IsItemDeactivated() && motion_edit_history.InTransaction())
            {
                motion_editor_asset.SortKeys();
                motion_edit_history.Commit(motion_editor_asset);
                motion_selected_key = -1;
                motion_selected_keys.clear();
            }
            ImGui::PopID();
        }
        ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + 18.0f));
        ImGui::PopID();
    }

    for (int event_track_index = 0;
        event_track_index < static_cast<int>(motion_editor_asset.event_tracks.size()); ++event_track_index)
    {
        MotionEventTrack& track = motion_editor_asset.event_tracks[event_track_index];
        ImGui::PushID(100000 + event_track_index);
        const std::string label = track.object.Valid()
            ? "Event: " + track.object.ToString() : "Event: Broadcast";
        if (ImGui::Selectable(label.c_str(), motion_selected_event_track == event_track_index,
            ImGuiSelectableFlags_SpanAllColumns, ImVec2(110.0f, 0.0f)))
        {
            motion_selected_event_track = event_track_index;
            motion_selected_event = -1;
            motion_selected_track = -1;
            motion_selected_key = -1;
            motion_selected_keys.clear();
        }
        ImGui::SameLine();
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        draw_list->AddLine(origin, ImVec2(origin.x + width, origin.y),
            IM_COL32(150, 110, 70, 255), 1.0f);
        for (int event_index = 0; event_index < static_cast<int>(track.events.size()); ++event_index)
        {
            MotionEvent& event = track.events[event_index];
            const float t = motion_editor_asset.duration > 0.0f
                ? event.time / motion_editor_asset.duration : 0.0f;
            ImGui::SetCursorScreenPos(ImVec2(origin.x + marker_span * t, origin.y - 6.0f));
            ImGui::PushID(event_index);
            ImGui::SmallButton(motion_selected_event_track == event_track_index &&
                motion_selected_event == event_index ? "●" : "○");
            if (ImGui::IsItemClicked())
            {
                motion_selected_event_track = event_track_index;
                motion_selected_event = event_index;
                motion_selected_track = -1;
                motion_selected_key = -1;
                motion_selected_keys.clear();
            }
            if (ImGui::IsItemActivated())
                motion_edit_history.Begin(motion_editor_asset, "Eventを移動");
            if (ImGui::IsItemActive() && ImGui::IsMouseDragging(0))
            {
                const float dt = ImGui::GetIO().MouseDelta.x / marker_span *
                    motion_editor_asset.duration;
                event.time = (std::min)(motion_editor_asset.duration,
                    SnapMotionTime(event.time + dt, motion_editor_fps,
                        motion_editor_frame_snap));
                motion_editor_dirty = true;
            }
            if (ImGui::IsItemDeactivated() && motion_edit_history.InTransaction())
            {
                motion_editor_asset.SortKeys();
                motion_edit_history.Commit(motion_editor_asset);
            }
            ImGui::PopID();
        }
        ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + 18.0f));
        ImGui::PopID();
    }

    // playhead vertical line across visible tracks
    if (motion_editor_asset.duration > 0.0f)
    {
        const float x = ruler.x + marker_span * motion_preview_time /
            motion_editor_asset.duration;
        draw_list->AddLine(ImVec2(x, ruler.y),
            ImVec2(x, ImGui::GetCursorScreenPos().y), IM_COL32(255, 210, 70, 230), 1.5f);
    }

    ImGui::Dummy(ImVec2(width, 1.0f));
    ImGui::EndChild();
    ImGui::End();
}

void framework::draw_motion_graph_editor()
{
    if (!show_motion_graph_panel) return;
    if (!ImGui::Begin("グラフエディター", &show_motion_graph_panel))
    {
        ImGui::End();
        return;
    }

    if (!motion_editor_loaded || motion_selected_track < 0 ||
        motion_selected_track >= static_cast<int>(motion_editor_asset.tracks.size()))
    {
        ImGui::TextDisabled("Track / Keyを選択してください。");
        ImGui::End();
        return;
    }

    MotionTrack& track = motion_editor_asset.tracks[motion_selected_track];
    const int channel_count = ChannelCount(track.value_type);
    motion_graph_channel = (std::max)(0, (std::min)(channel_count - 1,
        motion_graph_channel));

    ImGui::Checkbox("Speed Graph", &motion_graph_speed_mode);
    ImGui::SameLine();
    ImGui::Checkbox("Overlay Tracks", &motion_graph_overlay_tracks);
    if (channel_count > 1)
    {
        ImGui::SameLine();
        ImGui::Text("Channel:");
        for (int channel = 0; channel < channel_count; ++channel)
        {
            ImGui::SameLine();
            if (ImGui::RadioButton(ChannelLabel(track.value_type, channel),
                motion_graph_channel == channel)) motion_graph_channel = channel;
        }
    }

    constexpr int sample_count = 256;
    std::vector<float> samples(static_cast<std::size_t>(sample_count), 0.0f);
    float min_value = (std::numeric_limits<float>::max)();
    float max_value = (std::numeric_limits<float>::lowest)();
    ReplayEngine::Reflection::PropertyValue evaluated;
    const float duration = (std::max)(0.0001f, motion_editor_asset.duration);
    for (int sample = 0; sample < sample_count; ++sample)
    {
        const float t = duration * sample / static_cast<float>(sample_count - 1);
        float value = 0.0f;
        if (MotionEvaluator::EvaluateTrack(track, t, evaluated))
            value = ScalarChannel(evaluated, track.value_type, motion_graph_channel);
        samples[static_cast<std::size_t>(sample)] = value;
    }
    if (motion_graph_speed_mode)
    {
        const float dt = duration / static_cast<float>(sample_count - 1);
        std::vector<float> speed(samples.size(), 0.0f);
        for (int sample = 1; sample < sample_count - 1; ++sample)
            speed[static_cast<std::size_t>(sample)] =
                (samples[static_cast<std::size_t>(sample + 1)] -
                    samples[static_cast<std::size_t>(sample - 1)]) / (2.0f * dt);
        if (sample_count > 1)
        {
            speed.front() = (samples[1] - samples[0]) / dt;
            speed.back() = (samples.back() - samples[samples.size() - 2]) / dt;
        }
        samples.swap(speed);
    }
    struct OverlayCurve
    {
        std::string name;
        std::vector<float> values;
    };
    std::vector<OverlayCurve> overlay_curves;
    if (motion_graph_overlay_tracks)
    {
        for (int overlay_index = 0; overlay_index <
            static_cast<int>(motion_editor_asset.tracks.size()); ++overlay_index)
        {
            if (overlay_index == motion_selected_track) continue;
            const MotionTrack& overlay_track = motion_editor_asset.tracks[overlay_index];
            if (ChannelCount(overlay_track.value_type) <= motion_graph_channel) continue;
            OverlayCurve curve;
            curve.name = overlay_track.name;
            curve.values.resize(static_cast<std::size_t>(sample_count), 0.0f);
            for (int sample = 0; sample < sample_count; ++sample)
            {
                const float t = duration * sample / static_cast<float>(sample_count - 1);
                if (MotionEvaluator::EvaluateTrack(overlay_track, t, evaluated))
                    curve.values[static_cast<std::size_t>(sample)] =
                        ScalarChannel(evaluated, overlay_track.value_type, motion_graph_channel);
            }
            if (motion_graph_speed_mode)
            {
                const float dt = duration / static_cast<float>(sample_count - 1);
                std::vector<float> speed(curve.values.size(), 0.0f);
                for (int sample = 1; sample < sample_count - 1; ++sample)
                    speed[static_cast<std::size_t>(sample)] =
                        (curve.values[static_cast<std::size_t>(sample + 1)] -
                            curve.values[static_cast<std::size_t>(sample - 1)]) / (2.0f * dt);
                if (sample_count > 1)
                {
                    speed.front() = (curve.values[1] - curve.values[0]) / dt;
                    speed.back() = (curve.values.back() -
                        curve.values[curve.values.size() - 2]) / dt;
                }
                curve.values.swap(speed);
            }
            overlay_curves.push_back(std::move(curve));
        }
    }

    for (const float value : samples)
    {
        min_value = (std::min)(min_value, value);
        max_value = (std::max)(max_value, value);
    }
    for (const OverlayCurve& curve : overlay_curves)
    {
        for (const float value : curve.values)
        {
            min_value = (std::min)(min_value, value);
            max_value = (std::max)(max_value, value);
        }
    }
    if (std::fabs(max_value - min_value) < 0.0001f)
    {
        min_value -= 1.0f;
        max_value += 1.0f;
    }
    const float padding = (max_value - min_value) * 0.1f;
    min_value -= padding;
    max_value += padding;

    const ImVec2 graph_origin = ImGui::GetCursorScreenPos();
    const ImVec2 graph_size((std::max)(240.0f, ImGui::GetContentRegionAvail().x), 240.0f);
    ImGui::InvisibleButton("##motion_value_graph", graph_size);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(graph_origin,
        ImVec2(graph_origin.x + graph_size.x, graph_origin.y + graph_size.y),
        IM_COL32(24, 24, 28, 255));
    draw->AddRect(graph_origin,
        ImVec2(graph_origin.x + graph_size.x, graph_origin.y + graph_size.y),
        IM_COL32(90, 90, 95, 255));
    for (int grid = 1; grid < 4; ++grid)
    {
        const float gx = graph_origin.x + graph_size.x * grid / 4.0f;
        const float gy = graph_origin.y + graph_size.y * grid / 4.0f;
        draw->AddLine(ImVec2(gx, graph_origin.y), ImVec2(gx, graph_origin.y + graph_size.y),
            IM_COL32(55,55,60,255));
        draw->AddLine(ImVec2(graph_origin.x, gy), ImVec2(graph_origin.x + graph_size.x, gy),
            IM_COL32(55,55,60,255));
    }
    const auto to_graph = [&](int sample, float value)
    {
        const float x = graph_origin.x + graph_size.x * sample /
            static_cast<float>(sample_count - 1);
        const float n = (value - min_value) / (max_value - min_value);
        const float y = graph_origin.y + graph_size.y * (1.0f - n);
        return ImVec2(x, y);
    };
    static const ImU32 overlay_colors[] =
    {
        IM_COL32(255, 120, 120, 150), IM_COL32(120, 255, 150, 150),
        IM_COL32(220, 150, 255, 150), IM_COL32(255, 220, 100, 150),
        IM_COL32(120, 220, 255, 150)
    };
    for (std::size_t curve_index = 0; curve_index < overlay_curves.size(); ++curve_index)
    {
        const OverlayCurve& curve = overlay_curves[curve_index];
        const ImU32 color = overlay_colors[curve_index %
            (sizeof(overlay_colors) / sizeof(overlay_colors[0]))];
        for (int sample = 1; sample < sample_count; ++sample)
            draw->AddLine(to_graph(sample - 1, curve.values[static_cast<std::size_t>(sample - 1)]),
                to_graph(sample, curve.values[static_cast<std::size_t>(sample)]), color, 1.0f);
    }
    for (int sample = 1; sample < sample_count; ++sample)
        draw->AddLine(to_graph(sample - 1, samples[static_cast<std::size_t>(sample - 1)]),
            to_graph(sample, samples[static_cast<std::size_t>(sample)]),
            IM_COL32(85, 190, 255, 255), 2.0f);

    // Value graphではKey値を重ねる。Back/ElasticのovershootもEvaluatorサンプルに含まれる。
    if (!motion_graph_speed_mode)
    {
        for (int key_index = 0; key_index < static_cast<int>(track.keys.size()); ++key_index)
        {
            const MotionKeyframe& key = track.keys[key_index];
            const float x = graph_origin.x + graph_size.x * key.time / duration;
            const float value = ScalarChannel(key.value, track.value_type, motion_graph_channel);
            const float n = (value - min_value) / (max_value - min_value);
            const float y = graph_origin.y + graph_size.y * (1.0f - n);
            draw->AddCircleFilled(ImVec2(x, y),
                motion_selected_key == key_index ? 5.0f : 3.5f,
                motion_selected_key == key_index ? IM_COL32(255,210,70,255) :
                    IM_COL32(220,220,220,255));
        }
    }
    ImGui::Text("Range %.4f .. %.4f   %s", min_value, max_value,
        motion_graph_speed_mode ? "value/sec" : "value");
    if (!overlay_curves.empty())
    {
        ImGui::TextDisabled("Selected: %s", track.name.c_str());
        for (const OverlayCurve& curve : overlay_curves)
        {
            ImGui::SameLine();
            ImGui::TextDisabled("| %s", curve.name.c_str());
        }
    }

    if (motion_selected_key < 0 || motion_selected_key >= static_cast<int>(track.keys.size()))
    {
        ImGui::TextDisabled("Keyを選ぶと補間ハンドルを編集できます。");
        ImGui::End();
        return;
    }

    MotionKeyframe& key = track.keys[motion_selected_key];
    ImGui::Separator();
    MotionEasing easing = key.easing;
    if (DrawEasingCombo("Easing", easing))
    {
        motion_edit_history.Begin(motion_editor_asset, "Easingを変更");
        key.easing = easing;
        motion_edit_history.Commit(motion_editor_asset);
        motion_editor_dirty = true;
    }
    if (ImGui::Button("Auto Smooth"))
    {
        motion_edit_history.Begin(motion_editor_asset, "Auto Smooth");
        ReplayEngine::Motion::MotionBezierHandles smooth_handles{};
        if (BuildAutoSmoothBezier(track, motion_selected_key, motion_graph_channel, smooth_handles))
        {
            key.easing = MotionEasing::CustomBezier;
            key.bezier = smooth_handles;
        }
        else
        {
            // Constant segments and the final key have no outgoing value slope to infer.
            key.easing = MotionEasing::EaseInOutCubic;
        }
        motion_edit_history.Commit(motion_editor_asset);
        motion_editor_dirty = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Linear"))
    {
        motion_edit_history.Begin(motion_editor_asset, "Linear");
        key.easing = MotionEasing::Linear;
        motion_edit_history.Commit(motion_editor_asset);
        motion_editor_dirty = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Ease In"))
    {
        motion_edit_history.Begin(motion_editor_asset, "Ease In");
        key.easing = MotionEasing::CustomBezier;
        key.bezier.out_handle = { 0.42f, 0.0f };
        key.bezier.in_handle = { 1.0f, 1.0f };
        motion_edit_history.Commit(motion_editor_asset); motion_editor_dirty = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Ease Out"))
    {
        motion_edit_history.Begin(motion_editor_asset, "Ease Out");
        key.easing = MotionEasing::CustomBezier;
        key.bezier.out_handle = { 0.0f, 0.0f };
        key.bezier.in_handle = { 0.58f, 1.0f };
        motion_edit_history.Commit(motion_editor_asset); motion_editor_dirty = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Ease InOut"))
    {
        motion_edit_history.Begin(motion_editor_asset, "Ease InOut");
        key.easing = MotionEasing::CustomBezier;
        key.bezier.out_handle = { 0.42f, 0.0f };
        key.bezier.in_handle = { 0.58f, 1.0f };
        motion_edit_history.Commit(motion_editor_asset); motion_editor_dirty = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Hold"))
    {
        motion_edit_history.Begin(motion_editor_asset, "Hold");
        key.easing = MotionEasing::Step;
        motion_edit_history.Commit(motion_editor_asset); motion_editor_dirty = true;
    }

    if (key.easing == MotionEasing::CustomBezier)
    {
        float out_handle[2]{ key.bezier.out_handle.x, key.bezier.out_handle.y };
        float in_handle[2]{ key.bezier.in_handle.x, key.bezier.in_handle.y };
        bool numeric_changed = false;
        numeric_changed |= ImGui::DragFloat2("Out", out_handle, 0.005f, -2.0f, 2.0f);
        numeric_changed |= ImGui::DragFloat2("In", in_handle, 0.005f, -2.0f, 2.0f);
        if (numeric_changed)
        {
            motion_edit_history.Begin(motion_editor_asset, "Bezierを変更");
            key.bezier.out_handle = { out_handle[0], out_handle[1] };
            key.bezier.in_handle = { in_handle[0], in_handle[1] };
            motion_edit_history.Commit(motion_editor_asset);
            motion_editor_dirty = true;
        }

        // Direct visual Bezier handles. x/y are deliberately allowed outside 0..1
        // so overshoot and anticipation can be authored rather than clamped away.
        const ImVec2 bezier_origin = ImGui::GetCursorScreenPos();
        const ImVec2 bezier_size((std::max)(220.0f, ImGui::GetContentRegionAvail().x), 180.0f);
        ImGui::InvisibleButton("##bezier_handles", bezier_size);
        ImDrawList* bd = ImGui::GetWindowDrawList();
        bd->AddRectFilled(bezier_origin,
            ImVec2(bezier_origin.x + bezier_size.x, bezier_origin.y + bezier_size.y),
            IM_COL32(20,20,24,255));
        bd->AddRect(bezier_origin,
            ImVec2(bezier_origin.x + bezier_size.x, bezier_origin.y + bezier_size.y),
            IM_COL32(90,90,95,255));
        const auto bezier_pos = [&](const DirectX::XMFLOAT2& h)
        {
            return ImVec2(bezier_origin.x + h.x * bezier_size.x,
                bezier_origin.y + (1.0f - h.y) * bezier_size.y);
        };
        const ImVec2 p0 = bezier_pos({0.0f,0.0f});
        const ImVec2 p1 = bezier_pos(key.bezier.out_handle);
        const ImVec2 p2 = bezier_pos(key.bezier.in_handle);
        const ImVec2 p3 = bezier_pos({1.0f,1.0f});
        ImVec2 previous = p0;
        for (int segment = 1; segment <= 64; ++segment)
        {
            const float t = segment / 64.0f;
            const float u = 1.0f - t;
            const float x = u*u*u*p0.x + 3*u*u*t*p1.x + 3*u*t*t*p2.x + t*t*t*p3.x;
            const float y = u*u*u*p0.y + 3*u*u*t*p1.y + 3*u*t*t*p2.y + t*t*t*p3.y;
            const ImVec2 current(x,y);
            bd->AddLine(previous,current,IM_COL32(90,210,255,255),2.0f);
            previous=current;
        }
        bd->AddLine(p0,p1,IM_COL32(130,130,140,255));
        bd->AddLine(p3,p2,IM_COL32(130,130,140,255));
        bd->AddCircleFilled(p1,6.0f,IM_COL32(255,190,60,255));
        bd->AddCircleFilled(p2,6.0f,IM_COL32(255,120,90,255));

        const ImVec2 mouse = ImGui::GetIO().MousePos;
        const auto dist2 = [&](ImVec2 a, ImVec2 b)
        { const float x=a.x-b.x,y=a.y-b.y; return x*x+y*y; };
        static int dragging_handle = 0;
        if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(0))
        {
            if (dist2(mouse,p1) <= 100.0f) dragging_handle = 1;
            else if (dist2(mouse,p2) <= 100.0f) dragging_handle = 2;
            if (dragging_handle != 0) motion_edit_history.Begin(motion_editor_asset,
                "Bezier Handleをドラッグ");
        }
        if (dragging_handle != 0 && ImGui::IsMouseDown(0))
        {
            DirectX::XMFLOAT2 h{
                (mouse.x - bezier_origin.x) / bezier_size.x,
                1.0f - (mouse.y - bezier_origin.y) / bezier_size.y };
            h.x = (std::max)(-2.0f, (std::min)(2.0f, h.x));
            h.y = (std::max)(-2.0f, (std::min)(2.0f, h.y));
            if (dragging_handle == 1) key.bezier.out_handle = h;
            else key.bezier.in_handle = h;
            motion_editor_dirty = true;
        }
        if (dragging_handle != 0 && ImGui::IsMouseReleased(0))
        {
            if (motion_edit_history.InTransaction()) motion_edit_history.Commit(motion_editor_asset);
            dragging_handle = 0;
        }
    }

    ImGui::End();
}
