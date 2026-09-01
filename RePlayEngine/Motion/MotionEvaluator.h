#pragma once

#include "MotionAsset.h"

#include <string>

namespace ReplayEngine::Assets { class AssetDatabase; }

namespace ReplayEngine::Motion
{
    struct MotionTrackEvaluationContext
    {
        float time = 0.0f;
        float raw_time = 0.0f;
        float duration = 0.0f;
        const Assets::AssetDatabase* database = nullptr;
        std::string* error = nullptr;
    };

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

        static bool EvaluateTrackWithContext(const MotionTrack& track,
            const MotionTrackEvaluationContext& context,
            Reflection::PropertyValue& out);

        static float RemapMotionTime(const MotionAsset& asset, float time,
            const Assets::AssetDatabase* database = nullptr,
            std::string* curve_error = nullptr);
    };
}
