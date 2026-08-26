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
    class UIEffectStackComponent final : public Core::Component
    {
        REPLAY_COMPONENT_BODY(UIEffectStackComponent)

    public:
        enum TargetScope : int
        {
            Self = 0,
            Subtree = 1,
        };

        UIEffectStackComponent();

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
        // Self は従来互換。Subtree はこの GameObject と全子孫を 1 枚に Precompose してから
        // Effect Stack を 1 回適用する。
        int target_scope = Self;
        // Screen Space Overlay の Image / Text だけ、直前まで描かれた画素を
        // Effect の入力へ含める。既定 false は従来の offscreen 経路を保つ。
        bool capture_backdrop = false;
        // Stack 全体へ掛ける範囲制限。shape=TextureMask なら投げ縄などの
        // 白黒画像を指定でき、invert で範囲外だけへ反転できる。
        UI::UIEffectRegion effect_region;
        // false の既存 Scene は inline 値をそのまま使う。Preset 参照は追加の選択肢。
        bool use_preset = false;
        Reflection::AssetReference effect_preset;
        int effect_count = 0;
        std::vector<UI::UIEffect> effects;

    private:
        void ResizeEffects();
        void RebuildDynamicProperties();

        std::vector<Reflection::PropertyDesc> dynamic_properties_;
        std::vector<Rendering::ShaderPropertySchemaRef> custom_schemas_;
    };
}
