#pragma once

#include "MotionBindingResolver.h"

#include <string>
#include <unordered_map>

namespace ReplayEngine::Motion
{
    class MotionMixer final
    {
    public:
        struct Contribution
        {
            Reflection::PropertyValue value;
            float weight = 0.0f;
            MotionBlendMode mode = MotionBlendMode::Override;
        };

        void BeginFrame();
        void Contribute(const ResolvedMotionBinding& binding,
            const Reflection::PropertyValue& value, float weight,
            MotionBlendMode mode = MotionBlendMode::Override);
        void Apply();
        bool WasDriven(const Core::Component& component,
            const std::string& property) const noexcept;

    private:
        struct TargetKey
        {
            Core::Component* component = nullptr;
            std::string property;

            bool operator==(const TargetKey& other) const noexcept
            {
                return component == other.component && property == other.property;
            }
        };

        struct TargetKeyHash
        {
            std::size_t operator()(const TargetKey& key) const noexcept;
        };

        struct Accumulator
        {
            Core::Component* component = nullptr;
            const Reflection::PropertyDesc* property = nullptr;
            Reflection::PropertyValue base_value;
            std::vector<Contribution> contributions;
            float total_weight = 0.0f;
        };

        std::unordered_map<TargetKey, Accumulator, TargetKeyHash> accumulators_;
    };
}
