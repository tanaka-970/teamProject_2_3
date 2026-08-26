#include "UIMaskComponent.h"

#include "RectTransformComponent.h"
#include "../../Object/GameObject/GameObject.h"
#include "../../Reflection/Property/PropertyValue.h"

#include <algorithm>
#include <cstdio>
#include <string>

namespace ReplayEngine::Components
{
    void UIMaskComponent::OnAttach()
    {
        if (Core::GameObject* owner = Owner())
        {
            owner->AddComponent<RectTransformComponent>();
        }
    }

    void UIMaskComponent::NormalizeMatteOperations()
    {
        matte_operations.resize(matte_objects.size(), MatteAdd);
        for (int& operation : matte_operations)
            operation = (std::max)(static_cast<int>(MatteAdd),
                (std::min)(static_cast<int>(MatteIntersect), operation));
    }

    void UIMaskComponent::OnPropertyChanged(const char*)
    {
        NormalizeMatteOperations();
        RebuildDynamicProperties();
    }

    const std::vector<Reflection::PropertyDesc>*
        UIMaskComponent::DynamicProperties() const noexcept
    {
        const_cast<UIMaskComponent*>(this)->NormalizeMatteOperations();
        RebuildDynamicProperties();
        return dynamic_properties_.empty() ? nullptr : &dynamic_properties_;
    }

    void UIMaskComponent::RebuildDynamicProperties() const
    {
        dynamic_properties_.clear();
        dynamic_properties_.reserve(matte_objects.size());
        for (std::size_t i = 0; i < matte_objects.size(); ++i)
        {
            const int index = static_cast<int>(i);
            char name[64]{};
            std::snprintf(name, sizeof(name), "matte[%d].operation", index);
            Reflection::PropertyDesc property;
            property.name = name;
            property.display_name = "演算";
            property.category = "Track Matte " + std::to_string(index + 2);
            property.type = Reflection::PropertyType::Enum;
            property.enum_labels = { "Add", "Subtract", "Intersect" };
            property.animatable = Reflection::Animatable::Step;
            property.serializable = false;
            property.getter = [index](const Core::Component& component)
            {
                const auto& mask = static_cast<const UIMaskComponent&>(component);
                if (index < 0 || static_cast<std::size_t>(index) >= mask.matte_operations.size())
                    return Reflection::PropertyValue::MakeEnum(MatteAdd);
                return Reflection::PropertyValue::MakeEnum(
                    mask.matte_operations[static_cast<std::size_t>(index)]);
            };
            property.setter = [index](Core::Component& component,
                const Reflection::PropertyValue& value)
            {
                auto& mask = static_cast<UIMaskComponent&>(component);
                if (index < 0 || static_cast<std::size_t>(index) >= mask.matte_operations.size())
                    return;
                mask.matte_operations[static_cast<std::size_t>(index)] =
                    (std::max)(static_cast<int>(MatteAdd),
                        (std::min)(static_cast<int>(MatteIntersect), value.AsInt()));
            };
            dynamic_properties_.push_back(std::move(property));
        }
    }
}
