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

// Timeline / Graph Editor 描画の関数本体

void framework::draw_motion_timeline()
{
    if (!show_motion_timeline_panel) return;
    if (!ImGui::Begin("タイムライン", &show_motion_timeline_panel))
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

    const float width = (std::max)(1.0f, ImGui::GetContentRegionAvail().x - 120.0f);

    // キーを置ける横幅は、線の長さからマーカー 1 個ぶんを引いた範囲にする。
    //
    // 以前は t = 1.0（＝末尾のキー）を origin.x + width へ置いていた。
    // そこはちょうど線の右端なので、ボタンがその幅ぶん外側へはみ出し、
    // ImGui にクリップされて「見えない・掴めない」キーになっていた。
    // 末尾のキーは必ず作られるため、実質いつも 1 個掴めない状態だった。
    const float marker_width =
        ImGui::CalcTextSize("◆").x + ImGui::GetStyle().FramePadding.x * 2.0f;
    const float marker_span = (std::max)(1.0f, width - marker_width);

    for (int track_index = 0;
        track_index < static_cast<int>(motion_editor_asset.tracks.size());
        ++track_index)
    {
        MotionTrack& track = motion_editor_asset.tracks[track_index];
        ImGui::PushID(track_index);
        if (ImGui::Selectable(track.name.c_str(), motion_selected_track == track_index,
            ImGuiSelectableFlags_SpanAllColumns, ImVec2(110.0f, 0.0f)))
        {
            motion_selected_track = track_index;
            motion_selected_key = -1;
            motion_selected_event_track = -1;
            motion_selected_event = -1;
        }
        ImGui::SameLine();
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        draw_list->AddLine(origin, ImVec2(origin.x + width, origin.y),
            IM_COL32(100, 100, 100, 255), 1.0f);
        for (int key_index = 0; key_index < static_cast<int>(track.keys.size());
            ++key_index)
        {
            const float t = motion_editor_asset.duration > 0.0f
                ? track.keys[key_index].time / motion_editor_asset.duration : 0.0f;
            ImGui::SetCursorScreenPos(ImVec2(origin.x + marker_span * t, origin.y - 6.0f));
            ImGui::PushID(key_index);
            if (ImGui::SmallButton(motion_selected_track == track_index &&
                motion_selected_key == key_index ? "◆" : "◇"))
            {
                motion_selected_track = track_index;
                motion_selected_key = key_index;
                motion_selected_event_track = -1;
                motion_selected_event = -1;
            }
            ImGui::PopID();
        }
        ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + 18.0f));
        ImGui::PopID();
    }

    for (int event_track_index = 0;
        event_track_index < static_cast<int>(motion_editor_asset.event_tracks.size());
        ++event_track_index)
    {
        MotionEventTrack& track = motion_editor_asset.event_tracks[event_track_index];
        ImGui::PushID(100000 + event_track_index);
        const std::string label = track.object.Valid()
            ? "Event: " + track.object.ToString() : "Event: Broadcast";
        if (ImGui::Selectable(label.c_str(),
            motion_selected_event_track == event_track_index,
            ImGuiSelectableFlags_SpanAllColumns, ImVec2(110.0f, 0.0f)))
        {
            motion_selected_event_track = event_track_index;
            motion_selected_event = -1;
            motion_selected_track = -1;
            motion_selected_key = -1;
        }
        ImGui::SameLine();
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        draw_list->AddLine(origin, ImVec2(origin.x + width, origin.y),
            IM_COL32(150, 110, 70, 255), 1.0f);
        for (int event_index = 0;
            event_index < static_cast<int>(track.events.size()); ++event_index)
        {
            const float t = motion_editor_asset.duration > 0.0f
                ? track.events[event_index].time / motion_editor_asset.duration : 0.0f;
            ImGui::SetCursorScreenPos(ImVec2(origin.x + marker_span * t, origin.y - 6.0f));
            ImGui::PushID(event_index);
            if (ImGui::SmallButton(motion_selected_event_track == event_track_index &&
                motion_selected_event == event_index ? "●" : "○"))
            {
                motion_selected_event_track = event_track_index;
                motion_selected_event = event_index;
                motion_selected_track = -1;
                motion_selected_key = -1;
            }
            ImGui::PopID();
        }
        ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + 18.0f));
        ImGui::PopID();
    }

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
        ImGui::TextDisabled("Keyを選択してください。");
        ImGui::End();
        return;
    }

    MotionTrack& track = motion_editor_asset.tracks[motion_selected_track];
    if (motion_selected_key < 0 ||
        motion_selected_key >= static_cast<int>(track.keys.size()))
    {
        ImGui::TextDisabled("Keyを選択してください。");
        ImGui::End();
        return;
    }

    MotionKeyframe& key = track.keys[motion_selected_key];
    ImGui::Text("Easing: %s", ReplayEngine::Motion::ToString(key.easing));
    if (key.easing == MotionEasing::CustomBezier)
    {
        float out_handle[2]{ key.bezier.out_handle.x, key.bezier.out_handle.y };
        float in_handle[2]{ key.bezier.in_handle.x, key.bezier.in_handle.y };
        bool changed = false;
        changed |= ImGui::DragFloat2("Out", out_handle, 0.01f, -2.0f, 2.0f);
        changed |= ImGui::DragFloat2("In", in_handle, 0.01f, -2.0f, 2.0f);
        if (changed)
        {
            motion_edit_history.Begin(motion_editor_asset, "Bezierを変更");
            key.bezier.out_handle = { out_handle[0], out_handle[1] };
            key.bezier.in_handle = { in_handle[0], in_handle[1] };
            motion_edit_history.Commit(motion_editor_asset);
            motion_editor_dirty = true;
        }
    }
    else
    {
        ImGui::TextDisabled("CustomBezier のときだけハンドルを編集できます。");
    }

    // ---- 拡張点: Graph Curve View -----------------------------------------
    //
    // 【今は入れていない理由】
    //   Phase 4 は Motion Asset の作成・キー編集・Preview/Undo の経路を固定する段階。
    //   曲線描画は選択範囲、スナップ、複数値チャンネル表示の仕様が必要なため後段へ回す。
    //
    // 【入れるときにここへ足す】
    //   ・PropertyValue をチャンネルへ分解し、選択キーの前後区間を描く
    //   ・Back / Elastic の 0..1 外オーバーシュートを表示範囲へ含める
    //   ・キー移動は MotionEditHistory の 1 トランザクションにまとめる
    ImGui::End();
}
