#pragma once

#include "../../Reflection/Property/PropertyDesc.h"
#include "../../Reflection/Property/PropertyValue.h"

#include <vector>

namespace ReplayEngine::Components
{
    template<class T>
    const std::vector<Reflection::PropertyDesc>*
        MaterialOverrideDynamicProperties()
    {
        using Reflection::Animatable;
        using Reflection::PropertyDesc;
        using Reflection::PropertyType;
        using Reflection::PropertyValue;

        static const std::vector<PropertyDesc> properties = []
        {
            std::vector<PropertyDesc> result;
            result.reserve(7);

            auto push_float = [&](const char* name, float T::* member,
                const char* display, const char* tooltip,
                double minimum, double maximum)
            {
                PropertyDesc desc;
                desc.name = name;
                desc.display_name = display;
                desc.tooltip = tooltip;
                desc.type = PropertyType::Float;
                desc.animatable = Animatable::Interpolatable;
                desc.has_range = true;
                desc.minimum = minimum;
                desc.maximum = maximum;
                desc.getter = [member](const Core::Component& component)
                {
                    if (component.TypeID() != T::StaticTypeID()) return PropertyValue{};
                    return PropertyValue::MakeFloat(static_cast<const T&>(component).*member);
                };
                desc.setter = [member](Core::Component& component,
                    const PropertyValue& value)
                {
                    if (component.TypeID() != T::StaticTypeID()) return;
                    static_cast<T&>(component).*member =
                        value.AsFloat(static_cast<T&>(component).*member);
                };
                result.push_back(desc);
            };

            auto push_color = [&](const char* name,
                DirectX::XMFLOAT4 T::* member, const char* display,
                const char* tooltip)
            {
                PropertyDesc desc;
                desc.name = name;
                desc.display_name = display;
                desc.tooltip = tooltip;
                desc.type = PropertyType::Color;
                desc.animatable = Animatable::Interpolatable;
                desc.getter = [member](const Core::Component& component)
                {
                    if (component.TypeID() != T::StaticTypeID()) return PropertyValue{};
                    return PropertyValue::MakeColor(static_cast<const T&>(component).*member);
                };
                desc.setter = [member](Core::Component& component,
                    const PropertyValue& value)
                {
                    if (component.TypeID() != T::StaticTypeID()) return;
                    static_cast<T&>(component).*member = value.AsVector4();
                };
                result.push_back(desc);
            };

            auto push_vector3 = [&](const char* name,
                DirectX::XMFLOAT3 T::* member, const char* display,
                const char* tooltip)
            {
                PropertyDesc desc;
                desc.name = name;
                desc.display_name = display;
                desc.tooltip = tooltip;
                desc.type = PropertyType::Vector3;
                desc.animatable = Animatable::Interpolatable;
                desc.getter = [member](const Core::Component& component)
                {
                    if (component.TypeID() != T::StaticTypeID()) return PropertyValue{};
                    return PropertyValue::MakeVector3(static_cast<const T&>(component).*member);
                };
                desc.setter = [member](Core::Component& component,
                    const PropertyValue& value)
                {
                    if (component.TypeID() != T::StaticTypeID()) return;
                    static_cast<T&>(component).*member = value.AsVector3();
                };
                result.push_back(desc);
            };

            auto push_bool = [&](const char* name, bool T::* member,
                const char* display, const char* tooltip)
            {
                PropertyDesc desc;
                desc.name = name;
                desc.display_name = display;
                desc.tooltip = tooltip;
                desc.type = PropertyType::Bool;
                desc.animatable = Animatable::Step;
                desc.getter = [member](const Core::Component& component)
                {
                    if (component.TypeID() != T::StaticTypeID()) return PropertyValue{};
                    return PropertyValue::MakeBool(static_cast<const T&>(component).*member);
                };
                desc.setter = [member](Core::Component& component,
                    const PropertyValue& value)
                {
                    if (component.TypeID() != T::StaticTypeID()) return;
                    static_cast<T&>(component).*member =
                        value.AsBool(static_cast<T&>(component).*member);
                };
                result.push_back(desc);
            };

            push_color("material.base_color", &T::material_base_color,
                "マテリアル色", "Material Asset を置き換えるベース色。");
            push_float("material.metallic", &T::material_metallic,
                "金属度", "GBuffer へ渡す金属度。", 0.0, 1.0);
            push_float("material.roughness", &T::material_roughness,
                "粗さ", "GBuffer へ渡すラフネス。", 0.0, 1.0);
            push_float("material.ambient_occlusion",
                &T::material_ambient_occlusion,
                "環境遮蔽", "GBuffer へ渡す AO 係数。", 0.0, 1.0);
            push_vector3("material.emissive_color", &T::material_emissive_color,
                "発光色", "GBuffer へ渡す発光色。");
            push_float("material.emissive_strength",
                &T::material_emissive_strength,
                "発光強度", "GBuffer へ渡す発光強度。", 0.0, 16.0);
            push_bool("material.double_sided", &T::material_double_sided,
                "両面描画", "Material override 時だけ両面描画を有効にする。");

            return result;
        }();

        return &properties;
    }
}
