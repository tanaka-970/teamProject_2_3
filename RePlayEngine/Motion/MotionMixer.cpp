#include "MotionMixer.h"

#include "../Object/Component/Component.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

namespace ReplayEngine::Motion
{
    namespace
    {
        constexpr float zero_epsilon = 0.00001f;

        float BlendScalar(float current, float base, float value,
            float weight, MotionBlendMode mode) noexcept
        {
            const float t = (std::max)(0.0f, (std::min)(1.0f, weight));
            if (mode == MotionBlendMode::Additive)
            {
                return current + (value - base) * t;
            }
            if (mode == MotionBlendMode::Multiply)
            {
                if (std::fabs(base) <= zero_epsilon)
                {
                    return current + (value - current) * t;
                }
                return current * (1.0f + ((value / base) - 1.0f) * t);
            }
            return Reflection::PropertyValue::Lerp(
                Reflection::PropertyValue::MakeFloat(current),
                Reflection::PropertyValue::MakeFloat(value), t).AsFloat(current);
        }

        Reflection::PropertyValue AdditiveOrMultiply(
            const Reflection::PropertyValue& current,
            const Reflection::PropertyValue& base,
            const Reflection::PropertyValue& value,
            float weight,
            MotionBlendMode mode)
        {
            using Reflection::PropertyType;
            if (current.Type() != base.Type() || current.Type() != value.Type())
            {
                return current;
            }

            switch (current.Type())
            {
            case PropertyType::Float:
                return Reflection::PropertyValue::MakeFloat(BlendScalar(
                    current.AsFloat(), base.AsFloat(), value.AsFloat(), weight, mode));
            case PropertyType::Double:
                return Reflection::PropertyValue::MakeDouble(static_cast<double>(BlendScalar(
                    static_cast<float>(current.AsDouble()),
                    static_cast<float>(base.AsDouble()),
                    static_cast<float>(value.AsDouble()), weight, mode)));
            case PropertyType::Int:
            case PropertyType::Enum:
            {
                const float blended = BlendScalar(static_cast<float>(current.AsInt()),
                    static_cast<float>(base.AsInt()), static_cast<float>(value.AsInt()),
                    weight, mode);
                if (current.Type() == PropertyType::Enum)
                    return Reflection::PropertyValue::MakeEnum(static_cast<int>(std::round(blended)));
                return Reflection::PropertyValue::MakeInt(static_cast<int>(std::round(blended)));
            }
            case PropertyType::Vector2:
            {
                const DirectX::XMFLOAT2 c = current.AsVector2();
                const DirectX::XMFLOAT2 b = base.AsVector2();
                const DirectX::XMFLOAT2 v = value.AsVector2();
                return Reflection::PropertyValue::MakeVector2({
                    BlendScalar(c.x, b.x, v.x, weight, mode),
                    BlendScalar(c.y, b.y, v.y, weight, mode) });
            }
            case PropertyType::Vector3:
            {
                const DirectX::XMFLOAT3 c = current.AsVector3();
                const DirectX::XMFLOAT3 b = base.AsVector3();
                const DirectX::XMFLOAT3 v = value.AsVector3();
                return Reflection::PropertyValue::MakeVector3({
                    BlendScalar(c.x, b.x, v.x, weight, mode),
                    BlendScalar(c.y, b.y, v.y, weight, mode),
                    BlendScalar(c.z, b.z, v.z, weight, mode) });
            }
            case PropertyType::Vector4:
            case PropertyType::Color:
            {
                const DirectX::XMFLOAT4 c = current.AsVector4();
                const DirectX::XMFLOAT4 b = base.AsVector4();
                const DirectX::XMFLOAT4 v = value.AsVector4();
                const DirectX::XMFLOAT4 result{
                    BlendScalar(c.x, b.x, v.x, weight, mode),
                    BlendScalar(c.y, b.y, v.y, weight, mode),
                    BlendScalar(c.z, b.z, v.z, weight, mode),
                    BlendScalar(c.w, b.w, v.w, weight, mode) };
                if (current.Type() == PropertyType::Color)
                    return Reflection::PropertyValue::MakeColor(result);
                return Reflection::PropertyValue::MakeVector4(result);
            }
            default:
                break;
            }
            return current;
        }

        Reflection::PropertyValue CombineOverrideLike(
            const Reflection::PropertyValue& fallback,
            const std::vector<MotionMixer::Contribution>& contributions,
            Reflection::Animatable animatable,
            MotionBlendMode mode)
        {
            bool has_value = false;
            Reflection::PropertyValue value = fallback;
            float total_weight = 0.0f;
            float best_step_weight = -1.0f;

            for (const MotionMixer::Contribution& contribution : contributions)
            {
                if (contribution.mode != mode || contribution.weight <= 0.0f) continue;
                if (animatable == Reflection::Animatable::Step)
                {
                    if (contribution.weight >= best_step_weight)
                    {
                        value = contribution.value;
                        best_step_weight = contribution.weight;
                        has_value = true;
                    }
                    continue;
                }

                if (!has_value)
                {
                    value = contribution.value;
                    total_weight = contribution.weight;
                    has_value = true;
                    continue;
                }

                const float total = total_weight + contribution.weight;
                if (total <= 0.0f) continue;
                value = Reflection::PropertyValue::Lerp(value, contribution.value,
                    contribution.weight / total);
                total_weight = total;
            }

            return has_value ? value : fallback;
        }
    }

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
        const Reflection::PropertyValue& value, float weight, MotionBlendMode mode)
    {
        if (!binding.Valid() || weight <= 0.0f) return;
        if (binding.property->animatable == Reflection::Animatable::None) return;
        if (binding.property->animatable == Reflection::Animatable::Step)
        {
            mode = MotionBlendMode::Override;
        }

        TargetKey key{ binding.component, binding.property->name };
        auto found = accumulators_.find(key);
        if (found == accumulators_.end())
        {
            Accumulator accumulator;
            accumulator.component = binding.component;
            accumulator.property = binding.property;
            accumulator.base_value = binding.property->Capture(*binding.component);
            accumulator.total_weight = weight;
            accumulator.contributions.push_back({ value, weight, mode });
            accumulators_.emplace(std::move(key), std::move(accumulator));
            return;
        }

        Accumulator& accumulator = found->second;
        accumulator.contributions.push_back({ value, weight, mode });
        accumulator.total_weight += weight;
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

            Reflection::PropertyValue value = CombineOverrideLike(accumulator.base_value,
                accumulator.contributions, accumulator.property->animatable,
                MotionBlendMode::Override);

            for (const Contribution& contribution : accumulator.contributions)
            {
                if (contribution.mode != MotionBlendMode::Blend ||
                    contribution.weight <= 0.0f)
                {
                    continue;
                }
                value = Reflection::PropertyValue::Lerp(value, contribution.value,
                    (std::max)(0.0f, (std::min)(1.0f, contribution.weight)));
            }

            for (const Contribution& contribution : accumulator.contributions)
            {
                if ((contribution.mode != MotionBlendMode::Multiply &&
                    contribution.mode != MotionBlendMode::Additive) ||
                    contribution.weight <= 0.0f)
                {
                    continue;
                }
                value = AdditiveOrMultiply(value, accumulator.base_value,
                    contribution.value, contribution.weight, contribution.mode);
            }

            accumulator.property->Apply(*accumulator.component, value);
        }
    }

    bool MotionMixer::WasDriven(const Core::Component& component,
        const std::string& property) const noexcept
    {
        TargetKey key{ const_cast<Core::Component*>(&component), property };
        return accumulators_.find(key) != accumulators_.end();
    }
}
