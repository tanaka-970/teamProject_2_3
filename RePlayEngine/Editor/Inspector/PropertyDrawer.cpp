// PropertyDrawer の責務を 3 つのファイルへ分けている:
//   PropertyDrawer.cpp       … PropertyRegistry の全 Component を回す入口（このファイル）
//   PropertyDrawerDraw.cpp   … 1 Property の型別描画 switch
//                              （PropertyDrawer::Draw という単一関数。これ以上は本文を変えずに分割できない）
//   PropertyDrawerInternal.h … 分割後の描画から共有する内部ヘルパ
//
// 型ごとの描画規則は PropertyRegistry と既存の Draw 経路に置いたままにする。
// PropertyDrawerDraw.cpp は全 PropertyType を 1 つの switch で描き、同じ changed 状態と
// 最後の OnPropertyChanged 通知へ集約する責務を持つ。ケースを別関数へ出すと、Draw の
// 本文、switch の break、共通状態の受け渡しを変更するため、関数本文不変更の条件と両立しない。
#include "PropertyDrawer.h"

#include "../../Assets/AssetDatabase.h"
#include "../../Components/Physics/ColliderComponent.h"
#include "../../Object/Component/Component.h"
#include "../../Object/GameObject/GameObject.h"
#include "../../Object/Registry/ComponentRegistry.h"
#include "../../Physics/CollisionLayers.h"
#include "../../Reflection/Property/PropertyDesc.h"
#include "../../Reflection/Registry/PropertyRegistry.h"
#include "../../Rendering/Materials/MaterialAsset.h"
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

#include "PropertyDrawerInternal.h"

namespace
{
    bool IsUnlitOrFlatFillShadingModel(int shading_model) noexcept
    {
        return shading_model == 3 || shading_model == 5;
    }

    bool IsUnlitOrFlatFillMaterial(const ReplayEngine::Assets::AssetDatabase* assets,
        const std::string& guid)
    {
        if (assets == nullptr || guid.empty()) return false;
        const ReplayEngine::Assets::AssetRecord* record = assets->FindByGuid(guid);
        if (record == nullptr || record->kind != ReplayEngine::Assets::AssetKind::Material)
            return false;

        ReplayEngine::Rendering::MaterialAsset material;
        std::string error;
        if (!ReplayEngine::Rendering::MaterialAsset::Load(record->source_path, material, error))
            return false;
        return IsUnlitOrFlatFillShadingModel(material.shading_model);
    }

    bool ShouldDisableShadowToggles(const ReplayEngine::Core::Component& component,
        const ReplayEngine::Reflection::PropertyDesc& desc,
        const ReplayEngine::Assets::AssetDatabase* assets)
    {
        if (desc.name != "cast_shadow" && desc.name != "receive_shadow") return false;

        bool has_material = false;
        bool all_materials_unlit = true;
        const auto inspect_material = [&](const std::string& guid)
        {
            if (guid.empty()) return;
            has_material = true;
            if (!IsUnlitOrFlatFillMaterial(assets, guid)) all_materials_unlit = false;
        };

        if (const auto* material_desc =
            ReplayEngine::Reflection::PropertyRegistry::Find(component.TypeID(),
                "material_asset"))
        {
            inspect_material(material_desc->Capture(component).AsString());
        }

        if (const auto* dynamic = component.DynamicProperties())
        {
            for (const auto& dynamic_desc : *dynamic)
            {
                const std::string prefix = "material_slots[";
                const std::string suffix = "].asset";
                if (dynamic_desc.name.rfind(prefix, 0) != 0 ||
                    dynamic_desc.name.size() <= suffix.size() ||
                    dynamic_desc.name.compare(dynamic_desc.name.size() - suffix.size(),
                        suffix.size(), suffix) != 0)
                    continue;
                inspect_material(dynamic_desc.Capture(component).AsString());
            }
        }

        if (has_material) return all_materials_unlit;

        if (const auto* shading_desc =
            ReplayEngine::Reflection::PropertyRegistry::Find(component.TypeID(),
                "shading_model"))
        {
            return IsUnlitOrFlatFillShadingModel(shading_desc->Capture(component).AsInt(-1));
        }
        return false;
    }
}

namespace ReplayEngine::Editor
{
    using Reflection::PropertyDesc;
    using Reflection::PropertyRegistry;
    using Reflection::PropertyType;
    using Reflection::PropertyValue;
    using namespace Detail;

    bool PropertyDrawer::DrawAll(Core::Component& component,
        const Assets::AssetDatabase* assets, const Scene::Scene* scene)
    {
        bool changed = false;
        const auto draw_properties = [&](const std::vector<PropertyDesc>& properties,
                                         bool dynamic_properties)
        {
            const bool material_dynamic_disabled = dynamic_properties &&
                MaterialDynamicPropertiesDisabled(component);
            std::string active_category;
            bool category_open = false;
            bool category_pushed = false;
            const auto close_category = [&]()
            {
                if (category_open) ImGui::TreePop();
                category_open = false;
                if (category_pushed) ImGui::PopID();
                category_pushed = false;
            };

            for (const PropertyDesc& desc : properties)
            {
                if (desc.category != active_category)
                {
                    close_category();
                    active_category = desc.category;
                    if (!active_category.empty())
                    {
                        ImGui::PushID(active_category.c_str());
                        category_pushed = true;
                        category_open = ImGui::TreeNodeEx(active_category.c_str(), 0);
                    }
                }
                if (!active_category.empty() && !category_open) continue;

                ImGui::PushID(desc.name.c_str());
                if (dynamic_properties)
                {
                    const DisabledScope disabled(material_dynamic_disabled &&
                        IsMaterialDynamicProperty(desc));
                    if (Draw(desc, component, assets, scene)) changed = true;
                }
                else
                {
                    const bool shadow_toggle_disabled =
                        ShouldDisableShadowToggles(component, desc, assets);
                    PropertyDesc draw_desc = desc;
                    draw_desc.read_only = draw_desc.read_only || shadow_toggle_disabled;
                    if (Draw(draw_desc, component, assets, scene)) changed = true;
                    if (shadow_toggle_disabled && desc.name == "receive_shadow")
                        ImGui::TextDisabled(u8"Unlit / Flat Fill は照明と影を使わないため編集できません");
                }
                ImGui::PopID();
            }
            close_category();
        };

        draw_properties(PropertyRegistry::PropertiesOf(component.TypeID()), false);

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
            draw_properties(*dynamic, true);
        }
        return changed;
    }
}
