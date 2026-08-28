#pragma once

#include "../Reflection/Property/References.h"

#include <DirectXMath.h>

#include <filesystem>
#include <string>
#include <vector>

namespace ReplayEngine::Assets { class AssetDatabase; }

namespace ReplayEngine::Motion
{
    // Motion以外のComponentからもAssetReference経由で使える共通補間アセット。
    class EasingCurveAsset final
    {
    public:
        static constexpr const char* file_extension = ".replayeasing";
        static constexpr int current_version = 1;

        std::string name;
        int sample_count = 64;
        std::vector<float> samples; // 評価の正本は等間隔xに対応するy配列だけを持つ。
        std::vector<DirectX::XMFLOAT2> control_points; // 編集用の区分3次ベジェアンカーで空でもよい。

        float Evaluate(float t) const noexcept;
        void RebuildSamplesFromControlPoints() noexcept;
        void FitControlPointsToSamples() noexcept;
        void Normalize() noexcept;

        bool LoadFromFile(const std::filesystem::path& path, std::string& error);
        bool SaveToFile(const std::filesystem::path& path, std::string& error) const;

        static const EasingCurveAsset* Resolve(
            const Assets::AssetDatabase* database,
            const Reflection::AssetReference& reference) noexcept;
        static void Invalidate(const std::string& guid) noexcept;
    };
}
