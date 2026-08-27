#include "MotionEvaluator.h"

#include "EasingCurveAsset.h"
#include "MotionExpression.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>

namespace ReplayEngine::Motion
{
    namespace
    {
        void ApplyWiggle(const MotionTrack& track, float time,
            Reflection::PropertyValue& value)
        {
            if (!track.wiggle.enabled || track.wiggle.amplitude == 0.0f) return;
            if (!std::isfinite(time) || !std::isfinite(track.wiggle.amplitude)) return;

            using Reflection::PropertyType;
            const float amplitude = track.wiggle.amplitude;
            switch (value.Type())
            {
            case PropertyType::Float:
                value = Reflection::PropertyValue::MakeFloat(value.AsFloat() +
                    MotionExpressionEvaluator::WiggleNoise(track.wiggle, time, 0u) * amplitude);
                break;
            case PropertyType::Double:
                value = Reflection::PropertyValue::MakeDouble(value.AsDouble() +
                    static_cast<double>(MotionExpressionEvaluator::WiggleNoise(track.wiggle, time, 0u) * amplitude));
                break;
            case PropertyType::Vector2:
            {
                DirectX::XMFLOAT2 v = value.AsVector2();
                v.x += MotionExpressionEvaluator::WiggleNoise(track.wiggle, time, 0u) * amplitude;
                v.y += MotionExpressionEvaluator::WiggleNoise(track.wiggle, time, 1u) * amplitude;
                value = Reflection::PropertyValue::MakeVector2(v);
                break;
            }
            case PropertyType::Vector3:
            {
                DirectX::XMFLOAT3 v = value.AsVector3();
                v.x += MotionExpressionEvaluator::WiggleNoise(track.wiggle, time, 0u) * amplitude;
                v.y += MotionExpressionEvaluator::WiggleNoise(track.wiggle, time, 1u) * amplitude;
                v.z += MotionExpressionEvaluator::WiggleNoise(track.wiggle, time, 2u) * amplitude;
                value = Reflection::PropertyValue::MakeVector3(v);
                break;
            }
            case PropertyType::Vector4:
            {
                DirectX::XMFLOAT4 v = value.AsVector4();
                v.x += MotionExpressionEvaluator::WiggleNoise(track.wiggle, time, 0u) * amplitude;
                v.y += MotionExpressionEvaluator::WiggleNoise(track.wiggle, time, 1u) * amplitude;
                v.z += MotionExpressionEvaluator::WiggleNoise(track.wiggle, time, 2u) * amplitude;
                v.w += MotionExpressionEvaluator::WiggleNoise(track.wiggle, time, 3u) * amplitude;
                value = Reflection::PropertyValue::MakeVector4(v);
                break;
            }
            case PropertyType::Color:
            {
                DirectX::XMFLOAT4 v = value.AsVector4();
                v.x += MotionExpressionEvaluator::WiggleNoise(track.wiggle, time, 0u) * amplitude;
                v.y += MotionExpressionEvaluator::WiggleNoise(track.wiggle, time, 1u) * amplitude;
                v.z += MotionExpressionEvaluator::WiggleNoise(track.wiggle, time, 2u) * amplitude;
                v.w += MotionExpressionEvaluator::WiggleNoise(track.wiggle, time, 3u) * amplitude;
                value = Reflection::PropertyValue::MakeColor(v);
                break;
            }
            default:
                break;
            }
        }

        void ApplyOffsetLoop(const MotionTrack& track, double cycles,
            Reflection::PropertyValue& value)
        {
            if (cycles <= 0.0 || track.keys.empty()) return;
            const Reflection::PropertyValue& first = track.keys.front().value;
            const Reflection::PropertyValue& last = track.keys.back().value;
            if (first.Type() != last.Type() || value.Type() != first.Type()) return;

            using Reflection::PropertyType;
            switch (value.Type())
            {
            case PropertyType::Float:
                value = Reflection::PropertyValue::MakeFloat(value.AsFloat() +
                    (last.AsFloat() - first.AsFloat()) * static_cast<float>(cycles));
                break;
            case PropertyType::Double:
                value = Reflection::PropertyValue::MakeDouble(value.AsDouble() +
                    (last.AsDouble() - first.AsDouble()) * cycles);
                break;
            case PropertyType::Int:
            {
                const double shifted = static_cast<double>(value.AsInt()) +
                    (static_cast<double>(last.AsInt()) - static_cast<double>(first.AsInt())) * cycles;
                const double clamped = (std::max)(
                    static_cast<double>((std::numeric_limits<int>::min)()),
                    (std::min)(static_cast<double>((std::numeric_limits<int>::max)()), shifted));
                value = Reflection::PropertyValue::MakeInt(static_cast<int>(std::round(clamped)));
                break;
            }
            case PropertyType::Int64:
            {
                const double shifted = static_cast<double>(value.AsInt64()) +
                    (static_cast<double>(last.AsInt64()) -
                        static_cast<double>(first.AsInt64())) * cycles;
                const double minimum = static_cast<double>((std::numeric_limits<std::int64_t>::min)());
                const double maximum = static_cast<double>((std::numeric_limits<std::int64_t>::max)());
                if (shifted <= minimum)
                    value = Reflection::PropertyValue::MakeInt64(
                        (std::numeric_limits<std::int64_t>::min)());
                else if (shifted >= maximum)
                    value = Reflection::PropertyValue::MakeInt64(
                        (std::numeric_limits<std::int64_t>::max)());
                else
                    value = Reflection::PropertyValue::MakeInt64(
                        static_cast<std::int64_t>(std::round(shifted)));
                break;
            }
            case PropertyType::UInt64:
            {
                const double shifted = static_cast<double>(value.AsUInt64()) +
                    (static_cast<double>(last.AsUInt64()) -
                        static_cast<double>(first.AsUInt64())) * cycles;
                const double maximum = static_cast<double>((std::numeric_limits<std::uint64_t>::max)());
                if (shifted <= 0.0)
                    value = Reflection::PropertyValue::MakeUInt64(0u);
                else if (shifted >= maximum)
                    value = Reflection::PropertyValue::MakeUInt64(
                        (std::numeric_limits<std::uint64_t>::max)());
                else
                    value = Reflection::PropertyValue::MakeUInt64(
                        static_cast<std::uint64_t>(std::round(shifted)));
                break;
            }
            case PropertyType::Vector2:
            {
                DirectX::XMFLOAT2 v = value.AsVector2();
                const DirectX::XMFLOAT2 a = first.AsVector2();
                const DirectX::XMFLOAT2 b = last.AsVector2();
                const float count = static_cast<float>(cycles);
                v.x += (b.x - a.x) * count;
                v.y += (b.y - a.y) * count;
                value = Reflection::PropertyValue::MakeVector2(v);
                break;
            }
            case PropertyType::Vector3:
            {
                DirectX::XMFLOAT3 v = value.AsVector3();
                const DirectX::XMFLOAT3 a = first.AsVector3();
                const DirectX::XMFLOAT3 b = last.AsVector3();
                const float count = static_cast<float>(cycles);
                v.x += (b.x - a.x) * count;
                v.y += (b.y - a.y) * count;
                v.z += (b.z - a.z) * count;
                value = Reflection::PropertyValue::MakeVector3(v);
                break;
            }
            case PropertyType::Vector4:
            case PropertyType::Color:
            {
                DirectX::XMFLOAT4 v = value.AsVector4();
                const DirectX::XMFLOAT4 a = first.AsVector4();
                const DirectX::XMFLOAT4 b = last.AsVector4();
                const float count = static_cast<float>(cycles);
                v.x += (b.x - a.x) * count;
                v.y += (b.y - a.y) * count;
                v.z += (b.z - a.z) * count;
                v.w += (b.w - a.w) * count;
                value = value.Type() == PropertyType::Color
                    ? Reflection::PropertyValue::MakeColor(v)
                    : Reflection::PropertyValue::MakeVector4(v);
                break;
            }
            default:
                break;
            }
        }

        float MapLoopTime(const MotionTrack& track, float time,
            double& offset_cycles) noexcept
        {
            offset_cycles = 0.0;
            if (track.loop == MotionTrackLoop::None || track.keys.size() < 2 ||
                time <= track.keys.back().time)
            {
                return time;
            }

            const float begin = track.keys.front().time;
            const float end = track.keys.back().time;
            const float length = end - begin;
            if (length <= 0.0f) return time;

            const double elapsed = static_cast<double>(time) - static_cast<double>(begin);
            const double loop_length = static_cast<double>(length);
            if (track.loop == MotionTrackLoop::PingPong)
            {
                const double period = loop_length * 2.0;
                double phase = std::fmod(elapsed, period);
                if (phase < 0.0) phase += period;
                if (phase <= loop_length)
                    return begin + static_cast<float>(phase);
                return end - static_cast<float>(phase - loop_length);
            }

            double phase = std::fmod(elapsed, loop_length);
            if (phase < 0.0) phase += loop_length;
            if (track.loop == MotionTrackLoop::Offset)
                offset_cycles = std::floor(elapsed / loop_length);
            return begin + static_cast<float>(phase);
        }

        void SetCurveError(const MotionKeyframe& key,
            const Assets::AssetDatabase* database, std::string* curve_error)
        {
            if (curve_error == nullptr) return;
            if (!key.easing_curve.IsAssigned())
            {
                *curve_error = u8"PresetCurve のカーブが未設定です。Linear で評価します。";
                return;
            }
            if (database == nullptr)
            {
                *curve_error = u8"PresetCurve を解決する AssetDatabase がありません。Linear で評価します。 GUID: " +
                    key.easing_curve.guid;
                return;
            }
            *curve_error = u8"PresetCurve のカーブを解決できません。Linear で評価します。 GUID: " +
                key.easing_curve.guid;
        }
    }

    bool EvaluateTrackInternal(const MotionTrack& track, float time, float wiggle_time,
        float duration, Reflection::PropertyValue& out,
        const Assets::AssetDatabase* database, std::string* curve_error)
    {
        if (curve_error != nullptr) curve_error->clear();
        if (!track.enabled || track.keys.empty()) return false;

        double offset_cycles = 0.0;
        const float evaluated_time = MapLoopTime(track, time, offset_cycles);

        if (track.keys.size() == 1 || evaluated_time <= track.keys.front().time)
        {
            out = track.keys.front().value;
            if (track.loop == MotionTrackLoop::Offset) ApplyOffsetLoop(track, offset_cycles, out);
            if (track.expression.enabled && !track.expression.source.empty())
                MotionExpressionEvaluator::Apply(track.expression, out, evaluated_time,
                    wiggle_time, duration, curve_error);
            ApplyWiggle(track, wiggle_time, out);
            return true;
        }

        if (evaluated_time >= track.keys.back().time)
        {
            out = track.keys.back().value;
            if (track.loop == MotionTrackLoop::Offset) ApplyOffsetLoop(track, offset_cycles, out);
            if (track.expression.enabled && !track.expression.source.empty())
                MotionExpressionEvaluator::Apply(track.expression, out, evaluated_time,
                    wiggle_time, duration, curve_error);
            ApplyWiggle(track, wiggle_time, out);
            return true;
        }

        std::size_t next = 1;
        while (next < track.keys.size() && track.keys[next].time < evaluated_time)
        {
            ++next;
        }

        const MotionKeyframe& a = track.keys[next - 1];
        const MotionKeyframe& b = track.keys[next];
        const float span = b.time - a.time;
        if (span <= 0.0f)
        {
            out = b.value;
            if (track.loop == MotionTrackLoop::Offset) ApplyOffsetLoop(track, offset_cycles, out);
            if (track.expression.enabled && !track.expression.source.empty())
                MotionExpressionEvaluator::Apply(track.expression, out, evaluated_time,
                    wiggle_time, duration, curve_error);
            ApplyWiggle(track, wiggle_time, out);
            return true;
        }

        if (a.easing == MotionEasing::Step)
        {
            out = a.value;
            if (track.loop == MotionTrackLoop::Offset) ApplyOffsetLoop(track, offset_cycles, out);
            if (track.expression.enabled && !track.expression.source.empty())
                MotionExpressionEvaluator::Apply(track.expression, out, evaluated_time,
                    wiggle_time, duration, curve_error);
            ApplyWiggle(track, wiggle_time, out);
            return true;
        }

        const float normalized = (evaluated_time - a.time) / span;
        float eased = normalized;
        if (a.easing == MotionEasing::PresetCurve)
        {
            const EasingCurveAsset* curve = EasingCurveAsset::Resolve(database, a.easing_curve);
            if (curve != nullptr) eased = curve->Evaluate(normalized);
            else SetCurveError(a, database, curve_error);
        }
        else
        {
            eased = ApplyEasing(a.easing, normalized, a.bezier);
        }
        out = Reflection::PropertyValue::Lerp(a.value, b.value, eased);
        if (track.loop == MotionTrackLoop::Offset) ApplyOffsetLoop(track, offset_cycles, out);
        if (track.expression.enabled && !track.expression.source.empty())
            MotionExpressionEvaluator::Apply(track.expression, out, evaluated_time,
                wiggle_time, duration, curve_error);
        ApplyWiggle(track, wiggle_time, out);
        return true;
    }

    bool MotionEvaluator::EvaluateTrack(const MotionTrack& track, float time,
        Reflection::PropertyValue& out, const Assets::AssetDatabase* database,
        std::string* curve_error)
    {
        const float fallback_duration = track.keys.empty() ? 0.0f :
            (std::max)(0.0f, track.keys.back().time);
        return EvaluateTrackInternal(track, time, time, fallback_duration, out, database, curve_error);
    }

    bool MotionEvaluator::EvaluateTrack(const MotionTrack& track, float time, float wiggle_time,
        Reflection::PropertyValue& out, const Assets::AssetDatabase* database,
        std::string* curve_error)
    {
        const float fallback_duration = track.keys.empty() ? 0.0f :
            (std::max)(0.0f, track.keys.back().time);
        return EvaluateTrackInternal(track, time, wiggle_time, fallback_duration, out, database, curve_error);
    }

    bool MotionEvaluator::EvaluateTrackWithContext(const MotionTrack& track,
        const MotionTrackEvaluationContext& context, Reflection::PropertyValue& out)
    {
        return EvaluateTrackInternal(track, context.time, context.raw_time,
            context.duration, out, context.database, context.error);
    }

    float MotionEvaluator::RemapMotionTime(const MotionAsset& asset, float time,
        const Assets::AssetDatabase* database, std::string* curve_error)
    {
        if (curve_error != nullptr) curve_error->clear();
        if (asset.duration <= 0.0f) return time;
        if (!asset.time_remap.IsAssigned())
        {
            if (curve_error != nullptr)
                *curve_error = u8"Time Remap が未設定です。等速で評価します。";
            return time;
        }

        const EasingCurveAsset* curve = EasingCurveAsset::Resolve(database, asset.time_remap);
        if (curve == nullptr)
        {
            if (curve_error != nullptr)
            {
                if (database == nullptr)
                {
                    *curve_error = u8"Time Remap を解決する AssetDatabase がありません。等速で評価します。 GUID: " +
                        asset.time_remap.guid;
                }
                else
                {
                    *curve_error = u8"Time Remap のカーブを解決できません。等速で評価します。 GUID: " +
                        asset.time_remap.guid;
                }
            }
            return time;
        }

        const float normalized = time / asset.duration;
        const float remapped = curve->Evaluate(normalized) * asset.duration;
        return (std::max)(0.0f, (std::min)(asset.duration, remapped));
    }

}
