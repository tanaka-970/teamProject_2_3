#include "MotionEvaluator.h"

#include <cstddef>

namespace ReplayEngine::Motion
{
    bool MotionEvaluator::EvaluateTrack(const MotionTrack& track, float time,
        Reflection::PropertyValue& out)
    {
        if (!track.enabled || track.keys.empty()) return false;

        if (track.keys.size() == 1 || time <= track.keys.front().time)
        {
            out = track.keys.front().value;
            return true;
        }

        if (time >= track.keys.back().time)
        {
            out = track.keys.back().value;
            return true;
        }

        std::size_t next = 1;
        while (next < track.keys.size() && track.keys[next].time < time)
        {
            ++next;
        }

        const MotionKeyframe& a = track.keys[next - 1];
        const MotionKeyframe& b = track.keys[next];
        const float span = b.time - a.time;
        if (span <= 0.0f)
        {
            out = b.value;
            return true;
        }

        if (a.easing == MotionEasing::Step)
        {
            out = a.value;
            return true;
        }

        const float normalized = (time - a.time) / span;
        const float eased = ApplyEasing(a.easing, normalized, a.bezier);
        out = Reflection::PropertyValue::Lerp(a.value, b.value, eased);
        return true;
    }
}
