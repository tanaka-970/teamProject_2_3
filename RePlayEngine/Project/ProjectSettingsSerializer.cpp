#include "ProjectSettingsSerializer.h"

#include <fstream>
#include <iomanip>
#include <istream>
#include <locale>
#include <ostream>
#include <sstream>

namespace ReplayEngine::Project
{
    namespace
    {
        constexpr const char* magic_token = "REPLAY_PROJECT";
    }

    std::filesystem::path ProjectSettingsSerializer::DefaultPath()
    {
        return std::filesystem::path("resources") /
            (std::string("Project") + file_extension);
    }

    bool ProjectSettingsSerializer::WriteText(const ProjectSettings& settings,
        std::ostream& stream, std::string& error)
    {
        stream.imbue(std::locale::classic());

        stream << magic_token << ' ' << current_version << '\n';

        // 保存するのは AssetGUID だけ。表示名とパスは AssetDatabase から引き直す。
        // 名前やパスをここへ焼き込むと、Prefab の名前を変えた瞬間に食い違う。
        stream << "DEFAULT_CONTROLLED_CHARACTER_PREFAB "
            << std::quoted(settings.DefaultCharacterPrefabGuid()) << '\n';

        if (!stream)
        {
            error = "プロジェクト設定の書き込みに失敗しました。";
            return false;
        }
        return true;
    }

    bool ProjectSettingsSerializer::ReadText(ProjectSettings& settings,
        std::istream& stream, std::string& error)
    {
        stream.imbue(std::locale::classic());
        settings.Reset();

        std::string magic;
        int version = 0;
        if (!(stream >> magic >> version) || magic != magic_token)
        {
            error = "プロジェクト設定ファイルではありません。";
            return false;
        }
        if (version <= 0 || version > current_version)
        {
            error = "このビルドが読めないプロジェクト設定のバージョンです (v" +
                std::to_string(version) + ")。";
            return false;
        }

        // 以降は「キーワード + 行の残り」の並び。
        // 未知のキーワードは読み飛ばすので、新しい項目が増えた版で保存された
        // ファイルを古いビルドで開いても落ちない。
        std::string keyword;
        while (stream >> keyword)
        {
            std::string line;
            if (!std::getline(stream, line)) line.clear();

            if (keyword == "DEFAULT_CONTROLLED_CHARACTER_PREFAB")
            {
                std::istringstream value_stream(line);
                value_stream.imbue(std::locale::classic());
                std::string guid;
                if (value_stream >> std::quoted(guid))
                {
                    settings.SetDefaultCharacterPrefabGuid(std::move(guid));
                }
            }
            // 未知のキーワードはここで捨てる。
        }
        return true;
    }

    bool ProjectSettingsSerializer::SaveToFile(const ProjectSettings& settings,
        const std::filesystem::path& path, std::string& error)
    {
        std::error_code filesystem_error;
        if (!path.parent_path().empty())
        {
            std::filesystem::create_directories(path.parent_path(), filesystem_error);
            if (filesystem_error)
            {
                error = "プロジェクト設定の保存先フォルダーを作成できません。";
                return false;
            }
        }

        std::ostringstream buffer;
        if (!WriteText(settings, buffer, error)) return false;

        const std::filesystem::path temporary = path.string() + ".tmp";
        {
            std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
            if (!stream)
            {
                error = "プロジェクト設定ファイルを作成できません。";
                return false;
            }
            const std::string text = buffer.str();
            stream.write(text.data(), static_cast<std::streamsize>(text.size()));
            if (!stream)
            {
                error = "プロジェクト設定ファイルへの書き込みに失敗しました。";
                return false;
            }
        }

        std::filesystem::rename(temporary, path, filesystem_error);
        if (filesystem_error)
        {
            std::filesystem::copy_file(temporary, path,
                std::filesystem::copy_options::overwrite_existing, filesystem_error);
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            if (filesystem_error)
            {
                error = "プロジェクト設定ファイルを差し替えられません。";
                return false;
            }
        }
        return true;
    }

    bool ProjectSettingsSerializer::LoadFromFile(ProjectSettings& settings,
        const std::filesystem::path& path, std::string& error)
    {
        std::error_code filesystem_error;
        if (!std::filesystem::exists(path, filesystem_error) || filesystem_error)
        {
            // 「まだ作られていない」は失敗ではあるがエラーではない。
            // 呼び出し側は既定値のまま続行できる。
            error = "プロジェクト設定がまだありません: " + path.generic_string();
            settings.Reset();
            return false;
        }

        std::ifstream stream(path, std::ios::binary);
        if (!stream)
        {
            error = "プロジェクト設定ファイルを開けません: " + path.generic_string();
            return false;
        }
        return ReadText(settings, stream, error);
    }
}
