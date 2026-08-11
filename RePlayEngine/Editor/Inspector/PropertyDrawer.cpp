#include "PropertyDrawer.h"

#include "../../Assets/AssetDatabase.h"
#include "../../Components/Physics/ColliderComponent.h"
#include "../../Object/Component/Component.h"
#include "../../Object/GameObject/GameObject.h"
#include "../../Object/Registry/ComponentRegistry.h"
#include "../../Physics/CollisionLayers.h"
#include "../../Reflection/Property/PropertyDesc.h"
#include "../../Reflection/Registry/PropertyRegistry.h"
#include "../../Scene/Runtime/Scene.h"

#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>
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

        // Layer マスクを人が読める要約にする。
        // 「31」ではなく「Default, Player, Enemy」と出す。
        std::string MaskSummary(int mask)
        {
            if (mask == 0) return "なし";
            if ((mask & Physics::CollisionLayers::all_layers_mask) ==
                Physics::CollisionLayers::all_layers_mask)
            {
                return "すべて";
            }

            std::string summary;
            int shown = 0;
            for (int layer = 0; layer < Physics::CollisionLayers::count; ++layer)
            {
                if ((mask & Physics::CollisionLayers::MaskBit(layer)) == 0) continue;
                if (shown >= 3)
                {
                    summary += " ...";
                    break;
                }
                if (!summary.empty()) summary += ", ";
                summary += Physics::CollisionLayers::Name(layer);
                ++shown;
            }
            return summary;
        }

        // Collider 1 つ分の表示名。「形状名 #キー」の形にする。
        // 同じ形状が複数付いていても見分けられる。
        std::string ColliderLabel(const Components::ColliderComponent& collider)
        {
            return std::string(Components::ToString(collider.Shape())) +
                " #" + std::to_string(collider.collider_key);
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

        const char* BuiltInAssetDisplayName(const std::string& id)
        {
            if (id == "builtin:plane") return "Built-in Plane";
            if (id == "builtin:cube") return "Built-in Cube";
            if (id == "builtin:sphere") return "Built-in Sphere";
            if (id == "builtin:capsule") return "Built-in Capsule";
            if (id == "builtin:cylinder") return "Built-in Cylinder";
            if (id == "builtin:quad") return "Built-in Quad";
            return nullptr;
        }

        // Asset 参照の共通描画。
        //
        // 生の GUID（1bd1358040255f5bd18f3be0d79fbe3c のような文字列）を
        // 通常の Inspector へ出さない。表示するのは Asset 名とパスだけ。
        // GUID は折り畳みの「詳細」の中にのみ置く。
        //
        // Component ごとの専用処理ではなく、AssetPath 型の Property すべてに効く。
        // kind_filter を指定すると、その種類の Asset だけを候補に出す。
        // Scene 参照へ Texture を設定できてしまう経路を作らないために使う。
        bool DrawAssetReference(const char* label, const Assets::AssetDatabase* database,
            std::string& guid, bool read_only,
            Assets::AssetKind kind_filter = Assets::AssetKind::Unknown)
        {
            bool changed = false;

            const Assets::AssetRecord* current =
                database != nullptr ? database->FindByGuid(guid) : nullptr;
            const char* builtin_name = BuiltInAssetDisplayName(guid);

            // 選択欄。名前だけを見せる。builtin:* は AssetDatabase の外にある
            // Engine 内蔵メッシュなので Missing Asset 扱いにしない。
            const char* preview = "(未設定)";
            if (!guid.empty())
            {
                if (builtin_name != nullptr) preview = builtin_name;
                else preview = current != nullptr ? current->display_name.c_str() : "Missing Asset";
            }

            if (database != nullptr)
            {
                const DisabledScope disabled(read_only);
                if (ImGui::BeginCombo(label, preview))
                {
                    if (ImGui::Selectable("(未設定)", guid.empty()))
                    {
                        guid.clear();
                        changed = true;
                    }
                    for (const Assets::AssetRecord& record : database->Records())
                    {
                        // 種類が指定されていれば、それ以外は候補に出さない。
                        // 出さないだけで、既に保存されている値は消さない。
                        if (kind_filter != Assets::AssetKind::Unknown &&
                            record.kind != kind_filter)
                        {
                            continue;
                        }

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
            }
            else
            {
                // AssetDatabase が無い場合だけ手入力にする。
                if (DrawTextField(label, guid, read_only)) changed = true;
            }

            // 状態表示。パスか、欠損の警告。
            if (guid.empty())
            {
                ImGui::TextDisabled("  Asset が未指定です");
            }
            else if (builtin_name != nullptr)
            {
                ImGui::TextDisabled("  Engine Built-in Primitive");
            }
            else if (current != nullptr)
            {
                ImGui::TextDisabled("  %s", current->source_path.generic_string().c_str());
            }
            else
            {
                ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f),
                    "  GUID に対応する Asset が見つかりません");
            }

            // 解除ボタン。
            if (!guid.empty() && !read_only)
            {
                if (ImGui::SmallButton("解除"))
                {
                    guid.clear();
                    changed = true;
                }
                ImGui::SameLine();
            }

            // GUID は詳細の中だけ。通常表示には出さない。
            if (!guid.empty())
            {
                if (ImGui::TreeNode("詳細##AssetDetail"))
                {
                    ImGui::TextDisabled("Asset ID");
                    ImGui::TextWrapped("%s", guid.c_str());
                    if (ImGui::SmallButton("コピー")) ImGui::SetClipboardText(guid.c_str());
                    ImGui::TreePop();
                }
            }
            else
            {
                ImGui::NewLine();
            }

            return changed;
        }

        // 先に宣言だけしておく。配列要素の編集から呼ぶため。
        bool DrawObjectPicker(const char* label, const Scene::Scene& scene, Core::ObjectID& id);

        // 配列へ足す新しい要素の初期値。
        // 型ごとの「空の値」を返すだけで、既存の要素には触らない。
        PropertyValue MakeDefaultElement(PropertyType type)
        {
            switch (type)
            {
            case PropertyType::Bool:      return PropertyValue::MakeBool(false);
            case PropertyType::Int:       return PropertyValue::MakeInt(0);
            case PropertyType::Int64:     return PropertyValue::MakeInt64(0);
            case PropertyType::UInt64:    return PropertyValue::MakeUInt64(0);
            case PropertyType::Float:     return PropertyValue::MakeFloat(0.0f);
            case PropertyType::Double:    return PropertyValue::MakeDouble(0.0);
            case PropertyType::String:    return PropertyValue::MakeString(std::string());
            case PropertyType::Enum:      return PropertyValue::MakeEnum(0);
            case PropertyType::AssetPath: return PropertyValue::MakeAssetPath(std::string());
            case PropertyType::ObjectReference:
                return PropertyValue::MakeObjectReference(Core::ObjectID::Invalid());
            case PropertyType::AssetReference:
                return PropertyValue::MakeAssetReference(std::string());
            case PropertyType::SceneReference:
                return PropertyValue::MakeSceneReference(std::string());
            case PropertyType::Vector2:
                return PropertyValue::MakeVector2(DirectX::XMFLOAT2{ 0.0f, 0.0f });
            case PropertyType::Vector3:
                return PropertyValue::MakeVector3(DirectX::XMFLOAT3{ 0.0f, 0.0f, 0.0f });
            case PropertyType::Vector4:
                return PropertyValue::MakeVector4(
                    DirectX::XMFLOAT4{ 0.0f, 0.0f, 0.0f, 0.0f });
            case PropertyType::Color:
                return PropertyValue::MakeColor(DirectX::XMFLOAT4{ 1.0f, 1.0f, 1.0f, 1.0f });
            default:
                break;
            }
            // 既定値を用意できない型。呼び出し側が追加ボタンを出さない。
            return PropertyValue();
        }

        bool CanCreateElement(PropertyType type)
        {
            return MakeDefaultElement(type).Type() == type;
        }

        // 配列要素 1 件の編集欄。
        //
        // 対応していない型では読み取り専用の表示だけを出す。
        // 「編集できるように見えて実は反映されない」欄は置かない。
        // PropertyDesc の asset_type（`.OfAssetType("Image")` などで指定した名前）を
        // Picker の絞り込みに使える AssetKind へ直す。
        //
        // 指定が無い / 知らない名前なら Unknown を返す。Unknown は「絞り込まない」なので、
        // 綴りを間違えても候補が全部消えるのではなく、従来どおり全部出るだけで済む。
        Assets::AssetKind AssetKindFromTypeName(const std::string& name)
        {
            if (name.empty()) return Assets::AssetKind::Unknown;
            if (name == "Model") return Assets::AssetKind::Model;
            if (name == "Image") return Assets::AssetKind::Image;
            if (name == "Audio") return Assets::AssetKind::Audio;
            if (name == "Shader") return Assets::AssetKind::Shader;
            if (name == "Scene") return Assets::AssetKind::Scene;
            if (name == "Material") return Assets::AssetKind::Material;
            if (name == "Script") return Assets::AssetKind::Script;
            if (name == "SceneFlow") return Assets::AssetKind::SceneFlow;
            if (name == "Font") return Assets::AssetKind::Font;
            if (name == "Motion") return Assets::AssetKind::Motion;
            return Assets::AssetKind::Unknown;
        }

        bool DrawArrayElementValue(const char* label, PropertyValue& element,
            const Assets::AssetDatabase* assets, const Scene::Scene* scene, bool read_only,
            const std::string& asset_type = std::string())
        {
            const DisabledScope disabled(read_only);
            switch (element.Type())
            {
            case PropertyType::Bool:
            {
                bool value = element.AsBool();
                if (ImGui::Checkbox(label, &value))
                {
                    element = PropertyValue::MakeBool(value);
                    return true;
                }
                return false;
            }
            case PropertyType::Int:
            case PropertyType::Enum:
            {
                int value = element.AsInt();
                if (ImGui::InputInt(label, &value))
                {
                    element = element.Type() == PropertyType::Enum
                        ? PropertyValue::MakeEnum(value) : PropertyValue::MakeInt(value);
                    return true;
                }
                return false;
            }
            case PropertyType::Float:
            {
                float value = element.AsFloat();
                if (ImGui::DragFloat(label, &value, 0.01f))
                {
                    element = PropertyValue::MakeFloat(value);
                    return true;
                }
                return false;
            }
            case PropertyType::Double:
            {
                double value = element.AsDouble();
                if (ImGui::InputDouble(label, &value))
                {
                    element = PropertyValue::MakeDouble(value);
                    return true;
                }
                return false;
            }
            case PropertyType::String:
            case PropertyType::AssetPath:
            {
                std::string value = element.AsString();
                if (DrawTextField(label, value, read_only))
                {
                    element = element.Type() == PropertyType::AssetPath
                        ? PropertyValue::MakeAssetPath(std::move(value))
                        : PropertyValue::MakeString(std::move(value));
                    return true;
                }
                return false;
            }
            case PropertyType::Vector3:
            {
                DirectX::XMFLOAT3 value = element.AsVector3();
                if (ImGui::DragFloat3(label, &value.x, 0.01f))
                {
                    element = PropertyValue::MakeVector3(value);
                    return true;
                }
                return false;
            }
            case PropertyType::Color:
            {
                DirectX::XMFLOAT4 value = element.AsVector4();
                if (ImGui::ColorEdit4(label, &value.x))
                {
                    element = PropertyValue::MakeColor(value);
                    return true;
                }
                return false;
            }
            case PropertyType::ObjectReference:
            {
                if (scene == nullptr)
                {
                    ImGui::TextDisabled(u8"(Scene が無いため選べません)");
                    return false;
                }
                Core::ObjectID value = element.AsObjectReference();
                if (DrawObjectPicker(label, *scene, value))
                {
                    element = PropertyValue::MakeObjectReference(value);
                    return true;
                }
                return false;
            }
            case PropertyType::AssetReference:
            case PropertyType::SceneReference:
            {
                std::string guid = element.AsString();
                const Assets::AssetKind kind_filter =
                    element.Type() == PropertyType::SceneReference
                        ? Assets::AssetKind::Scene
                        : AssetKindFromTypeName(asset_type);
                if (DrawAssetReference(label, assets, guid, read_only, kind_filter))
                {
                    element = element.Type() == PropertyType::SceneReference
                        ? PropertyValue::MakeSceneReference(std::move(guid))
                        : PropertyValue::MakeAssetReference(std::move(guid));
                    return true;
                }
                return false;
            }
            default:
                break;
            }

            // 未対応の型。値は表示するだけで一切書き換えない。
            ImGui::TextDisabled(u8"%s: %s（この型の編集欄は未対応。値は保持されます）",
                label, Reflection::ToString(element.Type()));
            return false;
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

        bool IsMaterialDynamicProperty(const PropertyDesc& desc)
        {
            constexpr const char prefix[] = "material.";
            return desc.name.rfind(prefix, 0) == 0;
        }

        bool MaterialDynamicPropertiesDisabled(Core::Component& component)
        {
            const PropertyDesc* override_desc =
                PropertyRegistry::Find(component.TypeID(), "material_override");
            if (override_desc == nullptr) return false;
            return !override_desc->Capture(component).AsBool(false);
        }
    }

    bool PropertyDrawer::Draw(const PropertyDesc& desc, Core::Component& component,
        const Assets::AssetDatabase* assets, const Scene::Scene* scene, bool mixed)
    {
        if (!desc.editor_visible || !desc.getter) return false;

        const DisabledScope disabled(desc.read_only);
        const std::string label = "##" + desc.name;

        const std::string display_label = desc.DisplayName() + (mixed ? " [Mixed]" : "");
        ImGui::TextUnformatted(display_label.c_str());
        DrawTooltip(desc);
        ImGui::SetNextItemWidth(-1.0f);

        const PropertyValue current = desc.Capture(component);
        bool changed = false;

        switch (desc.type)
        {
        case PropertyType::Bool:
        {
            bool value = current.AsBool();
            if (mixed) ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, true);
            if (ImGui::Checkbox(label.c_str(), &value))
            {
                desc.Apply(component, PropertyValue::MakeBool(value));
                changed = true;
            }
            if (mixed) ImGui::PopItemFlag();
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
            if (DrawAssetReference(label.c_str(), assets, value, desc.read_only))
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
        case PropertyType::CollisionLayer:
        {
            // 整数を直接見せない。Layer 名の一覧から選ぶ。
            const int value = Physics::CollisionLayers::ClampLayer(current.AsInt());
            if (ImGui::BeginCombo(label.c_str(), Physics::CollisionLayers::Name(value)))
            {
                for (int layer = 0; layer < Physics::CollisionLayers::count; ++layer)
                {
                    const bool selected = layer == value;
                    if (ImGui::Selectable(Physics::CollisionLayers::Name(layer), selected))
                    {
                        desc.Apply(component, PropertyValue::MakeCollisionLayer(layer));
                        changed = true;
                    }
                    if (selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            break;
        }
        case PropertyType::CollisionMask:
        {
            // ビット列も整数では見せない。Layer 名のチェックボックスにする。
            int value = current.AsInt();

            // 旧データ互換: -1 は「すべて」の意味だったので、全ビット立てへ直す。
            if (value == -1) value = Physics::CollisionLayers::all_layers_mask;

            const std::string summary = MaskSummary(value);
            if (ImGui::BeginCombo(label.c_str(), summary.c_str()))
            {
                if (ImGui::Selectable("すべて", false))
                {
                    desc.Apply(component, PropertyValue::MakeCollisionMask(
                        Physics::CollisionLayers::all_layers_mask));
                    changed = true;
                }
                if (ImGui::Selectable("なし", false))
                {
                    desc.Apply(component, PropertyValue::MakeCollisionMask(0));
                    changed = true;
                }
                ImGui::Separator();

                for (int layer = 0; layer < Physics::CollisionLayers::count; ++layer)
                {
                    const int bit = Physics::CollisionLayers::MaskBit(layer);
                    bool enabled = (value & bit) != 0;
                    if (ImGui::Checkbox(Physics::CollisionLayers::Name(layer), &enabled))
                    {
                        const int updated = enabled ? (value | bit) : (value & ~bit);
                        desc.Apply(component, PropertyValue::MakeCollisionMask(updated));
                        changed = true;
                    }
                }
                ImGui::EndCombo();
            }
            break;
        }
        case PropertyType::ColliderReference:
        {
            // 同じ GameObject に付いている Collider の一覧から選ぶ。
            // Component 名でも GameObject 名でもなく collider_key を保存する。
            const int value = current.AsInt();
            Core::GameObject* owner = component.Owner();

            std::string preview = "(未設定)";
            const Components::ColliderComponent* selected_collider = nullptr;
            if (owner != nullptr && value > 0)
            {
                selected_collider = Components::FindColliderByKey(*owner, value);
                preview = selected_collider != nullptr
                    ? ColliderLabel(*selected_collider) : std::string("Missing Collider");
            }

            if (ImGui::BeginCombo(label.c_str(), preview.c_str()))
            {
                if (ImGui::Selectable("(未設定)", value <= 0))
                {
                    desc.Apply(component, PropertyValue::MakeColliderReference(0));
                    changed = true;
                }
                if (owner != nullptr)
                {
                    for (std::size_t index = 0; index < owner->ComponentCount(); ++index)
                    {
                        const auto* collider = dynamic_cast<const Components::ColliderComponent*>(
                            owner->ComponentAt(index));
                        if (collider == nullptr || collider->PendingDestroy()) continue;

                        // 候補を制限する。
                        // Mesh Collider と Trigger Collider は移動用に使えないので出さない。
                        if (!collider->UsableAsCharacterShape()) continue;
                        if (collider->is_trigger) continue;

                        const bool is_selected = collider->collider_key == value;
                        if (ImGui::Selectable(ColliderLabel(*collider).c_str(), is_selected))
                        {
                            desc.Apply(component, PropertyValue::MakeColliderReference(
                                collider->collider_key));
                            changed = true;
                        }
                        if (is_selected) ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }

            if (value > 0 && selected_collider == nullptr)
            {
                ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.25f, 1.0f),
                    u8"⚠ 移動用 Collider が設定されていません");
            }
            break;
        }

        // ---- v11 で追加した型 ------------------------------------------------

        case PropertyType::Int64:
        {
            std::int64_t value = current.AsInt64();
            if (ImGui::InputScalar(label.c_str(), ImGuiDataType_S64, &value))
            {
                desc.Apply(component, PropertyValue::MakeInt64(value));
                changed = true;
            }
            break;
        }
        case PropertyType::UInt64:
        {
            std::uint64_t value = current.AsUInt64();
            if (ImGui::InputScalar(label.c_str(), ImGuiDataType_U64, &value))
            {
                desc.Apply(component, PropertyValue::MakeUInt64(value));
                changed = true;
            }
            break;
        }
        case PropertyType::AssetReference:
        case PropertyType::SceneReference:
        {
            // どちらも AssetGUID を保存する。Picker は同じものを使う。
            //
            // SceneReference は Scene Asset だけを候補に出す。
            // 型で分けてある意味が「Picker に出る候補が違う」ことなので、
            // ここで絞らないと Texture を遷移先に設定できてしまう。
            //
            // AssetReference は PropertyDesc の asset_type で絞る。
            // `.OfAssetType("Image")` を書いてあるのにここで見ていなかったため、
            // 画像を選ぶ欄にモデルもマテリアルもスクリプトも並んでいた。
            // 指定が無ければ従来どおり全部出す（Unknown = 絞り込まない）。
            //
            // 解決できない GUID でも値は消さない。Picker には "Missing Asset" と出て、
            // Asset が戻れば自動的に元の表示へ戻る。
            std::string guid = current.AsString();
            const Assets::AssetKind kind_filter =
                desc.type == PropertyType::SceneReference
                    ? Assets::AssetKind::Scene
                    : AssetKindFromTypeName(desc.asset_type);
            if (DrawAssetReference(label.c_str(), assets, guid, desc.read_only, kind_filter))
            {
                desc.Apply(component, desc.type == PropertyType::AssetReference
                    ? PropertyValue::MakeAssetReference(guid)
                    : PropertyValue::MakeSceneReference(guid));
                changed = true;
            }
            break;
        }
        case PropertyType::ComponentReference:
        {
            // 所有 GameObject と、その中の Component の 2 段で選ぶ。
            //
            // 保存されるのは ComponentStableID。並び順でも型名でもないので、
            // Component を並べ替えても型名を変えても参照は切れない。
            //
            // 参照そのものは失わない。指す先が見つからないときも値は保持し、
            // 「Missing」と表示するだけにする。黙って無効化しない。
            Reflection::ComponentReference reference = current.AsComponentReference();

            if (scene == nullptr)
            {
                ImGui::TextDisabled(u8"(Scene が無いため参照先を選べません)");
                ImGui::TextDisabled(u8"Component StableID: %u",
                    static_cast<unsigned int>(reference.component));
                break;
            }

            Core::ObjectID owner_id = reference.owner;
            if (DrawObjectPicker(label.c_str(), *scene, owner_id))
            {
                reference.owner = owner_id;

                // 所有 GameObject を変えたら Component の指定は外す。
                // 別の GameObject の同じ番号は、まったく無関係な Component。
                reference.component = Core::invalid_component_stable_id;
                desc.Apply(component, PropertyValue::MakeComponentReference(reference));
                changed = true;
            }

            Core::GameObject* owner_object = scene->FindGameObjectByID(reference.owner);
            if (owner_object == nullptr)
            {
                if (reference.owner.Valid())
                {
                    ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.25f, 1.0f),
                        u8"  参照先の GameObject が見つかりません（値は保持しています）");
                }
                ImGui::TextDisabled(u8"  Component StableID: %u",
                    static_cast<unsigned int>(reference.component));
                break;
            }

            // 期待する型が宣言されていれば、その型だけを候補に出す。
            const Reflection::TypeGUID expected = desc.expected_component_type;

            const Core::Component* selected_component =
                owner_object->FindComponentByStableID(reference.component);
            const std::string preview = selected_component != nullptr
                ? std::string(selected_component->TypeName())
                : (reference.component == Core::invalid_component_stable_id
                    ? std::string(u8"(未設定)") : std::string(u8"Missing Component"));

            ImGui::PushID("component");
            {
                const DisabledScope disabled(desc.read_only);
                if (ImGui::BeginCombo(u8"  Component", preview.c_str()))
                {
                    if (ImGui::Selectable(u8"(未設定)",
                        reference.component == Core::invalid_component_stable_id))
                    {
                        reference.component = Core::invalid_component_stable_id;
                        desc.Apply(component,
                            PropertyValue::MakeComponentReference(reference));
                        changed = true;
                    }

                    for (std::size_t index = 0; index < owner_object->ComponentCount(); ++index)
                    {
                        const Core::Component* candidate = owner_object->ComponentAt(index);
                        if (candidate == nullptr || candidate->PendingDestroy()) continue;

                        if (expected.IsValid())
                        {
                            const Core::ComponentTypeInfo* info =
                                Core::ComponentRegistry::Find(candidate->TypeID());
                            if (info == nullptr || info->type_guid != expected) continue;
                        }

                        const bool selected = candidate->StableID() == reference.component;
                        ImGui::PushID(static_cast<int>(index));
                        const std::string entry = std::string(candidate->TypeName()) +
                            " #" + std::to_string(
                                static_cast<unsigned int>(candidate->StableID()));
                        if (ImGui::Selectable(entry.c_str(), selected))
                        {
                            reference.component = candidate->StableID();
                            desc.Apply(component,
                                PropertyValue::MakeComponentReference(reference));
                            changed = true;
                        }
                        if (selected) ImGui::SetItemDefaultFocus();
                        ImGui::PopID();
                    }
                    ImGui::EndCombo();
                }
            }
            ImGui::PopID();

            if (reference.component != Core::invalid_component_stable_id &&
                selected_component == nullptr)
            {
                ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.25f, 1.0f),
                    u8"  参照先の Component が見つかりません（値は保持しています）");
            }
            ImGui::TextDisabled(u8"  StableID: %u",
                static_cast<unsigned int>(reference.component));
            break;
        }
        case PropertyType::Array:
        {
            // 配列は Reflection 側が std::vector<T> の取り込みと書き戻しに
            // 対応しているので、追加・削除・並べ替えまで提供する。
            //
            // 反映は「1 操作 = 配列まるごと 1 回の Apply」にしてある。
            // 途中の状態を書き戻さないので、操作の途中で要素が失われることがない。
            const PropertyType element_type = current.ArrayElementType();
            std::vector<PropertyValue> elements = current.ArrayElements();
            const bool can_add = CanCreateElement(element_type);

            bool array_changed = false;

            const std::string header = label + " [" +
                std::to_string(elements.size()) + "]";
            if (ImGui::TreeNodeEx(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::TextDisabled(u8"要素の型: %s", Reflection::ToString(element_type));

                std::size_t remove_index = elements.size();
                std::size_t move_up_index = elements.size();
                std::size_t move_down_index = elements.size();

                for (std::size_t index = 0; index < elements.size(); ++index)
                {
                    ImGui::PushID(static_cast<int>(index));

                    const std::string element_label = "[" + std::to_string(index) + "]";
                    if (DrawArrayElementValue(element_label.c_str(), elements[index],
                        assets, scene, desc.read_only, desc.asset_type))
                    {
                        array_changed = true;
                    }

                    if (!desc.read_only)
                    {
                        ImGui::SameLine();
                        if (ImGui::SmallButton(u8"▲")) move_up_index = index;
                        ImGui::SameLine();
                        if (ImGui::SmallButton(u8"▼")) move_down_index = index;
                        ImGui::SameLine();
                        if (ImGui::SmallButton(u8"削除")) remove_index = index;
                    }
                    ImGui::PopID();
                }

                if (move_up_index > 0 && move_up_index < elements.size())
                {
                    std::swap(elements[move_up_index], elements[move_up_index - 1]);
                    array_changed = true;
                }
                if (move_down_index + 1 < elements.size())
                {
                    std::swap(elements[move_down_index], elements[move_down_index + 1]);
                    array_changed = true;
                }
                if (remove_index < elements.size())
                {
                    elements.erase(elements.begin() +
                        static_cast<std::ptrdiff_t>(remove_index));
                    array_changed = true;
                }

                if (!desc.read_only && can_add)
                {
                    if (ImGui::SmallButton(u8"＋ 要素を追加"))
                    {
                        elements.push_back(MakeDefaultElement(element_type));
                        array_changed = true;
                    }
                }
                else if (!desc.read_only)
                {
                    // 既定値を作れない型。追加ボタンを出さない。
                    // 出して何も起きないより、出せない理由を書く方がよい。
                    ImGui::TextDisabled(
                        u8"この要素型は既定値を作れないため追加できません（既存の要素は保持されます）");
                }

                ImGui::TreePop();
            }

            if (array_changed)
            {
                desc.Apply(component,
                    PropertyValue::MakeArray(element_type, std::move(elements)));
                changed = true;
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

        // インスタンスごとに変わるプロパティ（Script の公開変数など）。
        //
        // ここに型ごとの分岐は書かない。Component が申告した配列を
        // 静的分とまったく同じ経路で描くだけで、
        // 見た目は PropertyType と PropertyDesc のメタ情報だけで決まる。
        //
        // ポインタを 1 回だけ取って回す。描画の途中で中身が入れ替わらないことは
        // 呼ばれる側の約束（Component::DynamicProperties のコメントを参照）。
        if (const std::vector<PropertyDesc>* dynamic = component.DynamicProperties())
        {
            const bool material_dynamic_disabled =
                MaterialDynamicPropertiesDisabled(component);
            for (const PropertyDesc& desc : *dynamic)
            {
                ImGui::PushID(desc.name.c_str());
                const DisabledScope disabled(material_dynamic_disabled &&
                    IsMaterialDynamicProperty(desc));
                if (Draw(desc, component, assets, scene)) changed = true;
                ImGui::PopID();
            }
        }
        return changed;
    }
}
