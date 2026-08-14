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
        case UIEffectKind::Outline:
            return { safe_radius, safe_radius, safe_radius, safe_radius };
        case UIEffectKind::Shake:
        case UIEffectKind::Distortion:
        case UIEffectKind::ChromaticAberration:
        case UIEffectKind::DirectionalBlur:
        case UIEffectKind::RadialBlur:
        case UIEffectKind::LongShadow:
        case UIEffectKind::Ripple:
            return { safe_amount, safe_amount, safe_amount, safe_amount };
        case UIEffectKind::Waveform:
        {
            // 基本波は -1..1 へ正規化してあるので、変位の最大は 振幅 × (1 + うねり) で確定する。
            // 太さ変調は細らせる向きにしか効かないため、ここへ足す必要はない。
            // 末尾の 2.0f は RT の丸めと Canvas 拡大率ぶんの余裕。
            const float wobble = (std::max)(0.0f, progress);
            const float expansion = safe_amount * (1.0f + wobble) + 2.0f;
            return { expansion, expansion, expansion, expansion };
        }
        case UIEffectKind::Glitch:
        {
            const float expansion = (std::max)(safe_amount, std::fabs(intensity));
            return { expansion, expansion, expansion, expansion };
        }
        case UIEffectKind::VHS:
        {
            const float expansion = (std::max)(safe_radius,
                (std::max)(safe_amount, std::fabs(threshold)));
            return { expansion, expansion, expansion, expansion };
        }
        case UIEffectKind::LightStreaks:
            return { safe_radius, safe_radius, safe_radius, safe_radius };
        default:
            return { 0.0f, 0.0f, 0.0f, 0.0f };
        }
    }
}
