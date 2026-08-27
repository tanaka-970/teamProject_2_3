#pragma once

#include "MotionAsset.h"

#include <string>

namespace ReplayEngine::Assets { class AssetDatabase; }

namespace ReplayEngine::Motion
{
    class MotionEvaluator final
    {
    public:
        static bool EvaluateTrack(const MotionTrack& track, float time,
            Reflection::PropertyValue& out,
            const Assets::AssetDatabase* database = nullptr,
            std::string* curve_error = nullptr);
    };
}
