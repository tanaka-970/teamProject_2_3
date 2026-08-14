#pragma once

// PropertyDrawerInternal.h は分割実装だけが使う内部ヘルパで、外部から include しない。
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
#include <string>
#include <utility>
#include <vector>
namespace ReplayEngine::Editor::Detail
{
    using Reflection::PropertyDesc;
    using Reflection::PropertyRegistry;
    using Reflection::PropertyType;
    using Reflection::PropertyValue;
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

        inline void DrawTooltip(const PropertyDesc& desc)
        {
            if (desc.tooltip.empty() || !ImGui::IsItemHovered()) return;
            ImGui::BeginTooltip();
            ImGui::TextUnformatted(desc.tooltip.c_str());
            ImGui::EndTooltip();
        }

        inline float StepOrDefault(const PropertyDesc& desc, float fallback) noexcept
        {
            return desc.step > 0.0 ? static_cast<float>(desc.step) : fallback;
        }

        inline float MinimumOrDefault(const PropertyDesc& desc, float fallback) noexcept
        {
            return desc.has_range ? static_cast<float>(desc.minimum) : fallback;
        }

        inline float MaximumOrDefault(const PropertyDesc& desc, float fallback) noexcept
        {
            return desc.has_range ? static_cast<float>(desc.maximum) : fallback;
        }

        // Layer マスクを人が読める要約にする。
        // 「31」ではなく「Default, Player, Enemy」と出す。
        inline std::string MaskSummary(int mask)
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
        inline std::string ColliderLabel(const Components::ColliderComponent& collider)
        {
            return std::string(Components::ToString(collider.Shape())) +
                " #" + std::to_string(collider.collider_key);
        }

        // std::string を ImGui::InputText で編集するための固定長バッファ。
        // 長い文字列は切り詰められるが、Asset の GUID やパスには十分な長さを取る。
        constexpr int text_buffer_size = 512;

        inline bool DrawTextField(const char* label, std::string& value, bool read_only)
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

        inline const char* BuiltInAssetDisplayName(const std::string& id)
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
        inline bool DrawAssetReference(const char* label, const Assets::AssetDatabase* database,
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
        inline bool DrawObjectPicker(const char* label, const Scene::Scene& scene, Core::ObjectID& id);

        // 配列へ足す新しい要素の初期値。
        // 型ごとの「空の値」を返すだけで、既存の要素には触らない。
        inline PropertyValue MakeDefaultElement(PropertyType type)
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

        inline bool CanCreateElement(PropertyType type)
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
        inline Assets::AssetKind AssetKindFromTypeName(const std::string& name)
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

        inline bool DrawArrayElementValue(const char* label, PropertyValue& element,
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

        inline bool DrawObjectPicker(const char* label, const Scene::Scene& scene, Core::ObjectID& id)
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

        inline bool IsMaterialDynamicProperty(const PropertyDesc& desc)
        {
            constexpr const char prefix[] = "material.";
            return desc.name.rfind(prefix, 0) == 0;
        }

        inline bool MaterialDynamicPropertiesDisabled(Core::Component& component)
        {
            const PropertyDesc* override_desc =
                PropertyRegistry::Find(component.TypeID(), "material_override");
            if (override_desc == nullptr) return false;
            return !override_desc->Capture(component).AsBool(false);
        }

}
