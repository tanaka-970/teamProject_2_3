#pragma once

#include <filesystem>
#include <string>

namespace ReplayEngine::Editor
{
    class FileEditHistory;

    class EditorHelp final
    {
    public:
        EditorHelp() = delete;

        static void Configure(const std::filesystem::path& path,
            FileEditHistory* history) noexcept;
        static bool Load(std::string& error);
        static void Item(const char* key);
        static void Item(const char* key, const char* default_text);
        static bool ValidateRoundTrip(std::string& report);

    private:
        static bool Save(std::string& error);
    };
}
