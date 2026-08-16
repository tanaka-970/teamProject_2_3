#include "LocalizationTable.h"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <locale>
#include <sstream>
#include <utility>

namespace ReplayEngine::Localization
{
    namespace
    {
        constexpr const char* magic_token = "REPLAY_LOCALIZATION";

        void StripUtf8Bom(std::string& text)
        {
            if (text.size() >= 3 &&
                static_cast<unsigned char>(text[0]) == 0xEFu &&
                static_cast<unsigned char>(text[1]) == 0xBBu &&
                static_cast<unsigned char>(text[2]) == 0xBFu)
            {
                text.erase(0, 3);
            }
        }
    }

    bool LocalizationTable::LoadFromFile(const std::filesystem::path& path,
        std::string& error)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file)
        {
            error = "Localization table を開けません: " + path.generic_string();
            return false;
        }
        std::ostringstream bytes;
        bytes << file.rdbuf();
        std::string text = bytes.str();
        StripUtf8Bom(text);
        std::istringstream stream(text);
        stream.imbue(std::locale::classic());

        std::string magic;
        int version = 0;
        if (!(stream >> magic >> version) || magic != magic_token || version != current_version)
        {
            error = "Localization table の形式またはバージョンが不正です。";
            return false;
        }

        std::vector<std::string> languages;
        std::map<std::string, std::map<std::string, std::string>> entries;
        std::string keyword;
        bool reached_end = false;
        while (stream >> keyword)
        {
            if (keyword == "LANGUAGE")
            {
                std::string language;
                if (!(stream >> std::quoted(language)) || language.empty())
                {
                    error = "LANGUAGE 行が不正です。";
                    return false;
                }
                if (std::find(languages.begin(), languages.end(), language) == languages.end())
                    languages.push_back(std::move(language));
            }
            else if (keyword == "ENTRY")
            {
                std::string key;
                std::string language;
                std::string value;
                if (!(stream >> std::quoted(key) >> std::quoted(language) >> std::quoted(value)) ||
                    key.empty() || language.empty())
                {
                    error = "ENTRY 行が不正です。";
                    return false;
                }
                entries[key][language] = std::move(value);
                if (std::find(languages.begin(), languages.end(), language) == languages.end())
                    languages.push_back(std::move(language));
            }
            else if (keyword == "END_LOCALIZATION")
            {
                reached_end = true;
                break;
            }
            else
            {
                std::string ignored;
                std::getline(stream, ignored);
            }
        }

        if (!reached_end)
        {
            error = "Localization table の終端がありません。";
            return false;
        }
        languages_ = std::move(languages);
        entries_ = std::move(entries);
        error.clear();
        return true;
    }

    bool LocalizationTable::SaveToFile(const std::filesystem::path& path,
        std::string& error) const
    {
        std::error_code filesystem_error;
        if (!path.parent_path().empty())
        {
            std::filesystem::create_directories(path.parent_path(), filesystem_error);
            if (filesystem_error)
            {
                error = "Localization table の保存先を作成できません。";
                return false;
            }
        }
        const std::filesystem::path temporary = path.string() + ".tmp";
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        if (!stream)
        {
            error = "Localization table を作成できません。";
            return false;
        }
        stream.imbue(std::locale::classic());
        // Asset は UTF-8 / BOM なしで保存する。Load 側は BOM 付きも明示的に除去して読める。
        stream << magic_token << ' ' << current_version << '\n';
        for (const std::string& language : languages_)
            stream << "LANGUAGE " << std::quoted(language) << '\n';
        for (const auto& entry : entries_)
        {
            for (const auto& localized : entry.second)
            {
                stream << "ENTRY " << std::quoted(entry.first) << ' '
                    << std::quoted(localized.first) << ' '
                    << std::quoted(localized.second) << '\n';
            }
        }
        stream << "END_LOCALIZATION\n";
        stream.close();
        if (!stream)
        {
            error = "Localization table の書き込みに失敗しました。";
            return false;
        }
        std::filesystem::rename(temporary, path, filesystem_error);
        if (filesystem_error)
        {
            filesystem_error.clear();
            std::filesystem::copy_file(temporary, path,
                std::filesystem::copy_options::overwrite_existing, filesystem_error);
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            if (filesystem_error)
            {
                error = "Localization table を差し替えられません。";
                return false;
            }
        }
        error.clear();
        return true;
    }

    std::vector<std::string> LocalizationTable::Keys() const
    {
        std::vector<std::string> keys;
        keys.reserve(entries_.size());
        for (const auto& entry : entries_) keys.push_back(entry.first);
        return keys;
    }

    bool LocalizationTable::HasLanguage(const std::string& language) const noexcept
    {
        return std::find(languages_.begin(), languages_.end(), language) != languages_.end();
    }

    bool LocalizationTable::HasKey(const std::string& key) const noexcept
    {
        return entries_.find(key) != entries_.end();
    }

    std::string LocalizationTable::Resolve(const std::string& key,
        const std::string& language, const std::string& fallback) const
    {
        const auto entry = entries_.find(key);
        if (entry == entries_.end()) return fallback.empty() ? key : fallback;
        const auto localized = entry->second.find(language);
        if (localized != entry->second.end()) return localized->second;
        if (!languages_.empty())
        {
            const auto first = entry->second.find(languages_.front());
            if (first != entry->second.end()) return first->second;
        }
        if (!entry->second.empty()) return entry->second.begin()->second;
        return fallback.empty() ? key : fallback;
    }

    void LocalizationTable::SetLanguages(std::vector<std::string> languages)
    {
        languages.erase(std::remove_if(languages.begin(), languages.end(),
            [](const std::string& value) { return value.empty(); }), languages.end());
        std::vector<std::string> unique;
        for (std::string& language : languages)
        {
            if (std::find(unique.begin(), unique.end(), language) == unique.end())
                unique.push_back(std::move(language));
        }
        languages_ = std::move(unique);
    }

    void LocalizationTable::Set(const std::string& key, const std::string& language,
        std::string value)
    {
        if (key.empty() || language.empty()) return;
        if (!HasLanguage(language)) languages_.push_back(language);
        entries_[key][language] = std::move(value);
    }
}
