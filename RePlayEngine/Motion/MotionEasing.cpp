#include "MotionEasing.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace ReplayEngine::Motion
{
    namespace
    {
        constexpr float pi = 3.14159265358979323846f;

        bool Equals(const char* lhs, const char* rhs) noexcept
        {
            return std::strcmp(lhs, rhs) == 0;
        }

        float CubicBezier(float p0, float p1, float p2, float p3, float t) noexcept
        {
            const float one_minus = 1.0f - t;
            return one_minus * one_minus * one_minus * p0 +
                3.0f * one_minus * one_minus * t * p1 +
                3.0f * one_minus * t * t * p2 +
                t * t * t * p3;
        }

        float CubicBezierDerivative(float p0, float p1, float p2, float p3,
            float t) noexcept
        {
            const float one_minus = 1.0f - t;
            return 3.0f * one_minus * one_minus * (p1 - p0) +
                6.0f * one_minus * t * (p2 - p1) +
                3.0f * t * t * (p3 - p2);
        }

        float EvaluateBezier(float t, const MotionBezierHandles& handles) noexcept
        {
            float u = t;
            for (int i = 0; i < 6; ++i)
            {
                const float x = CubicBezier(0.0f, handles.out_handle.x,
                    handles.in_handle.x, 1.0f, u);
                const float dx = CubicBezierDerivative(0.0f,
                    handles.out_handle.x, handles.in_handle.x, 1.0f, u);
                if (std::fabs(dx) < 0.0001f) break;
                u -= (x - t) / dx;
                u = std::clamp(u, 0.0f, 1.0f);
            }
            return CubicBezier(0.0f, handles.out_handle.y,
                handles.in_handle.y, 1.0f, u);
        }
    }

    const char* ToString(MotionEasing easing) noexcept
    {
        switch (easing)
        {
        case MotionEasing::Linear: return "Linear";
        case MotionEasing::Step: return "Step";
        case MotionEasing::EaseInQuad: return "EaseInQuad";
        case MotionEasing::EaseOutQuad: return "EaseOutQuad";
        case MotionEasing::EaseInOutQuad: return "EaseInOutQuad";
        case MotionEasing::EaseInCubic: return "EaseInCubic";
        case MotionEasing::EaseOutCubic: return "EaseOutCubic";
        case MotionEasing::EaseInOutCubic: return "EaseInOutCubic";
        case MotionEasing::EaseInBack: return "EaseInBack";
        case MotionEasing::EaseOutBack: return "EaseOutBack";
        case MotionEasing::EaseInOutBack: return "EaseInOutBack";
        case MotionEasing::EaseInElastic: return "EaseInElastic";
        case MotionEasing::EaseOutElastic: return "EaseOutElastic";
        case MotionEasing::EaseInOutElastic: return "EaseInOutElastic";
        case MotionEasing::CustomBezier: return "CustomBezier";
        }
        return "Linear";
    }

    bool TryParseMotionEasing(const char* text, MotionEasing& out) noexcept
    {
        if (text == nullptr) return false;

        if (Equals(text, "Linear")) out = MotionEasing::Linear;
        else if (Equals(text, "Step")) out = MotionEasing::Step;
        else if (Equals(text, "EaseInQuad")) out = MotionEasing::EaseInQuad;
        else if (Equals(text, "EaseOutQuad")) out = MotionEasing::EaseOutQuad;
        else if (Equals(text, "EaseInOutQuad")) out = MotionEasing::EaseInOutQuad;
        else if (Equals(text, "EaseInCubic")) out = MotionEasing::EaseInCubic;
        else if (Equals(text, "EaseOutCubic")) out = MotionEasing::EaseOutCubic;
        else if (Equals(text, "EaseInOutCubic")) out = MotionEasing::EaseInOutCubic;
        else if (Equals(text, "EaseInBack")) out = MotionEasing::EaseInBack;
        else if (Equals(text, "EaseOutBack")) out = MotionEasing::EaseOutBack;
        else if (Equals(text, "EaseInOutBack")) out = MotionEasing::EaseInOutBack;
        else if (Equals(text, "EaseInElastic")) out = MotionEasing::EaseInElastic;
        else if (Equals(text, "EaseOutElastic")) out = MotionEasing::EaseOutElastic;
        else if (Equals(text, "EaseInOutElastic")) out = MotionEasing::EaseInOutElastic;
        else if (Equals(text, "CustomBezier")) out = MotionEasing::CustomBezier;
        else return false;

        return true;
    }

    float ApplyEasing(MotionEasing easing, float t,
        const MotionBezierHandles& handles) noexcept
    {
        t = std::clamp(t, 0.0f, 1.0f);
        if (easing == MotionEasing::Step) return 0.0f;

        switch (easing)
        {
        case MotionEasing::Linear:
            return t;
        case MotionEasing::EaseInQuad:
            return t * t;
        case MotionEasing::EaseOutQuad:
            return 1.0f - (1.0f - t) * (1.0f - t);
        case MotionEasing::EaseInOutQuad:
            return t < 0.5f
                ? 2.0f * t * t
                : 1.0f - std::pow(-2.0f * t + 2.0f, 2.0f) * 0.5f;
        case MotionEasing::EaseInCubic:
            return t * t * t;
        case MotionEasing::EaseOutCubic:
            return 1.0f - std::pow(1.0f - t, 3.0f);
        case MotionEasing::EaseInOutCubic:
            return t < 0.5f
                ? 4.0f * t * t * t
                : 1.0f - std::pow(-2.0f * t + 2.0f, 3.0f) * 0.5f;
        case MotionEasing::EaseInBack:
        {
            constexpr float c1 = 1.70158f;
            constexpr float c3 = c1 + 1.0f;
            return c3 * t * t * t - c1 * t * t;
        }
        case MotionEasing::EaseOutBack:
        {
            constexpr float c1 = 1.70158f;
            constexpr float c3 = c1 + 1.0f;
            const float u = t - 1.0f;
            return 1.0f + c3 * u * u * u + c1 * u * u;
        }
        case MotionEasing::EaseInOutBack:
        {
            constexpr float c1 = 1.70158f;
            constexpr float c2 = c1 * 1.525f;
            return t < 0.5f
                ? (std::pow(2.0f * t, 2.0f) *
                    ((c2 + 1.0f) * 2.0f * t - c2)) * 0.5f
                : (std::pow(2.0f * t - 2.0f, 2.0f) *
                    ((c2 + 1.0f) * (t * 2.0f - 2.0f) + c2) + 2.0f) * 0.5f;
        }
        case MotionEasing::EaseInElastic:
            if (t == 0.0f || t == 1.0f) return t;
            return -std::pow(2.0f, 10.0f * t - 10.0f) *
                std::sin((t * 10.0f - 10.75f) * (2.0f * pi / 3.0f));
        case MotionEasing::EaseOutElastic:
            if (t == 0.0f || t == 1.0f) return t;
            return std::pow(2.0f, -10.0f * t) *
                std::sin((t * 10.0f - 0.75f) * (2.0f * pi / 3.0f)) + 1.0f;
        case MotionEasing::EaseInOutElastic:
            if (t == 0.0f || t == 1.0f) return t;
            return t < 0.5f
                ? -(std::pow(2.0f, 20.0f * t - 10.0f) *
                    std::sin((20.0f * t - 11.125f) * (2.0f * pi / 4.5f))) * 0.5f
                : (std::pow(2.0f, -20.0f * t + 10.0f) *
                    std::sin((20.0f * t - 11.125f) * (2.0f * pi / 4.5f))) * 0.5f + 1.0f;
        case MotionEasing::CustomBezier:
            return EvaluateBezier(t, handles);
        case MotionEasing::Step:
            break;
        }

        return t;
    }
}
