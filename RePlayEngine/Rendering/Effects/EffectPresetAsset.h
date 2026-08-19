#pragma once

#include "../../Reflection/Property/References.h"
#include "../../UI/Effects/UIEffect.h"

#include <filesystem>
#include <string>
#include <vector>

namespace ReplayEngine::Assets { class AssetDatabase; }

namespace ReplayEngine::Rendering::Effects
{
    class EffectPresetAsset final
    {
    public:
        static constexpr const char* file_extension = ".replayeffect";
        static constexpr int current_version = 1;

        std::vector<UI::UIEffect> effects;

        bool LoadFromFile(const std::filesystem::path& path, std::string& error);
        bool SaveToFile(const std::filesystem::path& path, std::string& error) const;

        // AssetDatabase と file timestamp を見て共有 cache を更新する。
        // 見つからない / 壊れている場合は nullptr。呼び出し側は inline 値へ安全に戻す。
        static const std::vector<UI::UIEffect>* Resolve(
            const Assets::AssetDatabase* database,
            const Reflection::AssetReference& reference) noexcept;
        static void Invalidate(const std::string& guid) noexcept;
    };
}
