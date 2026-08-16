#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace ReplayEngine::Localization
{
    class LocalizationTable final
    {
    public:
        static constexpr const char* file_extension = ".replayloc";
        static constexpr int current_version = 1;

        bool LoadFromFile(const std::filesystem::path& path, std::string& error);
        bool SaveToFile(const std::filesystem::path& path, std::string& error) const;

        const std::vector<std::string>& Languages() const noexcept { return languages_; }
        std::vector<std::string> Keys() const;
        bool HasLanguage(const std::string& language) const noexcept;
        bool HasKey(const std::string& key) const noexcept;
        std::string Resolve(const std::string& key, const std::string& language,
            const std::string& fallback) const;

        void SetLanguages(std::vector<std::string> languages);
        void Set(const std::string& key, const std::string& language, std::string value);
        bool RemoveKey(const std::string& key) noexcept;
        bool RemoveLanguage(const std::string& language) noexcept;

    private:
        std::vector<std::string> languages_;
        std::map<std::string, std::map<std::string, std::string>> entries_;
    };
}
