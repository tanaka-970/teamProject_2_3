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

namespace
{
    using ReplayEngine::Core::Component;
    using ReplayEngine::Core::ComponentTypeID;
    using ReplayEngine::Motion::MotionAsset;
    using ReplayEngine::Motion::MotionBinding;
    using ReplayEngine::Motion::MotionEasing;
    using ReplayEngine::Motion::MotionEvaluator;
    using ReplayEngine::Motion::MotionKeyframe;
    using ReplayEngine::Motion::MotionTrack;
    using ReplayEngine::Reflection::Animatable;
    using ReplayEngine::Reflection::PropertyDesc;
    using ReplayEngine::Reflection::PropertyRegistry;
    using ReplayEngine::Reflection::PropertyType;
    using ReplayEngine::Reflection::PropertyValue;

    std::string Lower(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return value;
    }

    const char* PropertyTypeLabel(PropertyType type) noexcept
    {
        switch (type)
        {
        case PropertyType::Bool: return "Bool";
        case PropertyType::Int: return "Int";
        case PropertyType::Float: return "Float";
        case PropertyType::Double: return "Double";
        case PropertyType::String: return "String";
        case PropertyType::Vector2: return "Vector2";
        case PropertyType::Vector3: return "Vector3";
        case PropertyType::Vector4: return "Vector4";
        case PropertyType::Quaternion: return "Quaternion";
        case PropertyType::Color: return "Color";
        case PropertyType::Enum: return "Enum";
        case PropertyType::AssetReference: return "Asset";
        default: return "Other";
        }
    }

    PropertyValue DefaultValueFor(PropertyType type)
    {
        switch (type)
        {
        case PropertyType::Bool: return PropertyValue::MakeBool(false);
        case PropertyType::Int:
        case PropertyType::Enum: return PropertyValue::MakeInt(0);
        case PropertyType::Float: return PropertyValue::MakeFloat(0.0f);
        case PropertyType::Double: return PropertyValue::MakeDouble(0.0);
        case PropertyType::String: return PropertyValue::MakeString(std::string());
        case PropertyType::Vector2:
            return PropertyValue::MakeVector2(DirectX::XMFLOAT2{ 0.0f, 0.0f });
        case PropertyType::Vector3:
            return PropertyValue::MakeVector3(DirectX::XMFLOAT3{ 0.0f, 0.0f, 0.0f });
        case PropertyType::Vector4:
            return PropertyValue::MakeVector4(
                DirectX::XMFLOAT4{ 0.0f, 0.0f, 0.0f, 0.0f });
        case PropertyType::Quaternion:
            return PropertyValue::MakeQuaternion(
                DirectX::XMFLOAT4{ 0.0f, 0.0f, 0.0f, 1.0f });
        case PropertyType::Color:
            return PropertyValue::MakeColor(
                DirectX::XMFLOAT4{ 1.0f, 1.0f, 1.0f, 1.0f });
        case PropertyType::AssetReference:
            return PropertyValue::MakeAssetReference(std::string());
        default:
            return PropertyValue{};
        }
    }

    bool DrawValueEditor(const char* label, PropertyValue& value, PropertyType type)
    {
        bool changed = false;
        switch (type)
        {
        case PropertyType::Bool:
        {
            bool v = value.AsBool();
            if (ImGui::Checkbox(label, &v))
            {
                value = PropertyValue::MakeBool(v);
                changed = true;
            }
            break;
        }
        case PropertyType::Int:
        case PropertyType::Enum:
        {
            int v = value.AsInt();
            if (ImGui::DragInt(label, &v, 1.0f))
            {
                value = type == PropertyType::Enum
                    ? PropertyValue::MakeEnum(v) : PropertyValue::MakeInt(v);
                changed = true;
            }
            break;
        }
        case PropertyType::Float:
        {
            float v = value.AsFloat();
            if (ImGui::DragFloat(label, &v, 0.01f))
            {
                value = PropertyValue::MakeFloat(v);
                changed = true;
            }
            break;
        }
        case PropertyType::Double:
        {
            float v = static_cast<float>(value.AsDouble());
            if (ImGui::DragFloat(label, &v, 0.01f))
            {
                value = PropertyValue::MakeDouble(static_cast<double>(v));
                changed = true;
            }
            break;
        }
        case PropertyType::Vector2:
        {
            DirectX::XMFLOAT2 v = value.AsVector2();
            float raw[2]{ v.x, v.y };
            if (ImGui::DragFloat2(label, raw, 0.01f))
            {
                value = PropertyValue::MakeVector2({ raw[0], raw[1] });
                changed = true;
            }
            break;
        }
        case PropertyType::Vector3:
        {
            DirectX::XMFLOAT3 v = value.AsVector3();
            float raw[3]{ v.x, v.y, v.z };
            if (ImGui::DragFloat3(label, raw, 0.01f))
            {
                value = PropertyValue::MakeVector3({ raw[0], raw[1], raw[2] });
                changed = true;
            }
            break;
        }
        case PropertyType::Vector4:
        case PropertyType::Quaternion:
        {
            DirectX::XMFLOAT4 v = value.AsVector4();
            float raw[4]{ v.x, v.y, v.z, v.w };
            if (ImGui::DragFloat4(label, raw, 0.01f))
            {
                value = type == PropertyType::Quaternion
                    ? PropertyValue::MakeQuaternion({ raw[0], raw[1], raw[2], raw[3] })
                    : PropertyValue::MakeVector4({ raw[0], raw[1], raw[2], raw[3] });
                changed = true;
            }
            break;
        }
        case PropertyType::Color:
        {
            DirectX::XMFLOAT4 v = value.AsVector4();
            float raw[4]{ v.x, v.y, v.z, v.w };
            if (ImGui::ColorEdit4(label, raw))
            {
                value = PropertyValue::MakeColor({ raw[0], raw[1], raw[2], raw[3] });
                changed = true;
            }
            break;
        }
        case PropertyType::String:
        case PropertyType::AssetReference:
        {
            char buffer[512]{};
            const std::string& source = value.AsString();
            strncpy_s(buffer, source.c_str(), _TRUNCATE);
            if (ImGui::InputText(label, buffer, IM_ARRAYSIZE(buffer)))
            {
                value = type == PropertyType::AssetReference
                    ? PropertyValue::MakeAssetReference(buffer)
                    : PropertyValue::MakeString(buffer);
                changed = true;
            }
            break;
        }
        default:
            ImGui::TextDisabled("%s: 未対応型", label);
            break;
        }
        return changed;
    }

    bool DrawEasingCombo(const char* label, MotionEasing& easing)
    {
        constexpr MotionEasing easings[] = {
            MotionEasing::Linear,
            MotionEasing::Step,
            MotionEasing::EaseInQuad,
            MotionEasing::EaseOutQuad,
            MotionEasing::EaseInOutQuad,
            MotionEasing::EaseInCubic,
            MotionEasing::EaseOutCubic,
            MotionEasing::EaseInOutCubic,
            MotionEasing::EaseInBack,
            MotionEasing::EaseOutBack,
            MotionEasing::EaseInOutBack,
            MotionEasing::EaseInElastic,
            MotionEasing::EaseOutElastic,
            MotionEasing::EaseInOutElastic,
            MotionEasing::CustomBezier,
        };

        bool changed = false;
        const char* preview = ReplayEngine::Motion::ToString(easing);
        if (ImGui::BeginCombo(label, preview))
        {
            for (MotionEasing candidate : easings)
            {
                const bool selected = candidate == easing;
                if (ImGui::Selectable(ReplayEngine::Motion::ToString(candidate),
                    selected))
                {
                    easing = candidate;
                    changed = true;
                }
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        return changed;
    }

    Component* ResolveBindingComponent(ReplayEngine::Scene::Scene& scene,
        const MotionBinding& binding)
    {
        return ReplayEngine::Motion::MotionBindingResolver::Resolve(scene, binding).component;
    }

    int ComponentTypeIndex(const ReplayEngine::Core::GameObject& object,
        Component* target)
    {
        if (target == nullptr) return 0;
        int index = 0;
        for (std::size_t i = 0; i < object.ComponentCount(); ++i)
        {
            Component* component = object.ComponentAt(i);
            if (component == nullptr || component->PendingDestroy()) continue;
            if (component->TypeID() != target->TypeID()) continue;
            if (component == target) return index;
            ++index;
        }
        return 0;
    }

    bool FindFirstAnimatable(ReplayEngine::Core::GameObject& object,
        Component*& component, const PropertyDesc*& desc)
    {
        for (std::size_t i = 0; i < object.ComponentCount(); ++i)
        {
            Component* candidate = object.ComponentAt(i);
            if (candidate == nullptr || candidate->PendingDestroy()) continue;

            const std::vector<PropertyDesc>& properties =
                PropertyRegistry::PropertiesOf(candidate->TypeID());
            for (const PropertyDesc& property : properties)
            {
                if (property.animatable == Animatable::None) continue;
                component = candidate;
                desc = &property;
                return true;
            }
        }
        return false;
    }

    bool FindUIImageOpacity(ReplayEngine::Core::GameObject& object,
        Component*& component, const PropertyDesc*& desc)
    {
        component = object.GetComponent<ReplayEngine::Components::UIImageComponent>();
        if (component == nullptr) return false;
        desc = PropertyRegistry::Find(component->TypeID(), "opacity");
        return desc != nullptr && desc->animatable != Animatable::None;
    }
}

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
        motion_mixer.Contribute(binding, value, 1.0f);
    }
    motion_mixer.Apply();
}

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
    if (ImGui::Button("選択からTrack追加") && object_editor_context.CanEdit())
    {
        ReplayEngine::Scene::Scene* scene = object_editor_context.GetScene();
        ReplayEngine::Core::GameObject* object = scene != nullptr
            ? object_editor_context.Selection().ResolvePrimary(*scene) : nullptr;
        Component* component = nullptr;
        const PropertyDesc* desc = nullptr;
        if (object != nullptr && FindFirstAnimatable(*object, component, desc))
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
            track.name = object->Name() + "." + desc->DisplayName();
            track.binding.object = object->ID();
            track.binding.component_type = component->TypeID();
            track.binding.component_index = ComponentTypeIndex(*object, component);
            track.binding.property = desc->name;
            track.value_type = desc->type;
            MotionKeyframe key0;
            key0.time = 0.0f;
            key0.value = desc->Capture(*component);
            MotionKeyframe key1 = key0;
            key1.time = (std::max)(0.1f, motion_editor_asset.duration);
            track.keys.push_back(key0);
            track.keys.push_back(key1);
            motion_editor_asset.tracks.push_back(std::move(track));
            motion_selected_track = static_cast<int>(motion_editor_asset.tracks.size()) - 1;
            motion_selected_key = -1;
            motion_edit_history.Commit(motion_editor_asset);
            motion_editor_dirty = true;
        }
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
        ImGui::Text("Composition: %s", motion_editor_composition.name.c_str());
        for (const auto& layer : motion_editor_composition.layers)
            ImGui::TextDisabled("Layer %s %.2f", layer.motion_guid.c_str(),
                layer.start_offset);
    }
    else
    {
        ImGui::TextDisabled("Project Browserで .replaymotion を開くか、新規作成してください。");
    }

    ImGui::End();
}

void framework::draw_motion_preview()
{
    if (!show_motion_preview_panel) return;
    if (!ImGui::Begin("Motion プレビュー", &show_motion_preview_panel))
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

    if (ImGui::Button(motion_preview_active ? "停止" : "再生"))
    {
        if (motion_preview_active) stop_motion_preview();
        else
        {
            capture_motion_preview_targets();
            motion_preview_active = true;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("復元")) stop_motion_preview();
    ImGui::SameLine();
    ImGui::Checkbox("Loop", &motion_preview_loop);
    ImGui::SetNextItemWidth(140.0f);
    ImGui::DragFloat("Speed", &motion_preview_speed, 0.01f, -8.0f, 8.0f);

    if (motion_preview_active)
    {
        motion_preview_time += ImGui::GetIO().DeltaTime * motion_preview_speed;
        if (motion_editor_asset.duration > 0.0f)
        {
            if (motion_preview_loop)
            {
                while (motion_preview_time > motion_editor_asset.duration)
                    motion_preview_time -= motion_editor_asset.duration;
                while (motion_preview_time < 0.0f)
                    motion_preview_time += motion_editor_asset.duration;
            }
            else
            {
                motion_preview_time =
                    (std::min)((std::max)(motion_preview_time, 0.0f),
                        motion_editor_asset.duration);
            }
        }
        apply_motion_preview_time();
    }

    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::SliderFloat("Time", &motion_preview_time, 0.0f,
        (std::max)(0.001f, motion_editor_asset.duration)))
    {
        apply_motion_preview_time();
    }

    ImGui::Separator();
    ImGui::Text("Tracks: %d", static_cast<int>(motion_editor_asset.tracks.size()));
    ImGui::TextDisabled("PreviewはPropertyRegistry::Capture/Applyで復元します。");
    ImGui::End();
}

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

    if (motion_selected_track < 0 ||
        motion_selected_track >= static_cast<int>(motion_editor_asset.tracks.size()))
    {
        ImGui::Separator();
        ImGui::TextDisabled("Trackを選択してください。");
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
            ? PropertyRegistry::Find(bound_component->TypeID(), track.binding.property)
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
            ImGui::SetCursorScreenPos(ImVec2(origin.x + width * t, origin.y - 6.0f));
            ImGui::PushID(key_index);
            if (ImGui::SmallButton(motion_selected_track == track_index &&
                motion_selected_key == key_index ? "◆" : "◇"))
            {
                motion_selected_track = track_index;
                motion_selected_key = key_index;
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
