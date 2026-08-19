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
