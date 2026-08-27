#include "framework.h"

#include "../../RePlayEngine/Components/UI/UIImageComponent.h"
#include "../../RePlayEngine/Motion/MotionBindingResolver.h"
#include "../../RePlayEngine/Motion/MotionEvaluator.h"
#include "../../RePlayEngine/Motion/MotionExpression.h"
#include "../../RePlayEngine/Object/GameObject/GameObject.h"
#include "../../RePlayEngine/Object/Registry/ComponentRegistry.h"
#include "../../RePlayEngine/Reflection/Registry/PropertyRegistry.h"
#include "../../RePlayEngine/Scene/Runtime/Scene.h"

#include "imgui/imgui_internal.h"

#include <algorithm>
#include <array>
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
    if (!ImGui::Begin(u8"Motion インスペクター", &show_motion_inspector_panel))
    {
        ImGui::End();
        return;
    }

    if (!motion_editor_loaded)
    {
        ImGui::TextDisabled(u8"モーションアセットが未選択です。");
        ImGui::End();
        return;
    }

    char name_buffer[256]{};
    strncpy_s(name_buffer, motion_editor_asset.name.c_str(), _TRUNCATE);
    if (ImGui::InputText(u8"モーション名", name_buffer, IM_ARRAYSIZE(name_buffer)))
    {
        motion_edit_history.Begin(motion_editor_asset, u8"モーション名を変更");
        motion_editor_asset.name = name_buffer;
        motion_edit_history.Commit(motion_editor_asset);
        motion_editor_dirty = true;
    }
    float duration = motion_editor_asset.duration;
    if (ImGui::DragFloat(u8"モーションの長さ", &duration, 0.01f, 0.0f, 3600.0f, "%.2f s"))
    {
        motion_edit_history.Begin(motion_editor_asset, u8"モーションの長さを変更");
        motion_editor_asset.duration = (std::max)(0.0f, duration);
        motion_edit_history.Commit(motion_editor_asset);
        motion_editor_dirty = true;
    }

    ReplayEngine::Reflection::AssetReference time_remap = motion_editor_asset.time_remap;
    const ReplayEngine::Assets::AssetRecord* time_remap_record = time_remap.IsAssigned()
        ? asset_database.FindByGuid(time_remap.guid) : nullptr;
    const bool time_remap_resolved = time_remap_record != nullptr &&
        time_remap_record->kind == ReplayEngine::Assets::AssetKind::EasingCurve &&
        ReplayEngine::Motion::EasingCurveAsset::Resolve(&asset_database, time_remap) != nullptr;
    const char* time_remap_preview = !time_remap.IsAssigned() ? u8"未設定" :
        (time_remap_resolved ? time_remap_record->display_name.c_str() : u8"見つかりません");
    bool time_remap_changed = false;
    if (ImGui::BeginCombo(u8"時間リマップ##MotionTimeRemapCurve", time_remap_preview))
    {
        const bool unset = !time_remap.IsAssigned();
        if (ImGui::Selectable(u8"未設定", unset))
        {
            time_remap.Clear();
            time_remap_changed = true;
        }
        if (unset) ImGui::SetItemDefaultFocus();
        for (const ReplayEngine::Assets::AssetRecord& record : asset_database.Records())
        {
            if (record.kind != ReplayEngine::Assets::AssetKind::EasingCurve) continue;
            ImGui::PushID(record.guid.c_str());
            const bool selected = time_remap.guid == record.guid;
            if (ImGui::Selectable(record.display_name.c_str(), selected))
            {
                time_remap.guid = record.guid;
                time_remap_changed = true;
            }
            if (selected) ImGui::SetItemDefaultFocus();
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    if (time_remap_changed)
    {
        motion_edit_history.Begin(motion_editor_asset, u8"時間リマップを変更");
        motion_editor_asset.time_remap = time_remap;
        motion_edit_history.Commit(motion_editor_asset);
        motion_editor_dirty = true;
        motion_easing_curve_warning_guids.clear();
    }
    if (motion_editor_asset.time_remap.IsAssigned())
    {
        std::string remap_error;
        MotionEvaluator::RemapMotionTime(motion_editor_asset, motion_preview_time,
            &asset_database, &remap_error);
        push_motion_curve_warning_once(remap_error);
    }

    ImGui::Separator();
    if (ImGui::Button(u8"イベントトラックを追加"))
    {
        stop_motion_preview();
        motion_edit_history.Begin(motion_editor_asset, u8"イベントトラックを追加");
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
        ImGui::Text(u8"イベントトラック %d", motion_selected_event_track + 1);
        ImGui::Text(u8"送信先オブジェクトID: %s", event_track.object.Valid()
            ? event_track.object.ToString().c_str() : u8"(なし / ブロードキャスト)");

        if (ImGui::Button(u8"選択中オブジェクトを送信先にする"))
        {
            ReplayEngine::Scene::Scene* scene = object_editor_context.GetScene();
            ReplayEngine::Core::GameObject* selected = scene != nullptr
                ? object_editor_context.Selection().ResolvePrimary(*scene) : nullptr;
            if (selected != nullptr)
            {
                motion_edit_history.Begin(motion_editor_asset, u8"イベント送信先を変更");
                event_track.object = selected->ID();
                motion_edit_history.Commit(motion_editor_asset);
                motion_editor_dirty = true;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button(u8"送信先なし"))
        {
            motion_edit_history.Begin(motion_editor_asset, u8"イベント送信先を解除");
            event_track.object = ReplayEngine::Core::ObjectID::Invalid();
            motion_edit_history.Commit(motion_editor_asset);
            motion_editor_dirty = true;
        }

        if (ImGui::Button(u8"イベントを追加"))
        {
            motion_edit_history.Begin(motion_editor_asset, u8"イベントを追加");
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
        if (ImGui::Button(u8"イベントトラックを削除"))
        {
            stop_motion_preview();
            motion_edit_history.Begin(motion_editor_asset, u8"イベントトラックを削除");
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
            ImGui::Text(u8"イベント %d", motion_selected_event + 1);
            float event_time = event.time;
            if (ImGui::DragFloat(u8"イベント時刻", &event_time, 0.01f, 0.0f,
                motion_editor_asset.duration))
            {
                motion_edit_history.Begin(motion_editor_asset, u8"イベント時刻を変更");
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
            if (ImGui::InputText(u8"イベント名", event_name, IM_ARRAYSIZE(event_name)))
            {
                motion_edit_history.Begin(motion_editor_asset, u8"イベント名を変更");
                event.name = event_name;
                motion_edit_history.Commit(motion_editor_asset);
                motion_editor_dirty = true;
            }
            char event_parameter[512]{};
            strncpy_s(event_parameter, event.parameter.c_str(), _TRUNCATE);
            if (ImGui::InputText(u8"パラメータ", event_parameter,
                IM_ARRAYSIZE(event_parameter)))
            {
                motion_edit_history.Begin(motion_editor_asset, u8"イベントパラメータを変更");
                event.parameter = event_parameter;
                motion_edit_history.Commit(motion_editor_asset);
                motion_editor_dirty = true;
            }
            if (ImGui::Button(u8"イベントを削除"))
            {
                motion_edit_history.Begin(motion_editor_asset, u8"イベントを削除");
                event_track.events.erase(event_track.events.begin() + motion_selected_event);
                motion_selected_event = -1;
                motion_edit_history.Commit(motion_editor_asset);
                motion_editor_dirty = true;
            }
        }

        ImGui::TextDisabled(u8"イベントトラックは値を持たないためグラフエディターには表示しません。");
        ImGui::End();
        return;
    }

    if (motion_selected_track < 0 ||
        motion_selected_track >= static_cast<int>(motion_editor_asset.tracks.size()))
    {
        ImGui::TextDisabled(u8"トラックまたはイベントトラックを選択してください。");
        ImGui::End();
        return;
    }

    MotionTrack& track = motion_editor_asset.tracks[motion_selected_track];
    ImGui::Separator();
    char track_name[256]{};
    strncpy_s(track_name, track.name.c_str(), _TRUNCATE);
    if (ImGui::InputText(u8"トラック名", track_name, IM_ARRAYSIZE(track_name)))
    {
        motion_edit_history.Begin(motion_editor_asset, u8"トラック名を変更");
        track.name = track_name;
        motion_edit_history.Commit(motion_editor_asset);
        motion_editor_dirty = true;
    }
    if (ImGui::Checkbox(u8"有効", &track.enabled))
    {
        motion_edit_history.Begin(motion_editor_asset, u8"トラックの有効状態を変更");
        motion_edit_history.Commit(motion_editor_asset);
        motion_editor_dirty = true;
    }
    MotionBlendMode blend_mode = track.blend_mode;
    if (DrawBlendModeCombo(u8"ブレンドモード", blend_mode))
    {
        motion_edit_history.Begin(motion_editor_asset, u8"トラックのブレンドモードを変更");
        track.blend_mode = blend_mode;
        motion_edit_history.Commit(motion_editor_asset);
        motion_editor_dirty = true;
    }

    if (ImGui::CollapsingHeader(u8"Wiggle（揺れ） / ループ###MotionTrackModifiers"))
    {
        ReplayEngine::Motion::MotionWiggle wiggle = track.wiggle;
        bool wiggle_changed = false;
        wiggle_changed |= ImGui::Checkbox(u8"有効##MotionWiggleEnabled", &wiggle.enabled);
        wiggle_changed |= ImGui::DragFloat(u8"振幅##MotionWiggleAmplitude",
            &wiggle.amplitude, 0.01f);
        wiggle_changed |= ImGui::DragFloat(u8"周波数 (Hz)##MotionWiggleFrequency",
            &wiggle.frequency, 0.05f);
        wiggle_changed |= ImGui::DragInt(u8"シード##MotionWiggleSeed", &wiggle.seed, 1.0f);
        wiggle_changed |= ImGui::DragInt(u8"オクターブ##MotionWiggleOctaves",
            &wiggle.octaves, 1.0f, 1, 4);
        wiggle.amplitude = (std::max)(0.0f, wiggle.amplitude);
        wiggle.frequency = (std::max)(0.0f, wiggle.frequency);
        wiggle.octaves = (std::max)(1, (std::min)(4, wiggle.octaves));
        if (wiggle_changed)
        {
            motion_edit_history.Begin(motion_editor_asset, u8"Wiggleを変更");
            track.wiggle = wiggle;
            motion_edit_history.Commit(motion_editor_asset);
            motion_editor_dirty = true;
        }
        if (track.wiggle.enabled && !SupportsMotionWiggle(track.value_type))
            ImGui::TextDisabled(u8"この型には Wiggle は適用されません。");

        ReplayEngine::Motion::MotionTrackLoop loop = track.loop;
        if (DrawMotionTrackLoopCombo(u8"ループ##MotionTrackLoop", loop))
        {
            motion_edit_history.Begin(motion_editor_asset, u8"トラックのループを変更");
            track.loop = loop;
            motion_edit_history.Commit(motion_editor_asset);
            motion_editor_dirty = true;
        }
    }

    if (ImGui::CollapsingHeader(u8"式 / エクスプレッション###MotionTrackExpression"))
    {
        bool expression_enabled = track.expression.enabled;
        if (ImGui::Checkbox(u8"有効##MotionExpressionEnabled", &expression_enabled))
        {
            motion_edit_history.Begin(motion_editor_asset, u8"式の有効状態を変更");
            track.expression.enabled = expression_enabled;
            motion_edit_history.Commit(motion_editor_asset);
            motion_editor_dirty = true;
        }

        std::array<char, 4096> expression_buffer{};
        strncpy_s(expression_buffer.data(), expression_buffer.size(),
            track.expression.source.c_str(), _TRUNCATE);
        const bool expression_changed = ImGui::InputTextMultiline(
            u8"式##MotionExpressionSource", expression_buffer.data(),
            expression_buffer.size(), ImVec2(-1.0f, 96.0f));
        if (expression_changed)
        {
            motion_edit_history.Begin(motion_editor_asset, u8"式を変更");
            track.expression.source = expression_buffer.data();
            motion_editor_dirty = true;
            motion_easing_curve_warning_guids.clear();
        }
        if (ImGui::IsItemDeactivatedAfterEdit())
            motion_edit_history.Commit(motion_editor_asset);

        ImGui::TextDisabled(u8"変数: v, t, rt, d, i, n");
        ImGui::TextDisabled(u8"関数: abs sign min max clamp floor ceil round sqrt pow exp log mod");
        ImGui::TextDisabled(u8"      sin cos tan asin acos atan atan2 lerp step smoothstep noise wiggle");
        if (track.expression.enabled &&
            !ReplayEngine::Motion::MotionExpressionEvaluator::SupportsType(track.value_type))
        {
            ImGui::TextDisabled(u8"この型には式は適用されません。");
        }
        else if (track.expression.enabled)
        {
            std::string expression_error;
            if (!ReplayEngine::Motion::MotionExpressionEvaluator::Validate(
                track.expression.source, expression_error))
            {
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.35f, 1.0f),
                    u8"式エラー: %s", expression_error.c_str());
            }
        }
    }

    int binding_origin = track.binding.origin;
    if (DrawMotionBindingOriginCombo(u8"バインド起点", binding_origin) &&
        binding_origin != track.binding.origin)
    {
        motion_edit_history.Begin(motion_editor_asset, u8"モーションの起点を変更");
        track.binding.origin = binding_origin;
        motion_edit_history.Commit(motion_editor_asset);
        motion_editor_dirty = true;
    }
    if (track.binding.origin == static_cast<int>(MotionBindingOrigin::ChildPath))
    {
        char relative_path[512]{};
        strncpy_s(relative_path, track.binding.relative_path.c_str(), _TRUNCATE);
        if (ImGui::InputText(u8"子への相対パス", relative_path,
            IM_ARRAYSIZE(relative_path)))
        {
            motion_edit_history.Begin(motion_editor_asset, u8"モーションの子パスを変更");
            track.binding.relative_path = relative_path;
            motion_edit_history.Commit(motion_editor_asset);
            motion_editor_dirty = true;
        }
    }

    ReplayEngine::Scene::Scene* scene = object_editor_context.GetScene();
    Component* bound_component = scene != nullptr
        ? ResolveBindingComponent(*scene, track.binding) : nullptr;
    ImGui::Text(u8"オブジェクトID: %s", track.binding.object.ToString().c_str());
    ImGui::Text(u8"プロパティ: %s", track.binding.property.c_str());
    if (bound_component == nullptr) ImGui::TextColored(
        ImVec4(1.0f, 0.55f, 0.35f, 1.0f), u8"バインド未解決");

    if (ImGui::Button(u8"キーを追加")) add_motion_key_at_preview_time();
    ImGui::SameLine();
    ImGui::TextDisabled("(S)");
    ImGui::SameLine();
    if (ImGui::Button(u8"トラックを削除"))
    {
        stop_motion_preview();
        motion_edit_history.Begin(motion_editor_asset, u8"トラックを削除");
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
        ImGui::Text(u8"キー %d", motion_selected_key);
        float key_time = key.time;
        if (ImGui::DragFloat(u8"時刻", &key_time, 0.01f, 0.0f,
            motion_editor_asset.duration))
        {
            motion_edit_history.Begin(motion_editor_asset, u8"キー時刻を変更");
            key.time = (std::max)(0.0f, key_time);
            motion_editor_asset.SortKeys();
            motion_edit_history.Commit(motion_editor_asset);
            motion_editor_dirty = true;
            motion_selected_key = -1;
            ImGui::End();
            return;
        }
        PropertyValue edited = key.value;
        if (DrawValueEditor(u8"値", edited, track.value_type))
        {
            motion_edit_history.Begin(motion_editor_asset, u8"キー値を変更");
            key.value = edited;
            motion_edit_history.Commit(motion_editor_asset);
            motion_editor_dirty = true;
        }
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
    }

    ImGui::End();
}
