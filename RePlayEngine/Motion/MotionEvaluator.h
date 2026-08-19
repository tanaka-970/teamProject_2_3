#pragma once

#include "MotionAsset.h"

namespace ReplayEngine::Motion
{
    class MotionEvaluator final
    {
    public:
        static bool EvaluateTrack(const MotionTrack& track, float time,
            Reflection::PropertyValue& out);
    };
}
