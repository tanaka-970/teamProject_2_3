#pragma once

#include "../../Object/Component/Component.h"
#include "../../Reflection/Property/PropertyDesc.h"
#include "../../Rendering/Shaders/ShaderAsset.h"
#include "../../UI/Effects/UIEffect.h"

#include <DirectXMath.h>

#include <vector>

namespace ReplayEngine::Assets { class AssetDatabase; }

namespace ReplayEngine::Components
{
    class ScreenEffectStackComponent final : public Core::Component
    {
        REPLAY_COMPONENT_BODY(ScreenEffectStackComponent)

    public:
        ScreenEffectStackComponent();

        const std::vector<Reflection::PropertyDesc>* DynamicProperties()
            const noexcept override;
        void OnSerialize(Reflection::PropertyBag& output) const override;
        void OnDeserialize(const Reflection::PropertyBag& input) override;
        void OnPropertyChanged(const char* property_name) override;

        bool HasActiveEffects() const noexcept;
        bool HasActiveEffects(const Assets::AssetDatabase* database) const noexcept;
        DirectX::XMFLOAT4 ExpandBounds(float target_width,
            float target_height) const noexcept;
        DirectX::XMFLOAT4 ExpandBounds(float target_width, float target_height,
            const Assets::AssetDatabase* database) const noexcept;
        const std::vector<UI::UIEffect>& EffectiveEffects(
            const Assets::AssetDatabase* database) const noexcept;

        // ShaderCatalog の Schema を同期し、Custom Effect の Property を
        // DynamicProperties() へ追加する。GPU や AssetDatabase は保持しない。
        void SetCustomShaderSchema(std::size_t effect_index,
            Rendering::ShaderPropertySchemaRef schema);

        bool enabled = true;
        // false の既存 Scene は inline 値をそのまま使う。Preset 参照は追加の選択肢。
        bool use_preset = false;
        Reflection::AssetReference effect_preset;
        int effect_count = 0;
        std::vector<UI::UIEffect> effects;

        enum ApplyStage : int
        {
            AfterPostProcess = 0,
            BeforePostProcess = 1,
        };

        // 既定は PostProcess 後。UI 描画より前に適用される。
        int apply_stage = AfterPostProcess;

    private:
        void ResizeEffects();
        void RebuildDynamicProperties();

        std::vector<Reflection::PropertyDesc> dynamic_properties_;
        std::vector<Rendering::ShaderPropertySchemaRef> custom_schemas_;
    };
}
