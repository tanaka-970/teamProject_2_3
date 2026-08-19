#pragma once

#include "LocalizationTable.h"

#include <cstdint>
#include <filesystem>
#include <string>

namespace ReplayEngine::Assets { class AssetDatabase; }

namespace ReplayEngine::Localization
{
    class LocalizationService final
    {
    public:
        static LocalizationService& Global() noexcept;

        void Configure(const Assets::AssetDatabase* database,
            std::string table_guid, std::string language);
        void SetLanguage(std::string language);
        void SetTableGuid(std::string guid);

        const std::string& Language() const noexcept { return language_; }
        const std::string& TableGuid() const noexcept { return table_guid_; }
        std::uint64_t Revision() const noexcept { return revision_; }
        std::string Resolve(const std::string& key, const std::string& fallback) const;
        std::vector<std::string> Keys() const;
        const LocalizationTable* ActiveTable() const;

    private:
        bool Refresh() const;

        const Assets::AssetDatabase* database_ = nullptr;
        std::string table_guid_;
        std::string language_ = "ja";
        mutable LocalizationTable table_;
        mutable std::filesystem::path loaded_path_;
        mutable std::filesystem::file_time_type loaded_write_time_{};
        mutable bool loaded_ = false;
        mutable bool attempted_ = false;
        std::uint64_t revision_ = 1;
    };
}
