#pragma once

#include "../../Object/Component/Component.h"
#include "../../Reflection/Property/PropertyDesc.h"
#include "../../UI/Effects/UIEffect.h"

#include <DirectXMath.h>

#include <vector>

namespace ReplayEngine::Components
{
    class UIEffectStackComponent final : public Core::Component
    {
        REPLAY_COMPONENT_BODY(UIEffectStackComponent)

    public:
        UIEffectStackComponent();

        const std::vector<Reflection::PropertyDesc>* DynamicProperties()
            const noexcept override;
        void OnSerialize(Reflection::PropertyBag& output) const override;
        void OnDeserialize(const Reflection::PropertyBag& input) override;
        void OnPropertyChanged(const char* property_name) override;

        bool HasActiveEffects() const noexcept;
        DirectX::XMFLOAT4 ExpandBounds() const noexcept;

        bool enabled = true;
        int effect_count = 0;
        std::vector<UI::UIEffect> effects;

    private:
        void ResizeEffects();
        void RebuildDynamicProperties();

        std::vector<Reflection::PropertyDesc> dynamic_properties_;
    };
}
