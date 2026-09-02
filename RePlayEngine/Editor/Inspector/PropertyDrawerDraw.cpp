// PropertyDrawer のうち「1 Property の型別描画 switch」だけを持つ。
//
// PropertyRegistry から受け取った PropertyDesc を既存の共通描画経路へ通す。

#include "PropertyDrawer.h"

#include "../../Assets/AssetDatabase.h"
#include "../../Localization/LocalizationService.h"
#include "../../Components/Physics/ColliderComponent.h"
#include "../../Components/UI/UIImageComponent.h"
#include "../../Object/Component/Component.h"
#include "../../Object/GameObject/GameObject.h"
#include "../../Object/Registry/ComponentRegistry.h"
#include "../Help/EditorHelp.h"
#include "../../Physics/CollisionLayers.h"
#include "../../Reflection/Property/PropertyDesc.h"
#include "../../Reflection/Registry/PropertyRegistry.h"
#include "../../Scene/Runtime/Scene.h"
#include "../../UI/Effects/UIEffect.h"

#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>
#include <string>
#include <vector>

#include "PropertyDrawerInternal.h"

namespace ReplayEngine::Editor
{
    using Reflection::PropertyDesc;
    using Reflection::PropertyRegistry;
    using Reflection::PropertyType;
    using Reflection::PropertyValue;
    using namespace Detail;

    bool PropertyDrawer::Draw(const PropertyDesc& desc, Core::Component& component,
        const Assets::AssetDatabase* assets, const Scene::Scene* scene, bool mixed)
    {
        if (!desc.editor_visible || !desc.getter) return false;

        const bool ui_image_fill_direction_disabled =
            component.TypeID() == Components::UIImageComponent::StaticTypeID() &&
            (desc.name == "fill_method" || desc.name == "fill_reverse") &&
            static_cast<const Components::UIImageComponent&>(component).fill_amount >= 1.0f;
        const DisabledScope disabled(desc.read_only || ui_image_fill_direction_disabled);
        const std::string label = "##" + desc.name;

        const std::string display_label = desc.DisplayName() + (mixed ? " [Mixed]" : "");
        ImGui::TextUnformatted(display_label.c_str());
        const std::string help_key = "property." + std::string(component.TypeName()) +
            "." + desc.name;
        EditorHelp::Item(help_key.c_str(), desc.tooltip.c_str());
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
            if (desc.name == "localization_key" && !desc.read_only)
            {
                const std::vector<std::string> keys =
                    Localization::LocalizationService::Global().Keys();
                const std::string preview = value.empty() ? std::string("（未設定）") : value;
                if (ImGui::BeginCombo((label + "##LocalizationKeyPicker").c_str(),
                    preview.c_str()))
                {
                    if (ImGui::Selectable("（未設定）", value.empty()))
                    {
                        value.clear();
                        desc.Apply(component, PropertyValue::MakeString(value));
                        changed = true;
                    }
                    for (const std::string& key : keys)
                    {
                        const bool selected = key == value;
                        if (ImGui::Selectable(key.c_str(), selected))
                        {
                            value = key;
                            desc.Apply(component, PropertyValue::MakeString(value));
                            changed = true;
                        }
                        if (selected) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                ImGui::SetNextItemWidth(-1.0f);
            }
            if (DrawTextField(label.c_str(), value, desc.read_only))
            {
                desc.Apply(component, PropertyValue::MakeString(std::move(value)));
                changed = true;
            }
            break;
        }
        case PropertyType::AssetPath:
        {
            // AssetReference と同じく asset_type で候補を絞る。メッシュ欄に
            // テクスチャが並んでいたのは、ここへ渡し忘れていたため。
            std::string value = current.AsString();
            if (DrawAssetReference(label.c_str(), assets, value, desc.read_only,
                AssetKindFromTypeName(desc.asset_type)))
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
            const bool is_effect_kind = desc.enum_labels.size() ==
                static_cast<std::size_t>(UI::UIEffectKind::Count);
            const char* preview = "(不明)";
            std::string preview_storage;
            if (value >= 0 && value < static_cast<int>(desc.enum_labels.size()))
            {
                const std::string& enum_label =
                    desc.enum_labels[static_cast<std::size_t>(value)];
                const std::size_t description_separator = enum_label.find(u8" — ");
                if (description_separator == std::string::npos)
                    preview = enum_label.c_str();
                else
                {
                    preview_storage = enum_label.substr(0, description_separator);
                    preview = preview_storage.c_str();
                }
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
                    if (is_effect_kind)
                    {
                        const UI::UIEffectKind effect_kind =
                            static_cast<UI::UIEffectKind>(i);
                        const std::string help_key = std::string("effect.kind.") +
                            UI::UIEffectKindName(effect_kind);
                        EditorHelp::Item(help_key.c_str());
                    }
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


}
