#pragma once

#include "MotionAsset.h"

#include <cstdint>
#include <string>

namespace ReplayEngine::Motion
{
    class MotionExpressionEvaluator final
    {
    public:
        static bool SupportsType(Reflection::PropertyType type) noexcept;
        static bool Validate(const std::string& source, std::string& error);
        static bool Apply(const MotionExpression& expression,
            Reflection::PropertyValue& value, float time, float raw_time,
            float duration, std::string* error = nullptr);

        static float ValueNoise(float time, float frequency, int seed,
            std::uint32_t channel) noexcept;
        static float WiggleNoise(const MotionWiggle& wiggle, float time,
            std::uint32_t channel) noexcept;
    };
}
