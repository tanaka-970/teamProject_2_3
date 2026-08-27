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

        static bool EvaluateTrack(const MotionTrack& track, float time, float wiggle_time,
            Reflection::PropertyValue& out,
            const Assets::AssetDatabase* database = nullptr,
            std::string* curve_error = nullptr);

        static float RemapMotionTime(const MotionAsset& asset, float time,
            const Assets::AssetDatabase* database = nullptr,
            std::string* curve_error = nullptr);
    };
}
