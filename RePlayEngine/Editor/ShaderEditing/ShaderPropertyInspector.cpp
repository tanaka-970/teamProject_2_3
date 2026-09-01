#include "ShaderPropertyInspector.h"

#include "../../Rendering/Materials/MaterialSchema.h"
#include "imgui/imgui.h"

#include <algorithm>
#include <string>
#include <vector>

namespace ReplayEngine::Editor
{
    namespace
    {
        const Assets::AssetRecord* ImageAsset(const Assets::AssetDatabase& assets,
            const std::string& guid) noexcept
        {
            if (guid.empty()) return nullptr;
            const Assets::AssetRecord* record = assets.FindByGuid(guid);
            return record != nullptr && record->kind == Assets::AssetKind::Image
                ? record : nullptr;
        }

        void Tooltip(const Rendering::ShaderProperty& property)
        {
            if (!property.tooltip.empty() && ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", property.tooltip.c_str());
        }

        bool DrawTexture(const Rendering::ShaderProperty& property,
            Reflection::PropertyBag& properties,
            const Assets::AssetDatabase& assets)
        {
            const std::string saved = property.SavedName();
            const Reflection::PropertyValue* stored = properties.Find(saved);
            const std::string guid = stored == nullptr ? std::string() : stored->AsString();
            const Assets::AssetRecord* current = ImageAsset(assets, guid);

            ImGui::TextUnformatted(property.DisplayName().c_str());
            Tooltip(property);
            std::string label;
            if (guid.empty())
                label = "None  (default: " +
                    (property.default_texture.empty() ? std::string("white") : property.default_texture) + ")";
            else if (current != nullptr)
                label = current->display_name.empty()
                    ? current->source_path.filename().u8string() : current->display_name;
            else
                label = "Missing Image  [" + guid + "]";

            bool changed = false;
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::Button(label.c_str(), ImVec2(-1.0f, 0.0f)))
                ImGui::OpenPopup("TextureAssetPicker");

            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("REPLAY_ASSET_GUID"))
                {
                    if (payload->Data != nullptr && payload->DataSize > 0)
                    {
                        const std::string dropped(static_cast<const char*>(payload->Data));
                        const Assets::AssetRecord* record = assets.FindByGuid(dropped);
                        if (record != nullptr && record->kind == Assets::AssetKind::Image)
                        {
                            properties.Set(saved,
                                Reflection::PropertyValue::MakeAssetReference(record->guid));
                            changed = true;
                        }
                    }
                }
                ImGui::EndDragDropTarget();
            }

            if (ImGui::BeginPopup("TextureAssetPicker"))
            {
                if (ImGui::Selectable("None / Shader Default", guid.empty()))
                {
                    properties.Set(saved,
                        Reflection::PropertyValue::MakeAssetReference(std::string()));
                    changed = true;
                }
                ImGui::Separator();
                std::vector<const Assets::AssetRecord*> images;
                for (const Assets::AssetRecord& record : assets.Records())
                    if (record.kind == Assets::AssetKind::Image &&
                        !assets.IsMissing(record.guid)) images.push_back(&record);
                std::sort(images.begin(), images.end(),
                    [](const Assets::AssetRecord* a, const Assets::AssetRecord* b)
                    {
                        const std::string an = a->display_name.empty()
                            ? a->source_path.filename().u8string() : a->display_name;
                        const std::string bn = b->display_name.empty()
                            ? b->source_path.filename().u8string() : b->display_name;
                        return an < bn;
                    });
                for (const Assets::AssetRecord* record : images)
                {
                    const std::string item = record->display_name.empty()
                        ? record->source_path.filename().u8string() : record->display_name;
                    if (ImGui::Selectable(item.c_str(), record->guid == guid))
                    {
                        properties.Set(saved,
                            Reflection::PropertyValue::MakeAssetReference(record->guid));
                        changed = true;
                    }
                }
                if (images.empty()) ImGui::TextDisabled("Image Asset がありません");
                ImGui::EndPopup();
            }
            return changed;
        }

        bool DrawOne(const Rendering::ShaderProperty& property,
            Reflection::PropertyBag& properties, const Assets::AssetDatabase& assets)
        {
            const std::string saved = property.SavedName();
            const Reflection::PropertyValue* stored = properties.Find(saved);
            if (stored == nullptr) return false;
            ImGui::PushID(saved.c_str());
            bool changed = false;
            using Rendering::ShaderPropertyKind;
            switch (property.kind)
            {
            case ShaderPropertyKind::Float:
            {
                float value = stored->AsFloat(property.default_value.x);
                if (ImGui::DragFloat(property.DisplayName().c_str(), &value, 0.01f))
                { properties.Set(saved, Reflection::PropertyValue::MakeFloat(value)); changed = true; }
                Tooltip(property); break;
            }
            case ShaderPropertyKind::Range:
            {
                float value = stored->AsFloat(property.default_value.x);
                if (ImGui::SliderFloat(property.DisplayName().c_str(), &value,
                    property.minimum, property.maximum))
                { properties.Set(saved, Reflection::PropertyValue::MakeFloat(value)); changed = true; }
                Tooltip(property); break;
            }
            case ShaderPropertyKind::Float2:
            {
                auto value = stored->AsVector2();
                if (ImGui::DragFloat2(property.DisplayName().c_str(), &value.x, 0.01f))
                { properties.Set(saved, Reflection::PropertyValue::MakeVector2(value)); changed = true; }
                Tooltip(property); break;
            }
            case ShaderPropertyKind::Float3:
            {
                auto value = stored->AsVector3();
                if (ImGui::DragFloat3(property.DisplayName().c_str(), &value.x, 0.01f))
                { properties.Set(saved, Reflection::PropertyValue::MakeVector3(value)); changed = true; }
                Tooltip(property); break;
            }
            case ShaderPropertyKind::Float4:
            {
                auto value = stored->AsVector4();
                if (ImGui::DragFloat4(property.DisplayName().c_str(), &value.x, 0.01f))
                { properties.Set(saved, Reflection::PropertyValue::MakeVector4(value)); changed = true; }
                Tooltip(property); break;
            }
            case ShaderPropertyKind::Color:
            {
                auto value = stored->AsVector4();
                if (ImGui::ColorEdit4(property.DisplayName().c_str(), &value.x))
                { properties.Set(saved, Reflection::PropertyValue::MakeColor(value)); changed = true; }
                Tooltip(property); break;
            }
            case ShaderPropertyKind::Texture:
                changed = DrawTexture(property, properties, assets); break;
            case ShaderPropertyKind::Toggle:
            {
                bool value = stored->AsBool(property.default_value.x != 0.0f);
                if (ImGui::Checkbox(property.DisplayName().c_str(), &value))
                { properties.Set(saved, Reflection::PropertyValue::MakeBool(value)); changed = true; }
                Tooltip(property); break;
            }
            case ShaderPropertyKind::Enum:
            {
                int value = stored->AsInt(static_cast<int>(property.default_value.x));
                if (!property.enum_names.empty())
                {
                    std::vector<const char*> names;
                    for (const std::string& item : property.enum_names) names.push_back(item.c_str());
                    if (ImGui::Combo(property.DisplayName().c_str(), &value,
                        names.data(), static_cast<int>(names.size())))
                    { properties.Set(saved, Reflection::PropertyValue::MakeEnum(value)); changed = true; }
                }
                else if (ImGui::InputInt(property.DisplayName().c_str(), &value))
                { properties.Set(saved, Reflection::PropertyValue::MakeEnum(value)); changed = true; }
                Tooltip(property); break;
            }
            default:
                ImGui::TextDisabled("%s: unsupported property", property.DisplayName().c_str());
                break;
            }
            ImGui::PopID();
            return changed;
        }
    }

    bool ShaderPropertyInspector::Draw(const char* id,
        Reflection::PropertyBag& properties,
        const Rendering::ShaderPropertySchema& schema,
        const Assets::AssetDatabase& assets)
    {
        Rendering::MaterialSchema::EnsurePropertyBag(properties, schema);
        bool changed = false;
        ImGui::PushID(id);
        std::string category;
        bool first = true;
        for (const Rendering::ShaderProperty& property : schema.Properties())
        {
            const std::string next = property.category.empty()
                ? std::string("Properties") : property.category;
            if (first || next != category)
            {
                if (!first) ImGui::Spacing();
                category = next;
                ImGui::TextDisabled("%s", category.c_str());
                first = false;
            }
            if (DrawOne(property, properties, assets)) changed = true;
        }
        if (schema.Empty()) ImGui::TextDisabled("公開 Property はありません");
        ImGui::PopID();
        return changed;
    }
}
