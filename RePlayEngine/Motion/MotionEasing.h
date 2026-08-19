#pragma once

#include <DirectXMath.h>

namespace ReplayEngine::Motion
{
    enum class MotionEasing
    {
        Linear,
        Step,
        EaseInQuad,
        EaseOutQuad,
        EaseInOutQuad,
        EaseInCubic,
        EaseOutCubic,
        EaseInOutCubic,
        EaseInBack,
        EaseOutBack,
        EaseInOutBack,
        EaseInElastic,
        EaseOutElastic,
        EaseInOutElastic,
        CustomBezier,
    };

    struct MotionBezierHandles
    {
        DirectX::XMFLOAT2 out_handle{ 0.25f, 0.0f };
        DirectX::XMFLOAT2 in_handle{ 0.75f, 1.0f };
    };

    const char* ToString(MotionEasing easing) noexcept;
    bool TryParseMotionEasing(const char* text, MotionEasing& out) noexcept;

    // t の入力範囲だけ 0..1 に丸める。Back / Elastic は意図的に 0..1 外へ出るため、
    // 返り値は丸めない。
    float ApplyEasing(MotionEasing easing, float t,
        const MotionBezierHandles& handles = {}) noexcept;
}
