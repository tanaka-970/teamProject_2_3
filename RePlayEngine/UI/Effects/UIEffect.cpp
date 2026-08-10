#include "UIEffect.h"

#include <algorithm>
#include <cmath>

namespace ReplayEngine::UI
{
    DirectX::XMFLOAT4 UIEffect::ExpandBounds() const noexcept
    {
        if (!enabled) return { 0.0f, 0.0f, 0.0f, 0.0f };

        const UIEffectKind effect_kind = static_cast<UIEffectKind>(kind);
        const float safe_radius = (std::max)(0.0f, radius);
        const float safe_amount = std::fabs(amount);
        switch (effect_kind)
        {
        case UIEffectKind::Blur:
        case UIEffectKind::Glow:
            return { safe_radius, safe_radius, safe_radius, safe_radius };
        case UIEffectKind::DropShadow:
        {
            const float dx = direction.x * safe_amount;
            const float dy = direction.y * safe_amount;
            return {
                safe_radius + (std::max)(0.0f, -dx),
                safe_radius + (std::max)(0.0f, -dy),
                safe_radius + (std::max)(0.0f, dx),
                safe_radius + (std::max)(0.0f, dy) };
        }
        case UIEffectKind::Shake:
        case UIEffectKind::Distortion:
        case UIEffectKind::ChromaticAberration:
            return { safe_amount, safe_amount, safe_amount, safe_amount };
        default:
            return { 0.0f, 0.0f, 0.0f, 0.0f };
        }
    }
}
