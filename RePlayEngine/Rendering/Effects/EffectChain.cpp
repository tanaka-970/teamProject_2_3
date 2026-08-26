#include "EffectChain.h"

#include <algorithm>

namespace ReplayEngine::Rendering::Effects
{
    DirectX::XMFLOAT4 EffectChain::ExpandBounds(const std::vector<UI::UIEffect>& effects,
        float target_width, float target_height) noexcept
    {
        DirectX::XMFLOAT4 expansion{ 0.0f, 0.0f, 0.0f, 0.0f };
        for (const UI::UIEffect& effect : effects)
        {
            if (!effect.enabled) continue;
            const float current_width = target_width + expansion.x + expansion.z;
            const float current_height = target_height + expansion.y + expansion.w;
            const DirectX::XMFLOAT4 current = effect.ExpandBounds(current_width, current_height);
            expansion.x += current.x;
            expansion.y += current.y;
            expansion.z += current.z;
            expansion.w += current.w;
        }

        constexpr float max_expansion = 2048.0f;
        expansion.x = (std::min)(expansion.x, max_expansion);
        expansion.y = (std::min)(expansion.y, max_expansion);
        expansion.z = (std::min)(expansion.z, max_expansion);
        expansion.w = (std::min)(expansion.w, max_expansion);
        return expansion;
    }
}
