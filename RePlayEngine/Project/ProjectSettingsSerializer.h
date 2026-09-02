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

        // v1 … Default Controlled Character Prefab だけ
        // v2 … Startup Scene (AssetGUID) を追加
        // v3 … Active Scene Flow (AssetGUID) を追加
        // v4 … Runtime UI Focus Style を追加
        // v5 … Localization table / default language を追加
        // v6 … Input Action Asset GUID を追加
        // v7 … Game Template Component の Editor 表示方針を追加
        // v8 … SSAO / SSR / TAA の調整値を追加
        // v9 … Screen Space の追加調整値を追加
        // v10 … Loading Screen Scene (AssetGUID) を追加
        //
        // Scene のファイル形式とは別のバージョン番号。
        // 片方を上げたらもう片方も上げる、という関係にはしない。
        // 保存する内容が別なので、揃えると意味の無い版番号が増える。
        static constexpr int current_version = 10;

        // v1〜v9 のファイルもそのまま読める。読み込み後に保存すると v10 になる。
        static constexpr int minimum_supported_version = 1;

        // 読み込みに失敗したときに使う安全な既定値へ戻す。
        // 「壊れたファイルの中身が半分だけ残る」状態を作らない。
        static void ApplySafeDefaults(ProjectSettings& settings) noexcept;

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
