#include "LocalizationService.h"

#include "../Assets/AssetDatabase.h"

#include <utility>

namespace ReplayEngine::Localization
{
    LocalizationService& LocalizationService::Global() noexcept
    {
        static LocalizationService service;
        return service;
    }

    void LocalizationService::Configure(const Assets::AssetDatabase* database,
        std::string table_guid, std::string language)
    {
        const bool changed = database_ != database || table_guid_ != table_guid ||
            language_ != language;
        database_ = database;
        table_guid_ = std::move(table_guid);
        language_ = language.empty() ? std::string("ja") : std::move(language);
        if (changed)
        {
            loaded_ = false;
            attempted_ = false;
            loaded_path_.clear();
            ++revision_;
        }
    }

    void LocalizationService::SetLanguage(std::string language)
    {
        if (language.empty()) return;
        if (language_ == language) return;
        language_ = std::move(language);
        ++revision_;
    }

    void LocalizationService::SetTableGuid(std::string guid)
    {
        if (table_guid_ == guid) return;
        table_guid_ = std::move(guid);
        loaded_ = false;
        attempted_ = false;
        loaded_path_.clear();
        ++revision_;
    }

    bool LocalizationService::Refresh() const
    {
        if (database_ == nullptr || table_guid_.empty()) return false;
        const Assets::AssetRecord* record = database_->FindByGuid(table_guid_);
        if (record == nullptr || record->kind != Assets::AssetKind::Localization) return false;
        const std::filesystem::path path = record->source_path;
        std::error_code error_code;
        const auto write_time = std::filesystem::last_write_time(path, error_code);
        // 壊れた/未完成のテーブルを毎 UIText・毎フレーム読み直さない。
        // 同じ更新時刻なら「失敗」もキャッシュし、ファイルが更新されたときだけ再試行する。
        if (!error_code && attempted_ && path == loaded_path_ &&
            write_time == loaded_write_time_)
        {
            return loaded_;
        }
        if (error_code && attempted_ && path == loaded_path_) return loaded_;

        attempted_ = true;
        loaded_path_ = path;
        if (!error_code) loaded_write_time_ = write_time;
        LocalizationTable candidate;
        std::string error;
        if (!candidate.LoadFromFile(path, error))
        {
            loaded_ = false;
            return false;
        }
        table_ = std::move(candidate);
        loaded_ = true;
        return true;
    }

    std::string LocalizationService::Resolve(const std::string& key,
        const std::string& fallback) const
    {
        if (key.empty()) return fallback;
        if (!Refresh()) return fallback.empty() ? key : fallback;
        return table_.Resolve(key, language_, fallback);
    }

    std::vector<std::string> LocalizationService::Keys() const
    {
        if (!Refresh()) return {};
        return table_.Keys();
    }

    const LocalizationTable* LocalizationService::ActiveTable() const
    {
        return Refresh() ? &table_ : nullptr;
    }
}
