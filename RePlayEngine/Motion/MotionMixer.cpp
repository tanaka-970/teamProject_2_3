#include "MotionMixer.h"

#include "../Object/Component/Component.h"

#include <algorithm>
#include <functional>

namespace ReplayEngine::Motion
{
    std::size_t MotionMixer::TargetKeyHash::operator()(const TargetKey& key) const noexcept
    {
        const std::size_t a = std::hash<void*>{}(key.component);
        const std::size_t b = std::hash<std::string>{}(key.property);
        return a ^ (b + 0x9e3779b97f4a7c15ull + (a << 6) + (a >> 2));
    }

    void MotionMixer::BeginFrame()
    {
        accumulators_.clear();
    }

    void MotionMixer::Contribute(const ResolvedMotionBinding& binding,
        const Reflection::PropertyValue& value, float weight)
    {
        if (!binding.Valid() || weight <= 0.0f) return;
        if (binding.property->animatable == Reflection::Animatable::None) return;

        TargetKey key{ binding.component, binding.property->name };
        auto found = accumulators_.find(key);
        if (found == accumulators_.end())
        {
            Accumulator accumulator;
            accumulator.component = binding.component;
            accumulator.property = binding.property;
            accumulator.value = value;
            accumulator.total_weight = weight;
            accumulators_.emplace(std::move(key), std::move(accumulator));
            return;
        }

        Accumulator& accumulator = found->second;
        if (binding.property->animatable == Reflection::Animatable::Step)
        {
            if (weight >= accumulator.total_weight)
            {
                accumulator.value = value;
            }
            accumulator.total_weight += weight;
            return;
        }

        const float total = accumulator.total_weight + weight;
        if (total <= 0.0f) return;

        const float ratio = weight / total;
        accumulator.value =
            Reflection::PropertyValue::Lerp(accumulator.value, value, ratio);
        accumulator.total_weight = total;
    }

    void MotionMixer::Apply()
    {
        for (auto& pair : accumulators_)
        {
            Accumulator& accumulator = pair.second;
            if (accumulator.component == nullptr || accumulator.property == nullptr ||
                accumulator.total_weight <= 0.0f)
            {
                continue;
            }

            accumulator.property->Apply(*accumulator.component, accumulator.value);
        }
    }
}
