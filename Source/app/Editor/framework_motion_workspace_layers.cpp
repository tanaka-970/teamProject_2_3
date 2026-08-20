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

// Motion Layer 編集の関数本体

void framework::draw_motion_layers()
{
    if (!show_motion_layers_panel) return;
    if (!ImGui::Begin("Motion レイヤー", &show_motion_layers_panel))
    {
        ImGui::End();
        return;
    }

    if (ImGui::Button("新規"))
    {
        project_create_motion("NewMotion");
    }
    ImGui::SameLine();
    if (ImGui::Button("保存")) save_current_motion_asset();
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
        motion_edit_history.Begin(motion_editor_asset, "Trackを追加");
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

    if (ImGui::Button("選択からTrack追加"))
    {
        motion_property_picker_filter.fill('\0');
        ImGui::OpenPopup("MotionPropertyPicker");
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip(
            u8"選択中の GameObject が持つ、動かせるプロパティの一覧から選びます。\n"
            u8"先に Scene / UI Workspace で対象を選択しておいてください。");
    }

    if (ImGui::BeginPopup("MotionPropertyPicker"))
    {
        ReplayEngine::Scene::Scene* scene = object_editor_context.GetScene();
        ReplayEngine::Core::GameObject* object = scene != nullptr
            ? object_editor_context.Selection().ResolvePrimary(*scene) : nullptr;

        if (object == nullptr)
        {
            ImGui::TextDisabled(u8"GameObject が選択されていません。");
            ImGui::TextDisabled(u8"Scene / UI Workspace で対象をクリックしてから、もう一度押してください。");
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

    if (ImGui::Button("Opacity Fadeを作成") && object_editor_context.CanEdit())
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
            motion_edit_history.Begin(motion_editor_asset, "Opacity Fadeを作成");
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

    ImGui::Separator();
    ImGui::TextUnformatted(motion_editor_status.c_str());
    if (motion_editor_loaded)
    {
        ImGui::Text("Asset: %s%s", motion_editor_asset.name.c_str(),
            motion_editor_dirty ? " *" : "");
        for (int i = 0; i < static_cast<int>(motion_editor_asset.tracks.size()); ++i)
        {
            MotionTrack& track = motion_editor_asset.tracks[i];
            ImGui::PushID(i);
            if (ImGui::Selectable(track.name.c_str(), motion_selected_track == i))
            {
                motion_selected_track = i;
                motion_selected_key = -1;
            }
            ImGui::SameLine();
            ImGui::TextDisabled("%s", PropertyTypeLabel(track.value_type));
            ImGui::PopID();
        }
    }
    else if (motion_composition_loaded)
    {
        using ReplayEngine::Assets::AssetKind;
        using ReplayEngine::Motion::CompositionMarker;
        using ReplayEngine::Motion::CompositionMotionLayer;

        ImGui::Text("Composition: %s%s", motion_editor_composition.name.c_str(),
            motion_editor_dirty ? " *" : "");
        float duration = motion_editor_composition.duration;
        ImGui::SetNextItemWidth(120.0f);
        if (ImGui::DragFloat("Duration", &duration, 0.033333f, 0.0f, 3600.0f, "%.3fs"))
        {
            motion_editor_composition.duration = (std::max)(0.0f, duration);
            motion_preview_time = (std::min)(motion_preview_time,
                motion_editor_composition.duration);
            motion_editor_dirty = true;
        }

        if (ImGui::Button("+ Motion Layer"))
        {
            CompositionMotionLayer layer;
            layer.name = "Motion Layer " + std::to_string(motion_editor_composition.layers.size() + 1);
            layer.in_time = 0.0f;
            layer.out_time = motion_editor_composition.duration;
            motion_editor_composition.layers.push_back(std::move(layer));
            motion_editor_dirty = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("+ Precomp Layer"))
        {
            CompositionMotionLayer layer;
            layer.name = "Precomp " + std::to_string(motion_editor_composition.layers.size() + 1);
            layer.in_time = 0.0f;
            layer.out_time = motion_editor_composition.duration;
            motion_editor_composition.layers.push_back(std::move(layer));
            motion_editor_dirty = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("+ Marker"))
        {
            CompositionMarker marker;
            marker.name = "Marker " + std::to_string(motion_editor_composition.markers.size() + 1);
            marker.time = motion_preview_time;
            motion_editor_composition.markers.push_back(std::move(marker));
            motion_editor_dirty = true;
        }

        ImGui::Separator();
        ImGui::TextUnformatted("Layers");
        for (int i = 0; i < static_cast<int>(motion_editor_composition.layers.size()); ++i)
        {
            CompositionMotionLayer& layer = motion_editor_composition.layers[i];
            ImGui::PushID(i);
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
            if (ImGui::SmallButton("Delete"))
            {
                motion_editor_composition.layers.erase(motion_editor_composition.layers.begin() + i);
                motion_editor_dirty = true;
                ImGui::PopID();
                --i;
                continue;
            }

            const std::string guid = !layer.motion_guid.empty()
                ? layer.motion_guid : layer.composition_guid;
            const ReplayEngine::Assets::AssetRecord* record = guid.empty()
                ? nullptr : asset_database.FindByGuid(guid);
            const char* source_label = record != nullptr
                ? record->display_name.c_str() : (guid.empty() ? "Drop Motion / Composition here" : "Missing Asset");
            ImGui::SetNextItemWidth(360.0f);
            ImGui::Button(source_label, ImVec2(360.0f, 0.0f));
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
            if (ImGui::SmallButton("Clear"))
            {
                layer.motion_guid.clear();
                layer.composition_guid.clear();
                motion_editor_dirty = true;
            }

            float timing[3]{ layer.start_offset, layer.in_time, layer.out_time };
            ImGui::SetNextItemWidth(360.0f);
            if (ImGui::DragFloat3("Start / In / Out", timing, 0.01f, -3600.0f, 3600.0f, "%.3f"))
            {
                layer.start_offset = timing[0];
                layer.in_time = (std::max)(0.0f, timing[1]);
                layer.out_time = timing[2] < 0.0f ? -1.0f : (std::max)(layer.in_time, timing[2]);
                motion_editor_dirty = true;
            }
            float playback[2]{ layer.time_scale, layer.weight };
            ImGui::SetNextItemWidth(260.0f);
            if (ImGui::DragFloat2("Time Scale / Weight", playback, 0.01f, -16.0f, 16.0f, "%.3f"))
            {
                layer.time_scale = playback[0] == 0.0f ? 0.0001f : playback[0];
                layer.weight = (std::max)(0.0f, playback[1]);
                motion_editor_dirty = true;
            }
            ImGui::Separator();
            ImGui::PopID();
        }

        ImGui::Separator();
        ImGui::TextUnformatted("Markers");
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
            ImGui::PopID();
        }
    }
    else
    {
        ImGui::TextDisabled("Project Browserで .replaymotion を開くか、新規作成してください。");
    }

    ImGui::End();
}
