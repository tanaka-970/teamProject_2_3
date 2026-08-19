#include "UITextComponent.h"

#include "RectTransformComponent.h"
#include "../../Object/GameObject/GameObject.h"
#include "../../Reflection/Property/PropertyDesc.h"
#include "../../Reflection/Registry/PropertyRegistry.h"
#include "../../Scene/Runtime/Scene.h"
#include "../../Localization/LocalizationService.h"

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace ReplayEngine::Components
{
    void UITextComponent::OnAttach()
    {
        if (Core::GameObject* owner = Owner())
        {
            owner->AddComponent<RectTransformComponent>();
        }
    }

    std::string UITextComponent::ResolvedText() const
    {
        if (localization_key.empty()) return text;
        return Localization::LocalizationService::Global().Resolve(localization_key, text);
    }

    void UITextComponent::UpdateNumberDisplay(const ReplayEngine::Scene::Scene& scene)
    {
        if (!number_source.IsAssigned() || number_source_property.empty()) return;

        Core::GameObject* owner = scene.FindGameObjectByID(number_source.owner);
        if (owner == nullptr || owner->PendingDestroy()) return;
        Core::Component* source = owner->FindComponentByStableID(number_source.component);
        if (source == nullptr || source->PendingDestroy()) return;

        const Reflection::PropertyDesc* property =
            Reflection::PropertyRegistry::Find(source->TypeID(), number_source_property);
        if (property == nullptr)
        {
            const std::vector<Reflection::PropertyDesc>* dynamic =
                source->DynamicProperties();
            if (dynamic != nullptr)
            {
                for (const Reflection::PropertyDesc& candidate : *dynamic)
                {
                    if (candidate.name == number_source_property)
                    {
                        property = &candidate;
                        break;
                    }
                }
            }
        }
        if (property == nullptr) return;

        const Reflection::PropertyType type = property->type;
        const bool numeric = type == Reflection::PropertyType::Int ||
            type == Reflection::PropertyType::Float ||
            type == Reflection::PropertyType::Double ||
            type == Reflection::PropertyType::Enum ||
            type == Reflection::PropertyType::Int64 ||
            type == Reflection::PropertyType::UInt64;
        if (!numeric) return;

        const double value = property->Capture(*source).AsDouble(0.0);
        const std::string format = number_format.empty() ? "{0}" : number_format;
        const std::size_t token = format.find("{0");
        if (token == std::string::npos) return;

        const std::size_t close = format.find('}', token);
        if (close == std::string::npos) return;

        // 桁数は number_format の書式文字列へ分散させず、Scene の 1 プロパティで
        // 0..4 に制限する。壊れた保存値が来ても既定値へ倒し、例外を投げない。
        const int precision = (std::max)(0, (std::min)(4, number_digits));

        std::ostringstream number;
        number << std::fixed << std::setprecision(precision) << value;
        text = format.substr(0, token) + number.str() + format.substr(close + 1);
    }
}
