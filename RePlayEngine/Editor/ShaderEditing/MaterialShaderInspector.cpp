#include "MaterialShaderInspector.h"

#include "../../Rendering/Materials/MaterialSchema.h"
#include "../../Rendering/Shaders/BuiltInShaders.h"

#include "imgui/imgui.h"

#include <algorithm>
#include <string>
#include <vector>

namespace ReplayEngine::Editor
{
    namespace
    {
        using Rendering::MaterialAsset;
        using Rendering::ShaderCatalog;
        using Rendering::ShaderID;
        using Rendering::ShaderProperty;
        using Rendering::ShaderPropertyKind;

        ShaderID MaterialShaderID(const MaterialAsset& material) noexcept
        {
            // v3 で shader_guid が明示されているなら、それを唯一の正本にする。
            // 壊れた/未知 GUID を legacy shading_model へ丸めると Missing Shader が
            // 別の Built-in に化け、ユーザーが参照切れに気付けなくなる。
            if (!material.shader_guid.empty())
            {
                ShaderID id;
                return ShaderID::TryParse(material.shader_guid, id) ? id : ShaderID{};
            }

            // v1/v2 互換: GUID がまだ無い Material だけ旧番号から移行表示する。
            return Rendering::BuiltInShaders::FromShadingModel(material.shading_model);
        }

        const ShaderCatalog::Entry* CurrentEntry(const MaterialAsset& material,
            const ShaderCatalog& catalog) noexcept
        {
            const ShaderID id = MaterialShaderID(material);
            if (!id.IsValid()) return nullptr;
            return catalog.Find(id);
        }

        const Assets::AssetRecord* ImageAsset(const Assets::AssetDatabase& assets,
            const std::string& guid) noexcept
        {
            if (guid.empty()) return nullptr;
            const Assets::AssetRecord* record = assets.FindByGuid(guid);
            if (record == nullptr || record->kind != Assets::AssetKind::Image)
                return nullptr;
            return record;
        }

        void Tooltip(const ShaderProperty& property)
        {
            if (!property.tooltip.empty() && ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", property.tooltip.c_str());
        }

        bool DrawTexture(const ShaderProperty& property,
            MaterialAsset& material, const Assets::AssetDatabase& assets)
        {
            const std::string saved = property.SavedName();
            const Reflection::PropertyValue* stored = material.properties.Find(saved);
            const std::string guid = stored == nullptr ? std::string() : stored->AsString();
            const Assets::AssetRecord* current = ImageAsset(assets, guid);

            ImGui::TextUnformatted(property.DisplayName().c_str());
            Tooltip(property);

            std::string button_label;
            if (guid.empty())
            {
                button_label = std::string("None  (default: ") +
                    (property.default_texture.empty() ? "white" : property.default_texture) + ")";
            }
            else if (current != nullptr)
            {
                button_label = current->display_name.empty()
                    ? current->source_path.filename().u8string()
                    : current->display_name;
            }
            else
            {
                button_label = "Missing Image  [" + guid + "]";
            }

            bool changed = false;
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::Button(button_label.c_str(), ImVec2(-1.0f, 0.0f)))
                ImGui::OpenPopup("TextureAssetPicker");

            // Project Browser から画像 Asset を直接ドロップできる。
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* payload =
                    ImGui::AcceptDragDropPayload("REPLAY_ASSET_GUID"))
                {
                    if (payload->Data != nullptr && payload->DataSize > 0)
                    {
                        const char* raw = static_cast<const char*>(payload->Data);
                        const std::string dropped(raw);
                        const Assets::AssetRecord* record = assets.FindByGuid(dropped);
                        if (record != nullptr && record->kind == Assets::AssetKind::Image)
                        {
                            material.properties.Set(saved,
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
                    material.properties.Set(saved,
                        Reflection::PropertyValue::MakeAssetReference(std::string()));
                    changed = true;
                }
                ImGui::Separator();

                std::vector<const Assets::AssetRecord*> images;
                for (const Assets::AssetRecord& record : assets.Records())
                {
                    if (record.kind == Assets::AssetKind::Image &&
                        !assets.IsMissing(record.guid))
                        images.push_back(&record);
                }
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
                    const std::string label = record->display_name.empty()
                        ? record->source_path.filename().u8string()
                        : record->display_name;
                    if (ImGui::Selectable(label.c_str(), record->guid == guid))
                    {
                        material.properties.Set(saved,
                            Reflection::PropertyValue::MakeAssetReference(record->guid));
                        changed = true;
                    }
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("%s", record->source_path.generic_u8string().c_str());
                }
                if (images.empty())
                    ImGui::TextDisabled("Image Asset がありません。Project Browser へ画像を置いてください。");
                ImGui::EndPopup();
            }

            if (current != nullptr)
            {
                ImGui::TextDisabled("%s", current->source_path.generic_u8string().c_str());
            }
            return changed;
        }

        bool DrawProperty(const ShaderProperty& property,
            MaterialAsset& material, const Assets::AssetDatabase& assets)
        {
            const std::string saved = property.SavedName();
            const Reflection::PropertyValue* stored = material.properties.Find(saved);
            if (stored == nullptr) return false;

            ImGui::PushID(saved.c_str());
            bool changed = false;

            switch (property.kind)
            {
            case ShaderPropertyKind::Float:
            {
                float value = stored->AsFloat(property.default_value.x);
                if (ImGui::DragFloat(property.DisplayName().c_str(), &value, 0.01f))
                {
                    material.properties.Set(saved,
                        Reflection::PropertyValue::MakeFloat(value));
                    changed = true;
                }
                Tooltip(property);
                break;
            }
            case ShaderPropertyKind::Range:
            {
                float value = stored->AsFloat(property.default_value.x);
                if (ImGui::SliderFloat(property.DisplayName().c_str(), &value,
                    property.minimum, property.maximum))
                {
                    material.properties.Set(saved,
                        Reflection::PropertyValue::MakeFloat(value));
                    changed = true;
                }
                Tooltip(property);
                break;
            }
            case ShaderPropertyKind::Float2:
            {
                DirectX::XMFLOAT2 value = stored->AsVector2();
                if (ImGui::DragFloat2(property.DisplayName().c_str(), &value.x, 0.01f))
                {
                    material.properties.Set(saved,
                        Reflection::PropertyValue::MakeVector2(value));
                    changed = true;
                }
                Tooltip(property);
                break;
            }
            case ShaderPropertyKind::Float3:
            {
                DirectX::XMFLOAT3 value = stored->AsVector3();
                if (ImGui::DragFloat3(property.DisplayName().c_str(), &value.x, 0.01f))
                {
                    material.properties.Set(saved,
                        Reflection::PropertyValue::MakeVector3(value));
                    changed = true;
                }
                Tooltip(property);
                break;
            }
            case ShaderPropertyKind::Float4:
            {
                DirectX::XMFLOAT4 value = stored->AsVector4();
                if (ImGui::DragFloat4(property.DisplayName().c_str(), &value.x, 0.01f))
                {
                    material.properties.Set(saved,
                        Reflection::PropertyValue::MakeVector4(value));
                    changed = true;
                }
                Tooltip(property);
                break;
            }
            case ShaderPropertyKind::Color:
            {
                DirectX::XMFLOAT4 value = stored->AsVector4();
                if (ImGui::ColorEdit4(property.DisplayName().c_str(), &value.x))
                {
                    material.properties.Set(saved,
                        Reflection::PropertyValue::MakeColor(value));
                    changed = true;
                }
                Tooltip(property);
                break;
            }
            case ShaderPropertyKind::Texture:
                changed = DrawTexture(property, material, assets);
                break;

            case ShaderPropertyKind::Toggle:
            {
                bool value = stored->AsBool(property.default_value.x != 0.0f);
                if (ImGui::Checkbox(property.DisplayName().c_str(), &value))
                {
                    material.properties.Set(saved,
                        Reflection::PropertyValue::MakeBool(value));
                    changed = true;
                }
                Tooltip(property);
                break;
            }
            case ShaderPropertyKind::Enum:
            {
                int value = stored->AsInt(static_cast<int>(property.default_value.x));
                if (!property.enum_names.empty())
                {
                    std::vector<const char*> names;
                    names.reserve(property.enum_names.size());
                    for (const std::string& item : property.enum_names)
                        names.push_back(item.c_str());
                    if (ImGui::Combo(property.DisplayName().c_str(), &value,
                        names.data(), static_cast<int>(names.size())))
                    {
                        material.properties.Set(saved,
                            Reflection::PropertyValue::MakeEnum(value));
                        changed = true;
                    }
                }
                else if (ImGui::InputInt(property.DisplayName().c_str(), &value))
                {
                    material.properties.Set(saved,
                        Reflection::PropertyValue::MakeEnum(value));
                    changed = true;
                }
                Tooltip(property);
                break;
            }
            default:
                ImGui::TextDisabled("%s: unsupported property kind",
                    property.DisplayName().c_str());
                break;
            }

            ImGui::PopID();
            return changed;
        }

        bool DrawShaderPicker(MaterialAsset& material,
            const ShaderCatalog& catalog, bool& out_missing)
        {
            const ShaderID current_id = MaterialShaderID(material);
            const ShaderCatalog::Entry* current = current_id.IsValid()
                ? catalog.Find(current_id) : nullptr;
            out_missing = current == nullptr;

            std::string preview;
            if (current != nullptr)
                preview = current->info.MenuPath();
            else if (!material.shader_guid.empty())
                preview = "Missing Shader";
            else
                preview = "No Shader";

            bool changed = false;
            if (ImGui::BeginCombo("Shader", preview.c_str()))
            {
                std::vector<const ShaderCatalog::Entry*> entries;
                for (const ShaderCatalog::Entry& entry : catalog.All())
                {
                    if (entry.info.domain == Rendering::ShaderDomain::Surface)
                        entries.push_back(&entry);
                }
                std::sort(entries.begin(), entries.end(),
                    [](const ShaderCatalog::Entry* a, const ShaderCatalog::Entry* b)
                    {
                        return a->info.MenuPath() < b->info.MenuPath();
                    });

                for (const ShaderCatalog::Entry* entry : entries)
                {
                    const bool selected = current_id.IsValid() &&
                        entry->info.id == current_id;
                    const std::string label = entry->info.MenuPath();
                    if (ImGui::Selectable(label.c_str(), selected))
                    {
                        if (Rendering::MaterialSchema::SelectShader(material, *entry))
                        {
                            changed = true;
                            out_missing = false;
                        }
                    }
                    if (selected) ImGui::SetItemDefaultFocus();
                }

                if (entries.empty())
                    ImGui::TextDisabled("surface Shader が Catalog にありません");
                ImGui::EndCombo();
            }

            if (current == nullptr)
            {
                ImGui::TextColored(ImVec4(1.0f, 0.38f, 0.38f, 1.0f),
                    "Missing Shader - 保存済み Property は保持されています");
                if (!material.shader_guid.empty())
                    ImGui::TextDisabled("GUID  %s", material.shader_guid.c_str());
            }
            else
            {
                const bool usable = current->EverCompiled();
                ImGui::TextDisabled("%s  |  %s  |  %s",
                    current->info.source_path.filename().u8string().c_str(),
                    Rendering::ToString(current->info.domain),
                    Rendering::ToString(current->info.lighting_model));
                if (!current->AllCompiled())
                {
                    ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.30f, 1.0f),
                        usable
                        ? "直近の Compile に失敗。最後に成功した bytecode を使用中"
                        : "Shader Compile に成功していません");
                }
            }
            return changed;
        }

        std::size_t UnknownPropertyCount(const MaterialAsset& material,
            const Rendering::ShaderPropertySchema* schema)
        {
            if (schema == nullptr) return material.properties.Size();
            std::size_t count = 0;
            for (const Reflection::PropertyBag::Entry& item : material.properties.Entries())
            {
                if (schema->FindBySavedName(item.name) == nullptr) ++count;
            }
            return count;
        }
    }

    MaterialShaderInspector::Result MaterialShaderInspector::Draw(const char* id,
        Rendering::MaterialAsset& material,
        const Rendering::ShaderCatalog& catalog,
        const Assets::AssetDatabase& assets)
    {
        Result result{};
        ImGui::PushID(id);

        ImGui::TextUnformatted("Shader");
        bool missing = false;
        if (DrawShaderPicker(material, catalog, missing))
        {
            result.changed = true;
            result.shader_changed = true;
        }
        result.missing_shader = missing;

        const ShaderCatalog::Entry* entry = CurrentEntry(material, catalog);
        if (entry != nullptr && entry->schema)
        {
            if (Rendering::MaterialSchema::EnsureProperties(material, *entry->schema))
            {
                result.changed = true;
                result.properties_changed = true;
            }

            ImGui::Separator();
            ImGui::TextUnformatted("Shader Properties");

            std::string active_category;
            bool first = true;
            for (const ShaderProperty& property : entry->schema->Properties())
            {
                const std::string category = property.category.empty()
                    ? std::string("Properties") : property.category;
                if (first || category != active_category)
                {
                    if (!first) ImGui::Spacing();
                    active_category = category;
                    ImGui::TextDisabled("%s", active_category.c_str());
                    first = false;
                }

                if (DrawProperty(property, material, assets))
                {
                    result.changed = true;
                    result.properties_changed = true;
                }
            }
            if (entry->schema->Empty())
                ImGui::TextDisabled("公開 Property はありません");
        }
        else
        {
            ImGui::Separator();
            ImGui::TextDisabled("Shader が戻るまで PropertyBag は編集せず保持します。");
        }

        // Shader schema にまだ移していない render-state だけをここへ残す。
        // DoubleSided / AlphaCutoff は Shader が property 宣言していれば上で出る。
        ImGui::Separator();
        if (ImGui::TreeNodeEx("Rendering", ImGuiTreeNodeFlags_DefaultOpen))
        {
            int alpha = static_cast<int>(material.alpha_mode);
            const char* alpha_modes[] = { "Opaque", "Mask", "Blend" };
            if (ImGui::Combo("Alpha Mode", &alpha, alpha_modes,
                IM_ARRAYSIZE(alpha_modes)))
            {
                material.alpha_mode =
                    static_cast<Rendering::MaterialAlphaMode>(alpha);
                result.changed = true;
                result.rendering_changed = true;
            }
            ImGui::TreePop();
        }

        if (result.properties_changed || result.rendering_changed)
        {
            // Phase 6/12 の旧互換 bridge を壊さない。
            material.SyncPropertiesToLegacyFields();
            if (result.rendering_changed) material.SyncLegacyFieldsToProperties();
        }

        if (ImGui::TreeNode("Advanced"))
        {
            ImGui::TextDisabled("Shader GUID");
            ImGui::SameLine();
            ImGui::TextUnformatted(material.shader_guid.empty()
                ? "(legacy / empty)" : material.shader_guid.c_str());

            if (entry != nullptr)
            {
                ImGui::TextDisabled("Source");
                ImGui::TextWrapped("%s", entry->info.source_path.generic_u8string().c_str());
                ImGui::TextDisabled("Schema Revision  %u",
                    entry->schema ? entry->schema->Revision() : 0u);
            }

            const std::size_t retained = UnknownPropertyCount(material,
                entry != nullptr && entry->schema ? entry->schema.get() : nullptr);
            ImGui::TextDisabled("Retained / unknown properties  %zu", retained);
            ImGui::TextDisabled("未知 Property は Shader 切替や Missing 時にも削除しません");
            ImGui::TreePop();
        }

        ImGui::PopID();
        return result;
    }
}
