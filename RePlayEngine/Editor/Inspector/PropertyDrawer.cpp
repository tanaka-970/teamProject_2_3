#include "PropertyDrawer.h"

#include "../../Assets/AssetDatabase.h"
#include "../../Object/Component/Component.h"
#include "../../Reflection/Property/PropertyDesc.h"
#include "../../Reflection/Registry/PropertyRegistry.h"
#include "../../Scene/Runtime/Scene.h"

#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"

#include <cstring>
#include <string>
#include <vector>

namespace ReplayEngine::Editor
{
    using Reflection::PropertyDesc;
    using Reflection::PropertyRegistry;
    using Reflection::PropertyType;
    using Reflection::PropertyValue;

    namespace
    {
        // 使用している ImGui は 1.80 WIP で BeginDisabled / EndDisabled がまだ無い。
        // 既存プロジェクトが imgui_internal.h を取り込んでいるので、
        // 従来からある PushItemFlag + 透過で読み取り専用を表現する。
        struct DisabledScope
        {
            explicit DisabledScope(bool disabled) : active(disabled)
            {
                if (!active) return;
                ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
                ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
            }
            ~DisabledScope()
            {
                if (!active) return;
                ImGui::PopStyleVar();
                ImGui::PopItemFlag();
            }
            DisabledScope(const DisabledScope&) = delete;
            DisabledScope& operator=(const DisabledScope&) = delete;
            bool active = false;
        };

        void DrawTooltip(const PropertyDesc& desc)
        {
            if (desc.tooltip.empty() || !ImGui::IsItemHovered()) return;
            ImGui::BeginTooltip();
            ImGui::TextUnformatted(desc.tooltip.c_str());
            ImGui::EndTooltip();
        }

        float StepOrDefault(const PropertyDesc& desc, float fallback) noexcept
        {
            return desc.step > 0.0 ? static_cast<float>(desc.step) : fallback;
        }

        float MinimumOrDefault(const PropertyDesc& desc, float fallback) noexcept
        {
            return desc.has_range ? static_cast<float>(desc.minimum) : fallback;
        }

        float MaximumOrDefault(const PropertyDesc& desc, float fallback) noexcept
        {
            return desc.has_range ? static_cast<float>(desc.maximum) : fallback;
        }

        // std::string を ImGui::InputText で編集するための固定長バッファ。
        // 長い文字列は切り詰められるが、Asset の GUID やパスには十分な長さを取る。
        constexpr int text_buffer_size = 512;

        bool DrawTextField(const char* label, std::string& value, bool read_only)
        {
            char buffer[text_buffer_size]{};
            const std::size_t length = value.size() < text_buffer_size - 1
                ? value.size() : text_buffer_size - 1;
            std::memcpy(buffer, value.data(), length);
            buffer[length] = '\0';

            ImGuiInputTextFlags flags = ImGuiInputTextFlags_None;
            if (read_only) flags |= ImGuiInputTextFlags_ReadOnly;

            if (!ImGui::InputText(label, buffer, text_buffer_size, flags)) return false;
            if (read_only) return false;

            value.assign(buffer);
            return true;
        }

        // AssetDatabase に登録されている Asset を選ぶコンボ。
        // 選ばれた場合は GUID を返す。
        bool DrawAssetPicker(const char* label, const Assets::AssetDatabase& database,
            std::string& guid)
        {
            const Assets::AssetRecord* current = database.FindByGuid(guid);
            const char* preview = current != nullptr
                ? current->display_name.c_str()
                : (guid.empty() ? "(未設定)" : "(見つからない Asset)");

            bool changed = false;
            if (ImGui::BeginCombo(label, preview))
            {
                if (ImGui::Selectable("(未設定)", guid.empty()))
                {
                    guid.clear();
                    changed = true;
                }
                for (const Assets::AssetRecord& record : database.Records())
                {
                    const bool selected = record.guid == guid;
                    ImGui::PushID(record.guid.c_str());
                    if (ImGui::Selectable(record.display_name.c_str(), selected))
                    {
                        guid = record.guid;
                        changed = true;
                    }
                    if (selected) ImGui::SetItemDefaultFocus();
                    ImGui::PopID();
                }
                ImGui::EndCombo();
            }
            return changed;
        }

        bool DrawObjectPicker(const char* label, const Scene::Scene& scene, Core::ObjectID& id)
        {
            const Core::GameObject* current = scene.FindGameObjectByID(id);
            const char* preview = current != nullptr ? current->Name().c_str() : "(なし)";

            bool changed = false;
            if (ImGui::BeginCombo(label, preview))
            {
                if (ImGui::Selectable("(なし)", !id.Valid()))
                {
                    id = Core::ObjectID::Invalid();
                    changed = true;
                }
                for (std::size_t index = 0; index < scene.GameObjectCount(); ++index)
                {
                    const Core::GameObject* object = scene.GameObjectAt(index);
                    if (object == nullptr || object->PendingDestroy()) continue;

                    const bool selected = object->ID() == id;
                    ImGui::PushID(static_cast<int>(object->ID().Value()));
                    if (ImGui::Selectable(object->Name().c_str(), selected))
                    {
                        id = object->ID();
                        changed = true;
                    }
                    if (selected) ImGui::SetItemDefaultFocus();
                    ImGui::PopID();
                }
                ImGui::EndCombo();
            }
            return changed;
        }
    }

    bool PropertyDrawer::Draw(const PropertyDesc& desc, Core::Component& component,
        const Assets::AssetDatabase* assets, const Scene::Scene* scene)
    {
        if (!desc.editor_visible || !desc.getter) return false;

        const DisabledScope disabled(desc.read_only);
        const std::string label = "##" + desc.name;

        ImGui::TextUnformatted(desc.DisplayName().c_str());
        DrawTooltip(desc);
        ImGui::SetNextItemWidth(-1.0f);

        const PropertyValue current = desc.Capture(component);
        bool changed = false;

        switch (desc.type)
        {
        case PropertyType::Bool:
        {
            bool value = current.AsBool();
            if (ImGui::Checkbox(label.c_str(), &value))
            {
                desc.Apply(component, PropertyValue::MakeBool(value));
                changed = true;
            }
            break;
        }
        case PropertyType::Int:
        {
            int value = current.AsInt();
            const float speed = StepOrDefault(desc, 1.0f);
            if (ImGui::DragInt(label.c_str(), &value, speed,
                static_cast<int>(MinimumOrDefault(desc, 0.0f)),
                static_cast<int>(MaximumOrDefault(desc, 0.0f))))
            {
                desc.Apply(component, PropertyValue::MakeInt(value));
                changed = true;
            }
            break;
        }
        case PropertyType::Float:
        {
            float value = current.AsFloat();
            if (ImGui::DragFloat(label.c_str(), &value, StepOrDefault(desc, 0.01f),
                MinimumOrDefault(desc, 0.0f), MaximumOrDefault(desc, 0.0f), "%.4f"))
            {
                desc.Apply(component, PropertyValue::MakeFloat(value));
                changed = true;
            }
            break;
        }
        case PropertyType::Double:
        {
            double value = current.AsDouble();
            if (ImGui::InputDouble(label.c_str(), &value, desc.step, desc.step * 10.0, "%.6f"))
            {
                desc.Apply(component, PropertyValue::MakeDouble(value));
                changed = true;
            }
            break;
        }
        case PropertyType::String:
        {
            std::string value = current.AsString();
            if (DrawTextField(label.c_str(), value, desc.read_only))
            {
                desc.Apply(component, PropertyValue::MakeString(std::move(value)));
                changed = true;
            }
            break;
        }
        case PropertyType::AssetPath:
        {
            std::string value = current.AsString();
            if (assets != nullptr)
            {
                if (DrawAssetPicker(label.c_str(), *assets, value))
                {
                    desc.Apply(component, PropertyValue::MakeAssetPath(value));
                    changed = true;
                }
                // GUID を直接確認・貼り付けできるように、下へ生の文字列も出す。
                ImGui::SetNextItemWidth(-1.0f);
                const std::string raw_label = label + "_raw";
                if (DrawTextField(raw_label.c_str(), value, desc.read_only))
                {
                    desc.Apply(component, PropertyValue::MakeAssetPath(std::move(value)));
                    changed = true;
                }
            }
            else if (DrawTextField(label.c_str(), value, desc.read_only))
            {
                desc.Apply(component, PropertyValue::MakeAssetPath(std::move(value)));
                changed = true;
            }
            break;
        }
        case PropertyType::Vector2:
        {
            DirectX::XMFLOAT2 value = current.AsVector2();
            if (ImGui::DragFloat2(label.c_str(), &value.x, StepOrDefault(desc, 0.01f),
                MinimumOrDefault(desc, 0.0f), MaximumOrDefault(desc, 0.0f), "%.4f"))
            {
                desc.Apply(component, PropertyValue::MakeVector2(value));
                changed = true;
            }
            break;
        }
        case PropertyType::Vector3:
        {
            DirectX::XMFLOAT3 value = current.AsVector3();
            if (ImGui::DragFloat3(label.c_str(), &value.x, StepOrDefault(desc, 0.01f),
                MinimumOrDefault(desc, 0.0f), MaximumOrDefault(desc, 0.0f), "%.4f"))
            {
                desc.Apply(component, PropertyValue::MakeVector3(value));
                changed = true;
            }
            break;
        }
        case PropertyType::Vector4:
        case PropertyType::Quaternion:
        {
            DirectX::XMFLOAT4 value = current.AsVector4();
            if (ImGui::DragFloat4(label.c_str(), &value.x, StepOrDefault(desc, 0.01f),
                MinimumOrDefault(desc, 0.0f), MaximumOrDefault(desc, 0.0f), "%.4f"))
            {
                desc.Apply(component, desc.type == PropertyType::Quaternion
                    ? PropertyValue::MakeQuaternion(value)
                    : PropertyValue::MakeVector4(value));
                changed = true;
            }
            break;
        }
        case PropertyType::Color:
        {
            DirectX::XMFLOAT4 value = current.AsVector4();
            if (ImGui::ColorEdit4(label.c_str(), &value.x))
            {
                desc.Apply(component, PropertyValue::MakeColor(value));
                changed = true;
            }
            break;
        }
        case PropertyType::Enum:
        {
            int value = current.AsInt();
            const char* preview = "(不明)";
            if (value >= 0 && value < static_cast<int>(desc.enum_labels.size()))
            {
                preview = desc.enum_labels[static_cast<std::size_t>(value)].c_str();
            }
            if (ImGui::BeginCombo(label.c_str(), preview))
            {
                for (std::size_t i = 0; i < desc.enum_labels.size(); ++i)
                {
                    const bool selected = static_cast<int>(i) == value;
                    if (ImGui::Selectable(desc.enum_labels[i].c_str(), selected))
                    {
                        desc.Apply(component, PropertyValue::MakeEnum(static_cast<int>(i)));
                        changed = true;
                    }
                    if (selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            break;
        }
        case PropertyType::ObjectReference:
        {
            Core::ObjectID value = current.AsObjectReference();
            if (scene != nullptr)
            {
                if (DrawObjectPicker(label.c_str(), *scene, value))
                {
                    desc.Apply(component, PropertyValue::MakeObjectReference(value));
                    changed = true;
                }
            }
            else
            {
                ImGui::TextDisabled("ObjectID %s", value.ToString().c_str());
            }
            break;
        }
        }

        if (changed) component.OnPropertyChanged(desc.name.c_str());
        return changed;
    }

    bool PropertyDrawer::DrawAll(Core::Component& component,
        const Assets::AssetDatabase* assets, const Scene::Scene* scene)
    {
        bool changed = false;
        for (const PropertyDesc& desc : PropertyRegistry::PropertiesOf(component.TypeID()))
        {
            ImGui::PushID(desc.name.c_str());
            if (Draw(desc, component, assets, scene)) changed = true;
            ImGui::PopID();
        }
        return changed;
    }
}
