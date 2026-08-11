#pragma once

#include "../../Object/Component/Component.h"
#include "../../Reflection/Property/PropertyDesc.h"
#include "../../Rendering/Shaders/ShaderAsset.h"
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

        // ShaderCatalog の Schema を同期し、Custom Effect の Property を
        // DynamicProperties() へ追加する。GPU や AssetDatabase は保持しない。
        void SetCustomShaderSchema(std::size_t effect_index,
            Rendering::ShaderPropertySchemaRef schema);

        bool enabled = true;
        int effect_count = 0;
        std::vector<UI::UIEffect> effects;

    private:
        void ResizeEffects();
        void RebuildDynamicProperties();

        std::vector<Reflection::PropertyDesc> dynamic_properties_;
        std::vector<Rendering::ShaderPropertySchemaRef> custom_schemas_;
    };
}
