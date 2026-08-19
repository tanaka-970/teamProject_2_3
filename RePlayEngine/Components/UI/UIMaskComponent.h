#pragma once

#include "../../Object/Component/Component.h"
#include "../../Reflection/Property/References.h"

namespace ReplayEngine::Components
{
    class UIMaskComponent final : public Core::Component
    {
        REPLAY_COMPONENT_BODY(UIMaskComponent)

    public:
        enum MaskMode : int
        {
            Rectangle = 0,
            Image = 1,
            Shape = 2,
        };

        UIMaskComponent() = default;

        void OnAttach() override;

        bool enabled_mask = true;
        bool show_mask_graphic = true;
        int mask_mode = Rectangle;
        Reflection::AssetReference mask_image;
        float softness = 0.0f;

        // Rectangle は従来どおり RectTransform の resolved_rect を D3D11 scissor に渡す。
        // Image / Shape は既存 Effect Stack の Mask pass 用 RT へ逃がし、別の描画経路や
        // 新しい GPU リソース所有者を増やさない。softness は shader 側の境界幅に使う。
    };
}
