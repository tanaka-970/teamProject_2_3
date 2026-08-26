#pragma once

#include <DirectXMath.h>
#include "../../UI/Effects/UIEffect.h"

#include <vector>

namespace ReplayEngine::Rendering::Effects
{
    class EffectChain final
    {
    public:
        static DirectX::XMFLOAT4 ExpandBounds(const std::vector<UI::UIEffect>& effects,
            float target_width, float target_height) noexcept;
    };
}
