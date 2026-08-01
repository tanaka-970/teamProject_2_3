#pragma once

#include "ProjectSettings.h"

#include <filesystem>
#include <iosfwd>
#include <string>

namespace ReplayEngine::Project
{
    // ProjectSettings とファイルの相互変換だけを担当する。
    //
    // Scene / SceneSerializer と同じ分け方にしてある。
    // ProjectSettings 側はファイル入出力を一切知らない。
    //
    // 形式は Scene と同じ「先頭にマジックとバージョン」のテキスト。
    // 行単位で読むので、将来項目が増えても古いファイルを読める。
    class ProjectSettingsSerializer final
    {
    public:
        ProjectSettingsSerializer() = delete;

        static constexpr const char* file_extension = ".replayproject";
        static constexpr int current_version = 1;

        // 既定の保存先。プロジェクト直下の resources/ に置く。
        static std::filesystem::path DefaultPath();

        // 失敗しても既存ファイルを壊さないよう、一時ファイルへ書いてから差し替える。
        static bool SaveToFile(const ProjectSettings& settings,
            const std::filesystem::path& path, std::string& error);

        // ファイルが無い場合は false を返すが、settings は既定値のまま壊れない。
        // 呼び出し側は「無ければ新規プロジェクト」として扱えばよい。
        static bool LoadFromFile(ProjectSettings& settings,
            const std::filesystem::path& path, std::string& error);

        static bool WriteText(const ProjectSettings& settings, std::ostream& stream,
            std::string& error);
        static bool ReadText(ProjectSettings& settings, std::istream& stream,
            std::string& error);
    };
}
