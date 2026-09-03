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
#include <cctype>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include "framework_motion_workspaceInternal.h"
using namespace framework_motion_workspace::Detail;

// Motion Layer 編集の関数本体

void framework::draw_motion_layers()
{
    if (!show_motion_layers_panel) return;
    ReplayEngine::Editor::PanelTabColorScope panel_tab_color("Motion");
    if (!ImGui::Begin(u8"Motion レイヤー", &show_motion_layers_panel))
    {
        ImGui::End();
        return;
    }

    if (ImGui::Button(u8"新規"))
    {
        project_create_motion("NewMotion");
    }
    ReplayEngine::Editor::EditorHelp::Item("button.motion.new",
        u8"新しい Motion Asset を作成して編集対象にします。");
    ImGui::SameLine();
    if (ImGui::Button(u8"保存")) save_current_motion_asset();
    ReplayEngine::Editor::EditorHelp::Item("button.motion.save",
        u8"編集中の Motion Asset を保存します。");
    ImGui::SameLine();
    // 選択中の GameObject から Track を作る。
    //
    // 以前は FindFirstAnimatable() で「最初に見つかった 1 つ」を問答無用で
    // 使っていたため、MeshRenderer なら先頭の material_override (Bool) しか
    // 選べず、material.roughness のような目的の項目へ到達できなかった。
    // どれを動かすかは人が選ぶものなので、一覧から選ばせる。
    const auto add_motion_track = [this](ReplayEngine::Core::GameObject& object,
        Component& component, const PropertyDesc& desc)
    {
        if (!motion_editor_loaded)
        {
            motion_editor_asset = MotionAsset{};
            motion_editor_asset.name = "UnsavedMotion";
            motion_editor_loaded = true;
            motion_editor_dirty = true;
            motion_editor_path.clear();
            motion_editor_guid.clear();
        }

        stop_motion_preview();
        motion_edit_history.Begin(motion_editor_asset, u8"トラックを追加");
        MotionTrack track;
        track.name = object.Name() + "." + desc.DisplayName();
        track.binding.object = object.ID();
        track.binding.component_type = component.TypeID();
        track.binding.component_index = ComponentTypeIndex(object, &component);
        track.binding.property = desc.name;
        track.value_type = desc.type;
        MotionKeyframe key0;
        key0.time = 0.0f;
        key0.value = desc.Capture(component);
        MotionKeyframe key1 = key0;
        key1.time = (std::max)(0.1f, motion_editor_asset.duration);
        key1.value = TerminalKeyValueForNewTrack(desc, key0.value);
        track.keys.push_back(key0);
        track.keys.push_back(key1);
        motion_editor_asset.tracks.push_back(std::move(track));
        motion_selected_track = static_cast<int>(motion_editor_asset.tracks.size()) - 1;
        motion_selected_key = -1;
        motion_selected_event_track = -1;
        motion_selected_event = -1;
        motion_edit_history.Commit(motion_editor_asset);
        motion_editor_dirty = true;
    };

    if (ImGui::Button(u8"選択からトラックを追加"))
    {
        motion_property_picker_filter.fill('\0');
        ImGui::OpenPopup("MotionPropertyPicker");
    }
    ReplayEngine::Editor::EditorHelp::Item("button.motion.add_track",
        u8"選択中のゲームオブジェクトが持つ、動かせるプロパティの一覧から選びます。\n"
        u8"先にシーン / UI ワークスペースで対象を選択しておいてください。");

    if (ImGui::BeginPopup("MotionPropertyPicker"))
    {
        ReplayEngine::Scene::Scene* scene = object_editor_context.GetScene();
        ReplayEngine::Core::GameObject* object = scene != nullptr
            ? object_editor_context.Selection().ResolvePrimary(*scene) : nullptr;

        if (object == nullptr)
        {
            ImGui::TextDisabled(u8"ゲームオブジェクトが選択されていません。");
            ImGui::TextDisabled(u8"シーン / UI ワークスペースで対象をクリックしてから、もう一度押してください。");
        }
        else if (!object_editor_context.CanEdit())
        {
            ImGui::TextDisabled(u8"いまは編集できません（実行中などの理由）。");
        }
        else
        {
            ImGui::Text(u8"対象: %s", object->Name().c_str());
            ImGui::SetNextItemWidth(260.0f);
            ImGui::InputTextWithHint("##motion_property_filter", u8"絞り込み（例: rough）",
                motion_property_picker_filter.data(), motion_property_picker_filter.size());
            ImGui::Separator();

            const std::string filter = Lower(std::string(motion_property_picker_filter.data()));
            int shown = 0;

            // Component ごとにまとめて出す。どこに属する値かが分かるようにする。
            for (std::size_t i = 0; i < object->ComponentCount(); ++i)
            {
                Component* candidate = object->ComponentAt(i);
                if (candidate == nullptr || candidate->PendingDestroy()) continue;

                // 静的な登録ぶんと、Material のように実行時に決まるぶんの両方を見る。
                std::vector<const PropertyDesc*> animatable;
                for (const PropertyDesc& property :
                    PropertyRegistry::PropertiesOf(candidate->TypeID()))
                {
                    if (property.animatable == Animatable::None) continue;
                    animatable.push_back(&property);
                }
                if (const std::vector<PropertyDesc>* dynamic = candidate->DynamicProperties())
                {
                    for (const PropertyDesc& property : *dynamic)
                    {
                        if (property.animatable == Animatable::None) continue;
                        animatable.push_back(&property);
                    }
                }
                if (animatable.empty()) continue;

                const char* type_name = candidate->TypeName();
                bool header_drawn = false;
                for (const PropertyDesc* property : animatable)
                {
                    if (!filter.empty())
                    {
                        const std::string haystack =
                            Lower(std::string(property->name) + " " + property->DisplayName());
                        if (haystack.find(filter) == std::string::npos) continue;
                    }

                    if (!header_drawn)
                    {
                        ImGui::TextDisabled("%s", type_name);
                        header_drawn = true;
                    }

                    ImGui::PushID(property);
                    // 表示名だけだと内部名が分からないので両方出す。
                    // 補間できないものは「段階」と添えて、Ease が効かないことを示す。
                    const std::string label = std::string("  ") + property->DisplayName() +
                        "   (" + property->name + ")" +
                        (property->animatable == Animatable::Step ? u8"  ［段階］" : "");
                    if (ImGui::Selectable(label.c_str()))
                    {
                        add_motion_track(*object, *candidate, *property);
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::PopID();
                    ++shown;
                }
            }

            if (shown == 0)
            {
                ImGui::TextDisabled(filter.empty()
                    ? u8"動かせるプロパティがありません。"
                    : u8"絞り込みに一致するものがありません。");
            }
        }
        ImGui::EndPopup();
    }

    if (ImGui::Button(u8"不透明度フェードを作成") && object_editor_context.CanEdit())
    {
        ReplayEngine::Scene::Scene* scene = object_editor_context.GetScene();
        ReplayEngine::Core::GameObject* object = scene != nullptr
            ? object_editor_context.Selection().ResolvePrimary(*scene) : nullptr;
        Component* component = nullptr;
        const PropertyDesc* desc = nullptr;
        if (object != nullptr && FindUIImageOpacity(*object, component, desc))
        {
            if (!motion_editor_loaded && !project_create_motion("LogoFade"))
            {
                ImGui::End();
                return;
            }
            stop_motion_preview();
            motion_edit_history.Begin(motion_editor_asset, u8"不透明度フェードを作成");
            motion_editor_asset.duration = (std::max)(motion_editor_asset.duration, 1.0f);
            MotionTrack track;
            track.name = object->Name() + ".Opacity Fade";
            track.binding.object = object->ID();
            track.binding.component_type = component->TypeID();
            track.binding.component_index = ComponentTypeIndex(*object, component);
            track.binding.property = desc->name;
            track.value_type = PropertyType::Float;
            MotionKeyframe a;
            a.time = 0.0f;
            a.value = PropertyValue::MakeFloat(0.0f);
            a.easing = MotionEasing::EaseOutCubic;
            MotionKeyframe b;
            b.time = 1.0f;
            b.value = PropertyValue::MakeFloat(1.0f);
            b.easing = MotionEasing::EaseOutCubic;
            track.keys.push_back(a);
            track.keys.push_back(b);
            motion_editor_asset.tracks.push_back(std::move(track));
            motion_selected_track = static_cast<int>(motion_editor_asset.tracks.size()) - 1;
            motion_selected_key = -1;
            motion_selected_event_track = -1;
            motion_selected_event = -1;
            motion_edit_history.Commit(motion_editor_asset);
            motion_editor_dirty = true;
        }
    }
    ReplayEngine::Editor::EditorHelp::Item("button.motion.opacity_fade",
        u8"選択中の UI Image の不透明度を 0 から 1 へ変える Motion Track を作成します。");

    ImGui::Separator();
    ImGui::TextUnformatted(motion_editor_status.c_str());
    if (motion_editor_loaded)
    {
        ImGui::Text(u8"アセット: %s%s", motion_editor_asset.name.c_str(),
            motion_editor_dirty ? " *" : "");
        ReplayEngine::Editor::ReorderRequest track_move{};
        const bool can_edit_motion = object_editor_context.CanEdit();
        for (std::size_t i = 0; i < motion_editor_asset.tracks.size(); ++i)
        {
            MotionTrack& track = motion_editor_asset.tracks[i];
            const std::string item_id = "MotionTrack" + std::to_string(i);
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
            if (item.request.Valid() && !track_move.Valid())
                track_move = item.request;
        }
        if (track_move.Valid())
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
            motion_selected_keys.clear();
            motion_selected_event_track = -1;
            motion_selected_event = -1;
            motion_edit_history.Commit(motion_editor_asset);
            motion_editor_dirty = true;
        }
        if (const char* active_label = ReplayEngine::Editor::ActiveReorderLabel();
            active_label != nullptr)
        {
            ImGui::TextColored(ImGui::GetStyle().Colors[ImGuiCol_DragDropTarget],
                u8"移動中: %s", active_label);
        }
    }
    else if (motion_composition_loaded)
    {
        using ReplayEngine::Assets::AssetKind;
        using ReplayEngine::Motion::CompositionMarker;
        using ReplayEngine::Motion::CompositionMotionLayer;

        ImGui::Text(u8"コンポジション: %s%s", motion_editor_composition.name.c_str(),
            motion_editor_dirty ? " *" : "");
        float duration = motion_editor_composition.duration;
        ImGui::SetNextItemWidth(120.0f);
        if (ImGui::DragFloat(u8"長さ", &duration, 0.033333f, 0.0f, 3600.0f, "%.3fs"))
        {
            motion_editor_composition.duration = (std::max)(0.0f, duration);
            motion_preview_time = (std::min)(motion_preview_time,
                motion_editor_composition.duration);
            motion_editor_dirty = true;
        }

        if (ImGui::Button(u8"+ モーションレイヤー"))
        {
            composition_edit_history.Begin(motion_editor_composition, u8"コンポジションレイヤーを追加");
            CompositionMotionLayer layer;
            layer.name = "Motion Layer " + std::to_string(motion_editor_composition.layers.size() + 1);
            layer.in_time = 0.0f;
            layer.out_time = motion_editor_composition.duration;
            motion_editor_composition.layers.push_back(std::move(layer));
            composition_edit_history.Commit(motion_editor_composition);
            motion_editor_dirty = true;
        }
        ReplayEngine::Editor::EditorHelp::Item("button.motion.add_layer",
            u8"コンポジションへ Motion Asset を再生するレイヤーを追加します。");
        ImGui::SameLine();
        if (ImGui::Button(u8"+ プリコンポーズレイヤー"))
        {
            composition_edit_history.Begin(motion_editor_composition, u8"プリコンポーズレイヤーを追加");
            CompositionMotionLayer layer;
            layer.name = "Precomp " + std::to_string(motion_editor_composition.layers.size() + 1);
            layer.in_time = 0.0f;
            layer.out_time = motion_editor_composition.duration;
            motion_editor_composition.layers.push_back(std::move(layer));
            composition_edit_history.Commit(motion_editor_composition);
            motion_editor_dirty = true;
        }
        ReplayEngine::Editor::EditorHelp::Item("button.motion.add_precomp",
            u8"コンポジションを入れ子にして再生するプリコンポーズレイヤーを追加します。");
        ImGui::SameLine();
        if (ImGui::Button(u8"+ マーカー"))
        {
            CompositionMarker marker;
            marker.name = "Marker " + std::to_string(motion_editor_composition.markers.size() + 1);
            marker.time = motion_preview_time;
            motion_editor_composition.markers.push_back(std::move(marker));
            motion_editor_dirty = true;
        }
        ReplayEngine::Editor::EditorHelp::Item("button.motion.add_marker",
            u8"現在の再生位置へコンポジション用マーカーを追加します。");

        ImGui::Separator();
        ImGui::TextUnformatted(u8"レイヤー");
        ReplayEngine::Editor::ReorderRequest layer_move{};
        bool removed_layer = false;
        const bool can_edit_composition = object_editor_context.CanEdit();
        for (std::size_t i = 0; i < motion_editor_composition.layers.size(); )
        {
            CompositionMotionLayer& layer = motion_editor_composition.layers[i];
            const std::string item_id = "CompositionLayer" + std::to_string(i);
            bool remove_layer = false;
            const ReplayEngine::Editor::ReorderableItemResult item =
                ReplayEngine::Editor::DrawReorderableItem(
                    &motion_editor_composition.layers, item_id.c_str(), i,
                    motion_editor_composition.layers.size(), layer.name.c_str(),
                    false, true, can_edit_composition,
                    [&remove_layer, can_edit_composition]
                    {
                        if (ImGui::MenuItem(u8"削除", nullptr, false, can_edit_composition))
                            remove_layer = true;
                        ReplayEngine::Editor::EditorHelp::Item("button.motion.remove_layer",
                            u8"選択中のコンポジションレイヤーを削除します。");
                    });
            if (item.request.Valid() && !layer_move.Valid()) layer_move = item.request;

            if (item.opened)
            {
                ImGui::PushID(item_id.c_str());
                ImGui::Checkbox("##enabled", &layer.enabled);
                ImGui::SameLine();
                char name_buffer[128]{};
                strncpy_s(name_buffer, sizeof(name_buffer), layer.name.c_str(), _TRUNCATE);
                ImGui::SetNextItemWidth(180.0f);
                if (ImGui::InputText("##name", name_buffer, IM_ARRAYSIZE(name_buffer)))
                {
                    layer.name = name_buffer;
                    motion_editor_dirty = true;
                }
                ImGui::SameLine();
                if (ImGui::SmallButton(u8"削除")) remove_layer = true;
                ReplayEngine::Editor::EditorHelp::Item("button.motion.remove_layer",
                    u8"選択中のコンポジションレイヤーを削除します。");

                const std::string guid = !layer.motion_guid.empty()
                    ? layer.motion_guid : layer.composition_guid;
                const ReplayEngine::Assets::AssetRecord* record = guid.empty()
                    ? nullptr : asset_database.FindByGuid(guid);
                const char* source_label = record != nullptr
                    ? record->display_name.c_str() : (guid.empty() ? u8"モーション / コンポジションをここへドロップ" : u8"アセットが見つかりません");
                ImGui::SetNextItemWidth(360.0f);
                ImGui::Button(source_label, ImVec2(360.0f, 0.0f));
                ReplayEngine::Editor::EditorHelp::Item("button.motion.asset_drop_target",
                    u8"Motion または Composition Asset をここへドロップしてレイヤーへ割り当てます。");
                if (ImGui::BeginDragDropTarget())
                {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("REPLAY_ASSET_GUID"))
                    {
                        const char* dropped_guid = static_cast<const char*>(payload->Data);
                        const ReplayEngine::Assets::AssetRecord* dropped =
                            asset_database.FindByGuid(dropped_guid != nullptr ? dropped_guid : "");
                        if (dropped != nullptr && dropped->kind == AssetKind::Motion)
                        {
                            layer.motion_guid = dropped->guid;
                            layer.composition_guid.clear();
                            motion_editor_dirty = true;
                        }
                        else if (dropped != nullptr && dropped->kind == AssetKind::Composition)
                        {
                            // 自分自身をPrecompへ入れると循環するのでEditor側でも防ぐ。
                            if (dropped->guid != motion_editor_guid)
                            {
                                layer.composition_guid = dropped->guid;
                                layer.motion_guid.clear();
                                motion_editor_dirty = true;
                            }
                        }
                    }
                    ImGui::EndDragDropTarget();
                }
                ImGui::SameLine();
                if (ImGui::SmallButton(u8"クリア"))
                {
                    layer.motion_guid.clear();
                    layer.composition_guid.clear();
                    motion_editor_dirty = true;
                }
                ReplayEngine::Editor::EditorHelp::Item("button.motion.clear_layer_asset",
                    u8"レイヤーに割り当てた Motion または Composition Asset を外します。");

                float timing[3]{ layer.start_offset, layer.in_time, layer.out_time };
                ImGui::SetNextItemWidth(360.0f);
                if (ImGui::DragFloat3(u8"開始 / 入点 / 出点", timing, 0.01f, -3600.0f, 3600.0f, "%.3f"))
                {
                    layer.start_offset = timing[0];
                    layer.in_time = (std::max)(0.0f, timing[1]);
                    layer.out_time = timing[2] < 0.0f ? -1.0f : (std::max)(layer.in_time, timing[2]);
                    motion_editor_dirty = true;
                }
                float playback[2]{ layer.time_scale, layer.weight };
                ImGui::SetNextItemWidth(260.0f);
                if (ImGui::DragFloat2(u8"時間倍率 / ウェイト", playback, 0.01f, -16.0f, 16.0f, "%.3f"))
                {
                    layer.time_scale = playback[0] == 0.0f ? 0.0001f : playback[0];
                    layer.weight = (std::max)(0.0f, playback[1]);
                    motion_editor_dirty = true;
                }
                ImGui::Separator();
                ImGui::PopID();
            }

            if (remove_layer)
            {
                composition_edit_history.Begin(motion_editor_composition, u8"コンポジションレイヤーを削除");
                motion_editor_composition.layers.erase(
                    motion_editor_composition.layers.begin() +
                    static_cast<std::ptrdiff_t>(i));
                composition_edit_history.Commit(motion_editor_composition);
                motion_editor_dirty = true;
                removed_layer = true;
                continue;
            }
            ++i;
        }

        if (!removed_layer && layer_move.Valid())
        {
            composition_edit_history.Begin(motion_editor_composition,
                u8"コンポジションレイヤーの順序を変更");
            CompositionMotionLayer moved = std::move(
                motion_editor_composition.layers[layer_move.source]);
            motion_editor_composition.layers.erase(
                motion_editor_composition.layers.begin() +
                static_cast<std::ptrdiff_t>(layer_move.source));
            motion_editor_composition.layers.insert(
                motion_editor_composition.layers.begin() +
                static_cast<std::ptrdiff_t>(layer_move.destination), std::move(moved));
            composition_edit_history.Commit(motion_editor_composition);
            motion_editor_dirty = true;
        }
        if (const char* active_label = ReplayEngine::Editor::ActiveReorderLabel();
            active_label != nullptr)
        {
            ImGui::TextColored(ImGui::GetStyle().Colors[ImGuiCol_DragDropTarget],
                u8"移動中: %s", active_label);
        }

        ImGui::Separator();
        ImGui::TextUnformatted(u8"マーカー");
        for (int i = 0; i < static_cast<int>(motion_editor_composition.markers.size()); ++i)
        {
            CompositionMarker& marker = motion_editor_composition.markers[i];
            ImGui::PushID(100000 + i);
            char marker_name[96]{};
            strncpy_s(marker_name, sizeof(marker_name), marker.name.c_str(), _TRUNCATE);
            ImGui::SetNextItemWidth(180.0f);
            if (ImGui::InputText("##markerName", marker_name, IM_ARRAYSIZE(marker_name)))
            {
                marker.name = marker_name;
                motion_editor_dirty = true;
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(110.0f);
            if (ImGui::DragFloat("##markerTime", &marker.time, 0.01f, 0.0f,
                motion_editor_composition.duration, "%.3fs"))
            {
                marker.time = (std::max)(0.0f,
                    (std::min)(motion_editor_composition.duration, marker.time));
                motion_editor_dirty = true;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("X"))
            {
                motion_editor_composition.markers.erase(
                    motion_editor_composition.markers.begin() + i);
                motion_editor_dirty = true;
                ImGui::PopID();
                --i;
                continue;
            }
            ReplayEngine::Editor::EditorHelp::Item("button.motion.remove_marker",
                u8"選択中のコンポジションマーカーを削除します。");
            ImGui::PopID();
        }
    }
    else
    {
        ImGui::TextDisabled(u8"プロジェクトブラウザーで .replaymotion を開くか、新規作成してください。");
    }

    ImGui::End();
}

// Motion リグ。骨を選んでポーズを付ける。保存はしないので実行中だけ持つ。
void framework::draw_motion_rig()
{
    if (!show_motion_rig_panel) return;
    ReplayEngine::Editor::PanelTabColorScope panel_tab_color("Motion");
    if (!ImGui::Begin(u8"Motion リグ", &show_motion_rig_panel))
    {
        ImGui::End();
        return;
    }

    ImGui::Checkbox(u8"シーンビューへ描く", &show_rig_debug_draw);
    ReplayEngine::Editor::EditorHelp::Item("rig.draw",
        u8"骨を持つモデルのリグを Scene View へ重ねて描きます。");

    if (ImGui::CollapsingHeader(u8"見た目"))
    {
        ImGui::SliderFloat(u8"線の太さ", &rig_bone_thickness, 0.5f, 6.0f, "%.1f");
        ImGui::SliderFloat(u8"関節の大きさ", &rig_joint_radius, 0.5f, 10.0f, "%.1f");
        ImGui::SliderFloat(u8"選択中の倍率", &rig_picked_scale, 1.0f, 4.0f, "x%.1f");
        ImGui::SliderInt(u8"表示する深さ", &rig_max_depth, 0, 32);
        ImGui::SameLine();
        ImGui::TextDisabled(u8"0 で制限なし");
        ImGui::Checkbox(u8"名前を出す", &rig_show_names);
        ImGui::Checkbox(u8"ギズモをローカル軸で出す", &rig_gizmo_use_local);
        ImGui::ColorEdit4(u8"骨の色", &rig_bone_tint.x,
            ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_NoInputs);
        ImGui::ColorEdit4(u8"選択中の色", &rig_picked_tint.x,
            ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_NoInputs);
    }

    ImGui::Separator();
    const ReplayEngine::Core::GameObject* target =
        active_object_scene().FindGameObjectByID(object_editor_context.Selection().Primary());
    if (target == nullptr || target->PendingDestroy())
    {
        ImGui::TextDisabled(u8"骨を持つ GameObject を階層で選んでください。");
        ImGui::End();
        return;
    }
    std::uint64_t owner = target->ID().Value();
    auto found = object_rig_debug_bones.find(owner);
    if (found == object_rig_debug_bones.end() || found->second.empty())
    {
        // 階層で選び直さなくてよいよう、骨を持つものをここから選べるようにする。
        ImGui::TextDisabled(u8"選択中の GameObject に骨がありません。");
        bool any = false;
        for (const auto& candidate : object_rig_debug_bones)
        {
            if (candidate.second.empty()) continue;
            const ReplayEngine::Core::GameObject* object =
                active_object_scene().FindGameObjectByID(
                    ReplayEngine::Core::ObjectID{ candidate.first });
            const std::string label = (object != nullptr ? object->Name() : std::string("?")) +
                "  (" + std::to_string(candidate.second.size()) + u8" 本)";
            if (ImGui::Button(label.c_str()))
                object_editor_context.Selection().Select(
                    ReplayEngine::Core::ObjectID{ candidate.first });
            any = true;
        }
        if (!any)
            ImGui::TextDisabled(u8"骨のあるモデルが見つかりません。");
        ImGui::End();
        return;
    }
    const std::vector<rig_debug_bone>& bones = found->second;
    auto& pose = object_rig_pose[owner];
    ImGui::Text(u8"%s  骨 %zu 本 / ポーズ %zu 本",
        target->Name().c_str(), bones.size(), pose.size());

    static std::array<char, 64> rig_filter{};
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##MotionRigSearch", u8"骨を検索...",
        rig_filter.data(), rig_filter.size());
    if (ImGui::Button(u8"すべて戻す")) pose.clear();
    ReplayEngine::Editor::EditorHelp::Item("button.rig.reset_all",
        u8"この GameObject に付けたポーズをすべて捨てます。");

    if (ImGui::BeginChild("MotionRigBones", ImVec2(0.0f, 200.0f), true))
    {
        for (const rig_debug_bone& bone : bones)
        {
            if (rig_filter[0] != '\0' &&
                bone.name.find(rig_filter.data()) == std::string::npos)
                continue;
            std::string label = bone.name;
            if (pose.find(bone.name) != pose.end()) label += u8"  ●";
            if (ImGui::Selectable(label.c_str(), rig_selected_bone == bone.name))
                rig_selected_bone = bone.name;
        }
    }
    ImGui::EndChild();

    if (rig_selected_bone.empty())
    {
        ImGui::TextDisabled(u8"骨を選ぶと Move / Rotate / Scale を編集できます。");
        ImGui::End();
        return;
    }
    ImGui::Separator();
    ImGui::Text(u8"選択中: %s", rig_selected_bone.c_str());
    rig_pose_override& entry = pose[rig_selected_bone];
    ImGui::DragFloat3(u8"移動", &entry.translation.x, 0.01f);
    ImGui::DragFloat3(u8"回転", &entry.rotation.x, 0.5f, -360.0f, 360.0f, "%.1f");
    ImGui::DragFloat3(u8"拡縮", &entry.scale.x, 0.01f, 0.01f, 10.0f);
    if (ImGui::Button(u8"この骨を戻す")) pose.erase(rig_selected_bone);
    ImGui::End();
}
