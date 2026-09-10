#include "framework.h"

#include "../../RePlayEngine/Editor/Style/EditorStyle.h"
#include "../../RePlayEngine/Components/UI/UIImageComponent.h"
#include "../../RePlayEngine/Motion/MotionBindingResolver.h"
#include "../../RePlayEngine/Motion/MotionEvaluator.h"
#include "../../RePlayEngine/Object/GameObject/GameObject.h"
#include "../../RePlayEngine/Object/Registry/ComponentRegistry.h"
#include "../../RePlayEngine/Reflection/Registry/PropertyRegistry.h"
#include "../../RePlayEngine/Scene/Runtime/Scene.h"
#include "../../RePlayEngine/Editor/ReorderableList.h"

#include "imgui/imgui_internal.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstddef>
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
    REPLAY_PROFILE_SCOPE("Editor/MotionTimeline");
    if (!show_motion_timeline_panel) return;
    ReplayEngine::Editor::PanelTabColorScope panel_tab_color("Motion");
    if (!ImGui::Begin(u8"タイムライン", &show_motion_timeline_panel))
    {
        ImGui::End();
        return;
    }

    if (ImGui::CollapsingHeader(u8"コマンドガイド"))
    {
        const auto command_guide = [&](const char* name, const char* description)
        {
            const std::string shortcut = action_shortcut(name);
            ImGui::BulletText("%s%s%s", shortcut.c_str(), shortcut.empty() ? "" : "    ",
                description);
        };
        ImGui::TextDisabled(u8"文字入力中はモーションショートカットを無効化します。");
        command_guide(u8"キーを追加", u8"キーを追加");
        command_guide(u8"キーを削除", u8"選択キーを削除");
        command_guide(u8"キーを複製", u8"選択キーを複製");
        command_guide(u8"キーをコピー", u8"選択キーをコピー");
        command_guide(u8"キーを貼り付け", u8"キーを貼り付け");
        command_guide(u8"再生/停止", u8"再生 / 停止");
        command_guide(u8"先頭へ移動", u8"先頭へ移動");
        command_guide(u8"末尾へ移動", u8"末尾へ移動");
        command_guide(u8"1コマ戻る", u8"1フレーム戻る");
        command_guide(u8"1コマ進む", u8"1フレーム進む");
        command_guide(u8"プリセットを適用",
            u8"選択中プリセットを一括適用（未選択はEaseInOutCubic）");
        ImGui::Separator();
    }

    if (!motion_editor_loaded && motion_composition_loaded)
    {
        ImGui::SetNextItemWidth(86.0f);
        int fps = motion_editor_fps;
        if (ImGui::DragInt("FPS", &fps, 1.0f, 1, 240)) motion_editor_fps = (std::max)(1, fps);
        ImGui::SameLine(); ImGui::Checkbox(u8"フレーム表示", &motion_editor_display_frames);
        ImGui::SameLine(); ImGui::Checkbox(u8"フレーム吸着", &motion_editor_frame_snap);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(120.0f);
        ImGui::SliderFloat(u8"拡大", &motion_timeline_zoom, 1.0f, 12.0f, "%.1fx");

        const float frame_step = FrameStep(motion_editor_fps);
        if (ImGui::Button(u8"← 1F")) step_motion_preview_frames(-1);
        ReplayEngine::Editor::EditorHelp::Item("button.timeline.step_previous_composition",
            u8"コンポジションの再生位置を 1 フレーム戻します。");
        ImGui::SameLine();
        if (ImGui::Button(u8"1F →")) step_motion_preview_frames(1);
        ReplayEngine::Editor::EditorHelp::Item("button.timeline.step_next_composition",
            u8"コンポジションの再生位置を 1 フレーム進めます。");
        ImGui::SameLine();
        if (motion_editor_display_frames)
            ImGui::Text(u8"再生位置: %dF", MotionFrameAt(motion_preview_time, motion_editor_fps));
        else ImGui::Text(u8"再生位置: %.4fs", motion_preview_time);

        ImGui::TextUnformatted(u8"レイヤー順");
        ReplayEngine::Editor::ReorderRequest composition_layer_move{};
        const bool can_edit_composition = object_editor_context.CanEdit();
        for (std::size_t i = 0; i < motion_editor_composition.layers.size(); ++i)
        {
            const auto& layer = motion_editor_composition.layers[i];
            const std::string item_id = "CompositionTimelineLayer" + std::to_string(i);
            const ReplayEngine::Editor::ReorderableItemResult item =
                ReplayEngine::Editor::DrawReorderableItem(
                    &motion_editor_composition.layers, item_id.c_str(), i,
                    motion_editor_composition.layers.size(), layer.name.c_str(),
                    false, false, can_edit_composition, [] {});
            if (item.request.Valid() && !composition_layer_move.Valid())
                composition_layer_move = item.request;
        }
        if (composition_layer_move.Valid() &&
            composition_layer_move.source < motion_editor_composition.layers.size() &&
            composition_layer_move.destination < motion_editor_composition.layers.size())
        {
            composition_edit_history.Begin(motion_editor_composition,
                u8"コンポジションレイヤーの順序を変更");
            auto moved = std::move(
                motion_editor_composition.layers[composition_layer_move.source]);
            motion_editor_composition.layers.erase(
                motion_editor_composition.layers.begin() +
                static_cast<std::ptrdiff_t>(composition_layer_move.source));
            motion_editor_composition.layers.insert(
                motion_editor_composition.layers.begin() +
                static_cast<std::ptrdiff_t>(composition_layer_move.destination), std::move(moved));
            composition_edit_history.Commit(motion_editor_composition);
            motion_editor_dirty = true;
        }
        if (const char* active_label = ReplayEngine::Editor::ActiveReorderLabel();
            active_label != nullptr)
        {
            ImGui::TextColored(ImGui::GetStyle().Colors[ImGuiCol_DragDropTarget],
                u8"移動中: %s", active_label);
        }

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
        ImGui::TextDisabled(u8"モーション / コンポジションアセットが未選択です。");
        ImGui::End();
        return;
    }

    const auto evaluate_track_at_time = [&](const MotionTrack& track, float raw_time,
        ReplayEngine::Reflection::PropertyValue& value, std::string* curve_error)
    {
        std::string remap_error;
        const float evaluated_time = MotionEvaluator::RemapMotionTime(motion_editor_asset,
            raw_time, &asset_database, &remap_error);
        if (motion_editor_asset.time_remap.IsAssigned())
            push_motion_curve_warning_once(remap_error);
        ReplayEngine::Motion::MotionTrackEvaluationContext evaluation_context;
        evaluation_context.time = evaluated_time;
        evaluation_context.raw_time = raw_time;
        evaluation_context.duration = motion_editor_asset.duration;
        evaluation_context.database = &asset_database;
        evaluation_context.error = curve_error;
        return MotionEvaluator::EvaluateTrackWithContext(track, evaluation_context, value);
    };

    // ---- AE style time controls --------------------------------------------
    ImGui::SetNextItemWidth(86.0f);
    int fps = motion_editor_fps;
    if (ImGui::DragInt("FPS", &fps, 1.0f, 1, 240)) motion_editor_fps = (std::max)(1, fps);
    ImGui::SameLine();
    ImGui::Checkbox(u8"フレーム表示", &motion_editor_display_frames);
    ImGui::SameLine();
    ImGui::Checkbox(u8"フレーム吸着", &motion_editor_frame_snap);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.0f);
    ImGui::SliderFloat(u8"拡大", &motion_timeline_zoom, 1.0f, 12.0f, "%.1fx");
    ImGui::SameLine();
    ImGui::Checkbox(u8"範囲選択", &motion_box_select_mode);
    ReplayEngine::Editor::EditorHelp::Item("control.timeline.box_select",
        u8"有効中はトラック上をドラッグして範囲内のキーをまとめて選択します。");

    const float frame_step = FrameStep(motion_editor_fps);
    if (ImGui::Button(u8"← 1F")) step_motion_preview_frames(-1);
    ReplayEngine::Editor::EditorHelp::Item("button.timeline.step_previous",
        u8"再生位置を 1 フレーム戻します。");
    ImGui::SameLine();
    if (ImGui::Button(u8"1F →")) step_motion_preview_frames(1);
    ReplayEngine::Editor::EditorHelp::Item("button.timeline.step_next",
        u8"再生位置を 1 フレーム進めます。");
    ImGui::SameLine();
    if (motion_editor_display_frames)
        ImGui::Text(u8"再生位置: %dF", MotionFrameAt(motion_preview_time, motion_editor_fps));
    else
        ImGui::Text(u8"再生位置: %.4fs", motion_preview_time);

    if (motion_selected_track >= 0 &&
        motion_selected_track < static_cast<int>(motion_editor_asset.tracks.size()))
    {
        MotionTrack& selected_track = motion_editor_asset.tracks[motion_selected_track];
        if (ImGui::Button(u8"再生位置にキーを追加"))
        {
            ReplayEngine::Reflection::PropertyValue value;
            std::string curve_error;
            const bool evaluated_ok = evaluate_track_at_time(selected_track,
                motion_preview_time, value, &curve_error);
            push_motion_curve_warning_once(curve_error);
            if (!evaluated_ok && !selected_track.keys.empty())
                value = selected_track.keys.back().value;
            motion_edit_history.Begin(motion_editor_asset, u8"キーを追加");
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
        ReplayEngine::Editor::EditorHelp::Item("button.timeline.add_key",
            u8"現在の再生位置の値を読み取り、選択中の Track にキーを追加します。");
        ImGui::SameLine();
        if (ImGui::Button(u8"コピー")) copy_motion_keys();
        ReplayEngine::Editor::EditorHelp::Item("button.timeline.copy_keys",
            u8"選択中のキーを内部クリップボードへコピーします。");
        ImGui::SameLine();
        if (ImGui::Button(u8"貼り付け")) paste_motion_keys();
        ReplayEngine::Editor::EditorHelp::Item("button.timeline.paste_keys",
            u8"内部クリップボードのキーを現在位置へ貼り付けます。");
        ImGui::SameLine();
        if (ImGui::Button(u8"複製")) duplicate_motion_keys();
        ReplayEngine::Editor::EditorHelp::Item("button.timeline.duplicate_keys",
            u8"選択中のキーを複製して同じ Track へ追加します。");
        ImGui::SameLine();
        if (ImGui::Button(u8"削除")) delete_motion_keys();
        ReplayEngine::Editor::EditorHelp::Item("button.timeline.delete_keys",
            u8"選択中のキーを Track から削除します。");
        ImGui::SetNextItemWidth(92.0f);
        ImGui::DragFloat(u8"倍率##MotionKeyTimeScale", &motion_key_time_scale,
            0.01f, 0.01f, 100.0f, "%.2fx");
        ImGui::SameLine();
        const char* pivot_labels[] = { u8"選択の先頭", u8"再生ヘッド", u8"モーション先頭" };
        motion_key_time_scale_pivot = (std::max)(0, (std::min)(2, motion_key_time_scale_pivot));
        ImGui::SetNextItemWidth(120.0f);
        if (ImGui::BeginCombo(u8"基準点##MotionKeyTimeScalePivot",
            pivot_labels[motion_key_time_scale_pivot]))
        {
            for (int pivot = 0; pivot < 3; ++pivot)
            {
                const bool selected = motion_key_time_scale_pivot == pivot;
                if (ImGui::Selectable(pivot_labels[pivot], selected))
                    motion_key_time_scale_pivot = pivot;
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (ImGui::Button(u8"時間スケール適用##MotionKeyTimeScaleApply"))
            scale_motion_key_times(motion_key_time_scale, motion_key_time_scale_pivot);
        ReplayEngine::Editor::EditorHelp::Item("button.timeline.scale_key_times",
            u8"選択中のキーの時間間隔を倍率で伸縮します。");
    }

    ImGui::Separator();
    ImGui::TextUnformatted(u8"トラック順");
    ReplayEngine::Editor::ReorderRequest track_move{};
    const bool can_edit_motion = object_editor_context.CanEdit();
    for (std::size_t i = 0; i < motion_editor_asset.tracks.size(); ++i)
    {
        MotionTrack& track = motion_editor_asset.tracks[i];
        const std::string item_id = "MotionTimelineTrack" + std::to_string(i);
        const ReplayEngine::Editor::ReorderableItemResult item =
            ReplayEngine::Editor::DrawReorderableItemEx(
                &motion_editor_asset.tracks, item_id.c_str(), i,
                motion_editor_asset.tracks.size(), track.name.c_str(),
                motion_selected_track == static_cast<int>(i), false,
                can_edit_motion, 0, nullptr,
                [&track, this, i](const char* header_title, ImGuiTreeNodeFlags)
                {
                    ImGui::TextDisabled("%s", PropertyTypeLabel(track.value_type));
                    ImGui::SameLine();
                    const bool clicked = ImGui::Selectable(header_title,
                        motion_selected_track == static_cast<int>(i),
                        ImGuiSelectableFlags_SpanAllColumns);
                    if (clicked)
                    {
                        motion_selected_track = static_cast<int>(i);
                        motion_selected_key = -1;
                        motion_selected_keys.clear();
                        motion_selected_event_track = -1;
                        motion_selected_event = -1;
                    }
                    return false;
                },
                [] {},
                [](ReplayEngine::Editor::ReorderDropInfo&,
                    const ImVec2&, const ImVec2&) {});
        if (item.request.Valid() && !track_move.Valid()) track_move = item.request;
    }
    if (track_move.Valid() && track_move.source < motion_editor_asset.tracks.size() &&
        track_move.destination < motion_editor_asset.tracks.size())
    {
        motion_edit_history.Begin(motion_editor_asset, u8"Motion Track の順序を変更");
        MotionTrack moved = std::move(motion_editor_asset.tracks[track_move.source]);
        motion_editor_asset.tracks.erase(
            motion_editor_asset.tracks.begin() +
            static_cast<std::ptrdiff_t>(track_move.source));
        motion_editor_asset.tracks.insert(
            motion_editor_asset.tracks.begin() +
            static_cast<std::ptrdiff_t>(track_move.destination), std::move(moved));
        if (motion_selected_track == static_cast<int>(track_move.source))
            motion_selected_track = static_cast<int>(track_move.destination);
        else if (track_move.source < track_move.destination &&
            motion_selected_track > static_cast<int>(track_move.source) &&
            motion_selected_track <= static_cast<int>(track_move.destination))
            --motion_selected_track;
        else if (track_move.destination < track_move.source &&
            motion_selected_track >= static_cast<int>(track_move.destination) &&
            motion_selected_track < static_cast<int>(track_move.source))
            ++motion_selected_track;
        motion_edit_history.Commit(motion_editor_asset);
        motion_editor_dirty = true;
    }
    if (const char* active_label = ReplayEngine::Editor::ActiveReorderLabel();
        active_label != nullptr)
    {
        ImGui::TextColored(ImGui::GetStyle().Colors[ImGuiCol_DragDropTarget],
            u8"移動中: %s", active_label);
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
            std::string curve_error;
            const bool evaluated_ok = evaluate_track_at_time(track, time, value,
                &curve_error);
            push_motion_curve_warning_once(curve_error);
            if (!evaluated_ok && !track.keys.empty()) value = track.keys.back().value;
            motion_edit_history.Begin(motion_editor_asset, u8"タイムラインでキーを追加");
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
            ReplayEngine::Editor::EditorHelp::Item("button.timeline.key_marker",
                u8"タイムラインのキーを選択します。ドラッグすると時間位置を移動できます。");
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
                motion_edit_history.Begin(motion_editor_asset, u8"タイムラインでキーを移動");
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

    ReplayEngine::Editor::ReorderRequest event_track_move{};
    const bool can_edit_event_tracks = object_editor_context.CanEdit();
    for (std::size_t event_track_index = 0;
        event_track_index < motion_editor_asset.event_tracks.size(); ++event_track_index)
    {
        MotionEventTrack& track = motion_editor_asset.event_tracks[event_track_index];
        const std::string label = track.object.Valid()
            ? std::string(u8"イベント: ") + track.object.ToString() : std::string(u8"イベント: ブロードキャスト");
        const std::string item_id = "MotionEventTrack" + std::to_string(event_track_index);
        const ReplayEngine::Editor::ReorderableItemResult reorder =
            ReplayEngine::Editor::DrawReorderableItemEx(
                &motion_editor_asset.event_tracks, item_id.c_str(), event_track_index,
                motion_editor_asset.event_tracks.size(), label.c_str(),
                motion_selected_event_track == static_cast<int>(event_track_index), false,
                can_edit_event_tracks, 0, nullptr,
                [this, event_track_index](const char* header_title, ImGuiTreeNodeFlags)
                {
                    const bool clicked = ImGui::Selectable(header_title,
                        motion_selected_event_track == static_cast<int>(event_track_index),
                        ImGuiSelectableFlags_SpanAllColumns, ImVec2(110.0f, 0.0f));
                    if (clicked)
                    {
                        motion_selected_event_track = static_cast<int>(event_track_index);
                        motion_selected_event = -1;
                        motion_selected_track = -1;
                        motion_selected_key = -1;
                        motion_selected_keys.clear();
                    }
                    return false;
                },
                [] {},
                [](ReplayEngine::Editor::ReorderDropInfo&,
                    const ImVec2&, const ImVec2&) {});
        if (reorder.request.Valid() && !event_track_move.Valid())
            event_track_move = reorder.request;

        ImGui::PushID(100000 + static_cast<int>(event_track_index));
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
            ImGui::SmallButton(motion_selected_event_track == static_cast<int>(event_track_index) &&
                motion_selected_event == event_index ? "●" : "○");
            ReplayEngine::Editor::EditorHelp::Item("button.timeline.event_marker",
                u8"タイムラインのイベントを選択します。ドラッグすると発火時刻を移動できます。");
            if (ImGui::IsItemClicked())
            {
                motion_selected_event_track = static_cast<int>(event_track_index);
                motion_selected_event = event_index;
                motion_selected_track = -1;
                motion_selected_key = -1;
                motion_selected_keys.clear();
            }
            if (ImGui::IsItemActivated())
                motion_edit_history.Begin(motion_editor_asset, u8"イベントを移動");
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

    if (event_track_move.Valid() &&
        event_track_move.source < motion_editor_asset.event_tracks.size() &&
        event_track_move.destination < motion_editor_asset.event_tracks.size())
    {
        motion_edit_history.Begin(motion_editor_asset, u8"イベントトラックの順序を変更");
        MotionEventTrack moved = std::move(
            motion_editor_asset.event_tracks[event_track_move.source]);
        motion_editor_asset.event_tracks.erase(
            motion_editor_asset.event_tracks.begin() +
            static_cast<std::ptrdiff_t>(event_track_move.source));
        motion_editor_asset.event_tracks.insert(
            motion_editor_asset.event_tracks.begin() +
            static_cast<std::ptrdiff_t>(event_track_move.destination), std::move(moved));
        if (motion_selected_event_track == static_cast<int>(event_track_move.source))
            motion_selected_event_track = static_cast<int>(event_track_move.destination);
        else if (event_track_move.source < event_track_move.destination &&
            motion_selected_event_track > static_cast<int>(event_track_move.source) &&
            motion_selected_event_track <= static_cast<int>(event_track_move.destination))
            --motion_selected_event_track;
        else if (event_track_move.destination < event_track_move.source &&
            motion_selected_event_track >= static_cast<int>(event_track_move.destination) &&
            motion_selected_event_track < static_cast<int>(event_track_move.source))
            ++motion_selected_event_track;
        motion_edit_history.Commit(motion_editor_asset);
        motion_editor_dirty = true;
    }
    if (const char* active_label = ReplayEngine::Editor::ActiveReorderLabel(
        &motion_editor_asset.event_tracks); active_label != nullptr)
    {
        ImGui::TextColored(ImGui::GetStyle().Colors[ImGuiCol_DragDropTarget],
            u8"移動中: %s", active_label);
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
    REPLAY_PROFILE_SCOPE("Editor/MotionGraph");
    if (!show_motion_graph_panel) return;
    ReplayEngine::Editor::PanelTabColorScope panel_tab_color("Motion");
    if (!ImGui::Begin(u8"グラフエディター", &show_motion_graph_panel))
    {
        ImGui::End();
        return;
    }

    if (!motion_editor_loaded || motion_selected_track < 0 ||
        motion_selected_track >= static_cast<int>(motion_editor_asset.tracks.size()))
    {
        ImGui::TextDisabled(u8"トラック / キーを選択してください。");
        ImGui::End();
        return;
    }

    MotionTrack& track = motion_editor_asset.tracks[motion_selected_track];
    const auto evaluate_track_at_time = [&](const MotionTrack& source, float raw_time,
        ReplayEngine::Reflection::PropertyValue& value, std::string* curve_error)
    {
        std::string remap_error;
        const float evaluated_time = MotionEvaluator::RemapMotionTime(motion_editor_asset,
            raw_time, &asset_database, &remap_error);
        if (motion_editor_asset.time_remap.IsAssigned())
            push_motion_curve_warning_once(remap_error);
        ReplayEngine::Motion::MotionTrackEvaluationContext evaluation_context;
        evaluation_context.time = evaluated_time;
        evaluation_context.raw_time = raw_time;
        evaluation_context.duration = motion_editor_asset.duration;
        evaluation_context.database = &asset_database;
        evaluation_context.error = curve_error;
        return MotionEvaluator::EvaluateTrackWithContext(source, evaluation_context, value);
    };
    bool time_remap_active = false;
    if (motion_editor_asset.time_remap.IsAssigned() && motion_editor_asset.duration > 0.0f)
    {
        std::string remap_error;
        MotionEvaluator::RemapMotionTime(motion_editor_asset, 0.0f, &asset_database, &remap_error);
        push_motion_curve_warning_once(remap_error);
        time_remap_active = remap_error.empty();
    }
    const int channel_count = ChannelCount(track.value_type);
    motion_graph_channel = (std::max)(0, (std::min)(channel_count - 1,
        motion_graph_channel));

    ImGui::Checkbox(u8"速度グラフ", &motion_graph_speed_mode);
    ImGui::SameLine();
    ImGui::Checkbox(u8"他トラックを重ねる", &motion_graph_overlay_tracks);
    if (channel_count > 1)
    {
        ImGui::SameLine();
        ImGui::TextUnformatted(u8"チャンネル:");
        for (int channel = 0; channel < channel_count; ++channel)
        {
            ImGui::SameLine();
            if (ImGui::RadioButton(ChannelLabel(track.value_type, channel),
                motion_graph_channel == channel)) motion_graph_channel = channel;
        }
    }

    struct GraphPoint final
    {
        float time = 0.0f;
        float value = 0.0f;
    };

    const float duration = (std::max)(0.0001f, motion_editor_asset.duration);
    const auto build_eased_curve = [&](const MotionTrack& source, int channel,
        std::vector<GraphPoint>& points)
    {
        points.clear();
        if (!source.enabled || source.keys.empty()) return;
        if (time_remap_active || (source.expression.enabled && !source.expression.source.empty()))
        {
            constexpr int remap_samples = 256;
            for (int sample = 0; sample < remap_samples; ++sample)
            {
                const float raw_time = duration * static_cast<float>(sample) /
                    static_cast<float>(remap_samples - 1);
                ReplayEngine::Reflection::PropertyValue value;
                std::string curve_error;
                if (!evaluate_track_at_time(source, raw_time, value, &curve_error)) continue;
                push_motion_curve_warning_once(curve_error);
                const float scalar = ScalarChannel(value, source.value_type, channel);
                points.push_back({ raw_time, std::isfinite(scalar) ? scalar :
                    (std::numeric_limits<float>::quiet_NaN)() });
            }
            return;
        }
        const auto push_value = [&](float time,
            const ReplayEngine::Reflection::PropertyValue& value)
        {
            const float scalar = ScalarChannel(value, source.value_type, channel);
            if (!std::isfinite(time) || !std::isfinite(scalar)) return;
            points.push_back({ time, scalar });
        };

        if (source.keys.size() == 1)
        {
            push_value(0.0f, source.keys.front().value);
            push_value(duration, source.keys.front().value);
            return;
        }
        if (source.keys.front().time > 0.0f)
        {
            push_value(0.0f, source.keys.front().value);
            push_value(source.keys.front().time, source.keys.front().value);
        }

        const int subdivisions = source.keys.size() > 128 ? 16 : 24;
        for (std::size_t key_index = 0; key_index + 1 < source.keys.size(); ++key_index)
        {
            const MotionKeyframe& a = source.keys[key_index];
            const MotionKeyframe& b = source.keys[key_index + 1];
            const float span = b.time - a.time;
            if (!std::isfinite(span)) continue;
            if (span <= 0.0f)
            {
                push_value(b.time, b.value);
                continue;
            }

            if (a.easing == MotionEasing::Step)
            {
                push_value(a.time, a.value);
                push_value(b.time, a.value);
                push_value(b.time, b.value);
                continue;
            }

            for (int sample = 0; sample <= subdivisions; ++sample)
            {
                const float normalized = static_cast<float>(sample) /
                    static_cast<float>(subdivisions);
                const float sample_time = a.time + span * normalized;
                float eased = normalized;
                if (a.easing == MotionEasing::PresetCurve)
                {
                    const ReplayEngine::Motion::EasingCurveAsset* curve =
                        ReplayEngine::Motion::EasingCurveAsset::Resolve(
                            &asset_database, a.easing_curve);
                    if (curve != nullptr)
                    {
                        eased = curve->Evaluate(normalized);
                    }
                    else
                    {
                        const std::string curve_error = a.easing_curve.IsAssigned()
                            ? std::string(u8"PresetCurve のカーブを解決できません。Linear で評価します。 GUID: ") +
                                a.easing_curve.guid
                            : std::string(u8"PresetCurve のカーブが未設定です。Linear で評価します。");
                        push_motion_curve_warning_once(curve_error);
                    }
                }
                else
                {
                    eased = ReplayEngine::Motion::ApplyEasing(
                        a.easing, normalized, a.bezier);
                }
                if (!std::isfinite(eased))
                {
                    points.push_back({ sample_time,
                        (std::numeric_limits<float>::quiet_NaN)() });
                    continue;
                }
                const ReplayEngine::Reflection::PropertyValue mixed =
                    ReplayEngine::Reflection::PropertyValue::Lerp(a.value, b.value, eased);
                const float scalar = ScalarChannel(mixed, source.value_type, channel);
                points.push_back({ sample_time, std::isfinite(scalar) ? scalar :
                    (std::numeric_limits<float>::quiet_NaN)() });
            }
        }

        if (source.keys.back().time < duration)
        {
            push_value(source.keys.back().time, source.keys.back().value);
            push_value(duration, source.keys.back().value);
        }
    };

    constexpr int sample_count = 256;
    std::vector<float> samples(static_cast<std::size_t>(sample_count), 0.0f);
    ReplayEngine::Reflection::PropertyValue evaluated;
    for (int sample = 0; sample < sample_count; ++sample)
    {
        const float t = duration * sample / static_cast<float>(sample_count - 1);
        float value = 0.0f;
        std::string curve_error;
        if (evaluate_track_at_time(track, t, evaluated, &curve_error))
            value = ScalarChannel(evaluated, track.value_type, motion_graph_channel);
        push_motion_curve_warning_once(curve_error);
        samples[static_cast<std::size_t>(sample)] = std::isfinite(value) ? value : 0.0f;
    }
    if (motion_graph_speed_mode)
    {
        const float dt = duration / static_cast<float>(sample_count - 1);
        std::vector<float> speed(samples.size(), 0.0f);
        for (int sample = 1; sample < sample_count - 1; ++sample)
        {
            const float value = (samples[static_cast<std::size_t>(sample + 1)] -
                samples[static_cast<std::size_t>(sample - 1)]) / (2.0f * dt);
            speed[static_cast<std::size_t>(sample)] = std::isfinite(value) ? value : 0.0f;
        }
        if (sample_count > 1)
        {
            speed.front() = (samples[1] - samples[0]) / dt;
            speed.back() = (samples.back() - samples[samples.size() - 2]) / dt;
        }
        samples.swap(speed);
    }

    std::vector<GraphPoint> eased_points;
    if (!motion_graph_speed_mode)
        build_eased_curve(track, motion_graph_channel, eased_points);

    struct OverlayCurve final
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
                float value = 0.0f;
                std::string curve_error;
                if (evaluate_track_at_time(overlay_track, t, evaluated, &curve_error))
                {
                    value = ScalarChannel(evaluated, overlay_track.value_type,
                        motion_graph_channel);
                }
                push_motion_curve_warning_once(curve_error);
                curve.values[static_cast<std::size_t>(sample)] =
                    std::isfinite(value) ? value : 0.0f;
            }
            if (motion_graph_speed_mode)
            {
                const float dt = duration / static_cast<float>(sample_count - 1);
                std::vector<float> speed(curve.values.size(), 0.0f);
                for (int sample = 1; sample < sample_count - 1; ++sample)
                {
                    const float value =
                        (curve.values[static_cast<std::size_t>(sample + 1)] -
                            curve.values[static_cast<std::size_t>(sample - 1)]) / (2.0f * dt);
                    speed[static_cast<std::size_t>(sample)] =
                        std::isfinite(value) ? value : 0.0f;
                }
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

    float min_value = (std::numeric_limits<float>::max)();
    float max_value = (std::numeric_limits<float>::lowest)();
    if (motion_graph_speed_mode)
    {
        for (const float value : samples)
        {
            if (!std::isfinite(value)) continue;
            min_value = (std::min)(min_value, value);
            max_value = (std::max)(max_value, value);
        }
    }
    else
    {
        for (const GraphPoint& point : eased_points)
        {
            if (!std::isfinite(point.value)) continue;
            min_value = (std::min)(min_value, point.value);
            max_value = (std::max)(max_value, point.value);
        }
    }
    for (const OverlayCurve& curve : overlay_curves)
    {
        for (const float value : curve.values)
        {
            if (!std::isfinite(value)) continue;
            min_value = (std::min)(min_value, value);
            max_value = (std::max)(max_value, value);
        }
    }
    if (!std::isfinite(min_value) || !std::isfinite(max_value))
    {
        min_value = 0.0f;
        max_value = 1.0f;
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
    const auto to_graph = [&](float time, float value)
    {
        const float normalized_time = std::clamp(time / duration, 0.0f, 1.0f);
        const float x = graph_origin.x + graph_size.x * normalized_time;
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
        {
            const float a_time = duration * static_cast<float>(sample - 1) /
                static_cast<float>(sample_count - 1);
            const float b_time = duration * static_cast<float>(sample) /
                static_cast<float>(sample_count - 1);
            draw->AddLine(to_graph(a_time, curve.values[static_cast<std::size_t>(sample - 1)]),
                to_graph(b_time, curve.values[static_cast<std::size_t>(sample)]), color, 1.0f);
        }
    }

    if (motion_graph_speed_mode)
    {
        for (int sample = 1; sample < sample_count; ++sample)
        {
            const float a_time = duration * static_cast<float>(sample - 1) /
                static_cast<float>(sample_count - 1);
            const float b_time = duration * static_cast<float>(sample) /
                static_cast<float>(sample_count - 1);
            draw->AddLine(to_graph(a_time, samples[static_cast<std::size_t>(sample - 1)]),
                to_graph(b_time, samples[static_cast<std::size_t>(sample)]),
                IM_COL32(85, 190, 255, 255), 2.0f);
        }
    }
    else
    {
        for (std::size_t index = 1; index < eased_points.size(); ++index)
        {
            const GraphPoint& a = eased_points[index - 1];
            const GraphPoint& b = eased_points[index];
            if (!std::isfinite(a.time) || !std::isfinite(a.value) ||
                !std::isfinite(b.time) || !std::isfinite(b.value)) continue;
            draw->AddLine(to_graph(a.time, a.value), to_graph(b.time, b.value),
                IM_COL32(85, 190, 255, 255), 2.0f);
        }
    }

    const float playhead_time = std::clamp(motion_preview_time, 0.0f, duration);
    const float playhead_x = graph_origin.x + graph_size.x * playhead_time / duration;
    draw->AddLine(ImVec2(playhead_x, graph_origin.y),
        ImVec2(playhead_x, graph_origin.y + graph_size.y), IM_COL32(255, 210, 70, 185), 1.0f);

    bool playhead_value_valid = false;
    float playhead_value = 0.0f;
    if (!motion_graph_speed_mode)
    {
        std::string curve_error;
        if (evaluate_track_at_time(track, playhead_time, evaluated, &curve_error))
        {
            playhead_value = ScalarChannel(evaluated, track.value_type, motion_graph_channel);
            playhead_value_valid = std::isfinite(playhead_value);
        }
        push_motion_curve_warning_once(curve_error);
    }
    else
    {
        const float delta = (std::max)(0.0001f, duration /
            static_cast<float>(sample_count - 1));
        const float before_time = (std::max)(0.0f, playhead_time - delta);
        const float after_time = (std::min)(duration, playhead_time + delta);
        ReplayEngine::Reflection::PropertyValue before_value;
        ReplayEngine::Reflection::PropertyValue after_value;
        bool before_ok = false;
        bool after_ok = false;
        if (after_time > before_time)
        {
            std::string before_error;
            before_ok = evaluate_track_at_time(track, before_time, before_value,
                &before_error);
            push_motion_curve_warning_once(before_error);
            if (before_ok)
            {
                std::string after_error;
                after_ok = evaluate_track_at_time(track, after_time, after_value,
                    &after_error);
                push_motion_curve_warning_once(after_error);
            }
        }
        if (before_ok && after_ok)
        {
            const float before = ScalarChannel(before_value, track.value_type, motion_graph_channel);
            const float after = ScalarChannel(after_value, track.value_type, motion_graph_channel);
            playhead_value = (after - before) / (after_time - before_time);
            playhead_value_valid = std::isfinite(playhead_value);
        }
    }

    if (!motion_graph_speed_mode)
    {
        for (int key_index = 0; key_index < static_cast<int>(track.keys.size()); ++key_index)
        {
            const MotionKeyframe& key = track.keys[key_index];
            const float value = ScalarChannel(key.value, track.value_type, motion_graph_channel);
            if (!std::isfinite(value)) continue;
            const ImVec2 point = to_graph(key.time, value);
            draw->AddCircleFilled(point,
                motion_selected_key == key_index ? 5.0f : 3.5f,
                motion_selected_key == key_index ? IM_COL32(255,210,70,255) :
                    IM_COL32(220,220,220,255));
        }
    }

    if (playhead_value_valid)
    {
        const ImVec2 intersection = to_graph(playhead_time, playhead_value);
        draw->AddCircleFilled(intersection, 4.0f, IM_COL32(255, 225, 95, 255));
        char value_text[80]{};
        std::snprintf(value_text, sizeof(value_text), "%.4f", playhead_value);
        const ImVec2 text_size = ImGui::CalcTextSize(value_text);
        const float label_x = (std::min)(graph_origin.x + graph_size.x - text_size.x - 4.0f,
            intersection.x + 6.0f);
        const float label_y = (std::max)(graph_origin.y + 3.0f, intersection.y - text_size.y - 3.0f);
        draw->AddText(ImVec2(label_x, label_y), IM_COL32(255, 235, 145, 255), value_text);
    }

    ImGui::Text(u8"範囲 %.4f .. %.4f   %s", min_value, max_value,
        motion_graph_speed_mode ? u8"値/秒" : u8"値");
    if (!overlay_curves.empty())
    {
        ImGui::TextDisabled(u8"選択: %s", track.name.c_str());
        for (const OverlayCurve& curve : overlay_curves)
        {
            ImGui::SameLine();
            ImGui::TextDisabled("| %s", curve.name.c_str());
        }
    }

    if (motion_selected_key < 0 || motion_selected_key >= static_cast<int>(track.keys.size()))
    {
        ImGui::TextDisabled(u8"キーを選ぶと補間ハンドルを編集できます。");
        ImGui::End();
        return;
    }

    MotionKeyframe& key = track.keys[motion_selected_key];
    ImGui::Separator();
    if (key.easing == MotionEasing::PresetCurve && key.easing_curve.IsAssigned())
        motion_selected_easing_curve = key.easing_curve;
    MotionEasing easing = key.easing;
    ReplayEngine::Reflection::AssetReference easing_curve = key.easing_curve;
    if (DrawEasingCombo(u8"イージング", easing, &easing_curve, &asset_database))
    {
        motion_edit_history.Begin(motion_editor_asset, u8"イージングを変更");
        key.easing = easing;
        key.easing_curve = easing_curve;
        motion_edit_history.Commit(motion_editor_asset);
        motion_editor_dirty = true;
        if (easing == MotionEasing::PresetCurve && easing_curve.IsAssigned())
            motion_selected_easing_curve = easing_curve;
    }
    if (ImGui::Button(u8"自動スムーズ"))
    {
        motion_edit_history.Begin(motion_editor_asset, u8"自動スムーズ");
        ReplayEngine::Motion::MotionBezierHandles smooth_handles{};
        if (BuildAutoSmoothBezier(track, motion_selected_key, motion_graph_channel, smooth_handles))
        {
            key.easing = MotionEasing::CustomBezier;
            key.bezier = smooth_handles;
        }
        else
        {
            key.easing = MotionEasing::EaseInOutCubic;
        }
        motion_edit_history.Commit(motion_editor_asset);
        motion_editor_dirty = true;
    }
    ReplayEngine::Editor::EditorHelp::Item("button.timeline.auto_smooth",
        u8"前後のキーからベジェハンドルを自動計算して、動きを滑らかにします。");
    ImGui::SameLine();
    if (ImGui::Button(u8"線形"))
    {
        motion_edit_history.Begin(motion_editor_asset, u8"線形");
        key.easing = MotionEasing::Linear;
        motion_edit_history.Commit(motion_editor_asset);
        motion_editor_dirty = true;
    }
    ReplayEngine::Editor::EditorHelp::Item("button.timeline.linear_easing",
        u8"キー間の値を一定速度で補間します。");
    ImGui::SameLine();
    if (ImGui::Button(u8"イーズイン"))
    {
        motion_edit_history.Begin(motion_editor_asset, u8"イーズイン");
        key.easing = MotionEasing::CustomBezier;
        key.bezier.out_handle = { 0.42f, 0.0f };
        key.bezier.in_handle = { 1.0f, 1.0f };
        motion_edit_history.Commit(motion_editor_asset); motion_editor_dirty = true;
    }
    ReplayEngine::Editor::EditorHelp::Item("button.timeline.ease_in",
        u8"開始時をゆっくり、終了時を速くするベジェ補間を設定します。");
    ImGui::SameLine();
    if (ImGui::Button(u8"イーズアウト"))
    {
        motion_edit_history.Begin(motion_editor_asset, u8"イーズアウト");
        key.easing = MotionEasing::CustomBezier;
        key.bezier.out_handle = { 0.0f, 0.0f };
        key.bezier.in_handle = { 0.58f, 1.0f };
        motion_edit_history.Commit(motion_editor_asset); motion_editor_dirty = true;
    }
    ReplayEngine::Editor::EditorHelp::Item("button.timeline.ease_out",
        u8"開始時を速く、終了時をゆっくりするベジェ補間を設定します。");
    ImGui::SameLine();
    if (ImGui::Button(u8"イーズインアウト"))
    {
        motion_edit_history.Begin(motion_editor_asset, u8"イーズインアウト");
        key.easing = MotionEasing::CustomBezier;
        key.bezier.out_handle = { 0.42f, 0.0f };
        key.bezier.in_handle = { 0.58f, 1.0f };
        motion_edit_history.Commit(motion_editor_asset); motion_editor_dirty = true;
    }
    ReplayEngine::Editor::EditorHelp::Item("button.timeline.ease_in_out",
        u8"開始時と終了時をゆっくりするベジェ補間を設定します。");
    ImGui::SameLine();
    if (ImGui::Button(u8"保持"))
    {
        motion_edit_history.Begin(motion_editor_asset, u8"保持");
        key.easing = MotionEasing::Step;
        motion_edit_history.Commit(motion_editor_asset); motion_editor_dirty = true;
    }
    ReplayEngine::Editor::EditorHelp::Item("button.timeline.step_easing",
        u8"次のキーまで値を変化させず、段階的に切り替える補間を設定します。");

    if (key.easing == MotionEasing::CustomBezier)
    {
        float out_handle[2]{ key.bezier.out_handle.x, key.bezier.out_handle.y };
        float in_handle[2]{ key.bezier.in_handle.x, key.bezier.in_handle.y };
        bool numeric_changed = false;
        numeric_changed |= ImGui::DragFloat2(u8"出力ハンドル", out_handle, 0.005f, -2.0f, 2.0f);
        numeric_changed |= ImGui::DragFloat2(u8"入力ハンドル", in_handle, 0.005f, -2.0f, 2.0f);
        if (numeric_changed)
        {
            motion_edit_history.Begin(motion_editor_asset, u8"ベジェを変更");
            key.bezier.out_handle = { out_handle[0], out_handle[1] };
            key.bezier.in_handle = { in_handle[0], in_handle[1] };
            motion_edit_history.Commit(motion_editor_asset);
            motion_editor_dirty = true;
        }

        // ベジェハンドルは0..1外も許可し、予備動作とオーバーシュートを編集できるようにする。
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
                u8"ベジェハンドルをドラッグ");
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
