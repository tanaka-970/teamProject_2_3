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

    void ProjectSettingsSerializer::ApplySafeDefaults(ProjectSettings& settings) noexcept
    {
        // 既定は「何も設定されていない」。
        // 勝手にどれかの Scene や Prefab を選ぶことはしない。
        settings.Reset();
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

        // v2 で追加。空 GUID も「未設定」という値として書き出す。
        // 行ごと省略すると、v1 から移行しただけのファイルと
        // 「明示的に未設定へ戻したファイル」を区別できなくなる。
        stream << "STARTUP_SCENE " << std::quoted(settings.StartupSceneGuid()) << '\n';

        // v3 で追加。Active Scene Flow も GUID だけを保存する。
        stream << "SCENE_FLOW " << std::quoted(settings.SceneFlowGuid()) << '\n';
        stream << "LOCALIZATION_TABLE " << std::quoted(settings.LocalizationTableGuid()) << '\n';
        stream << "DEFAULT_LANGUAGE " << std::quoted(settings.DefaultLanguage()) << '\n';
        stream << "INPUT_ACTION_ASSET " << std::quoted(settings.InputActionAssetGuid()) << '\n';
        stream << "UI_FOCUS_OUTLINE_ENABLED " << (settings.FocusOutlineEnabled() ? 1 : 0) << '\n';
        const DirectX::XMFLOAT4 focus_color = settings.FocusOutlineColor();
        stream << "UI_FOCUS_OUTLINE_COLOR " << focus_color.x << ' ' << focus_color.y << ' '
            << focus_color.z << ' ' << focus_color.w << '\n';
        stream << "UI_FOCUS_OUTLINE_WIDTH " << settings.FocusOutlineWidth() << '\n';
        stream << "UI_FOCUS_CORNER_RADIUS " << settings.FocusCornerRadius() << '\n';

        // 描画トグル。行が無い旧ファイルは Reset() の既定値のまま読まれる。
        stream << "RENDER_SSAO " << (settings.SsaoEnabled() ? 1 : 0) << '\n';
        stream << "RENDER_SSR " << (settings.SsrEnabled() ? 1 : 0) << '\n';
        stream << "RENDER_TAA " << (settings.TaaEnabled() ? 1 : 0) << '\n';
        stream << "RENDER_DEPTH_PREPASS "
            << (settings.DepthPrepassEnabled() ? 1 : 0) << '\n';

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
        if (version < minimum_supported_version || version > current_version)
        {
            // 安全な既定値へ戻してから返す。
            // 読めなかったファイルの断片が設定として残らないようにする。
            ApplySafeDefaults(settings);
            error = "このビルドが読めないプロジェクト設定のバージョンです (v" +
                std::to_string(version) + ")。";
            return false;
        }

        // v1 からの移行:
        //   v1 には STARTUP_SCENE 行が無い。読み飛ばされるので空のままになり、
        //   「Startup Scene 未設定」という正しい状態になる。
        //   移行のために別の値を推測して埋めることはしない。

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
            else if (keyword == "STARTUP_SCENE")
            {
                // v2 で追加。v1 のファイルにはこの行が無いので、空のままになる。
                std::istringstream value_stream(line);
                value_stream.imbue(std::locale::classic());
                std::string guid;
                if (value_stream >> std::quoted(guid))
                {
                    settings.SetStartupSceneGuid(std::move(guid));
                }
            }
            else if (keyword == "SCENE_FLOW")
            {
                std::istringstream value_stream(line);
                value_stream.imbue(std::locale::classic());
                std::string guid;
                if (value_stream >> std::quoted(guid))
                {
                    settings.SetSceneFlowGuid(std::move(guid));
                }
            }
            else if (keyword == "LOCALIZATION_TABLE")
            {
                std::istringstream value_stream(line);
                value_stream.imbue(std::locale::classic());
                std::string guid;
                if (value_stream >> std::quoted(guid))
                    settings.SetLocalizationTableGuid(std::move(guid));
            }
            else if (keyword == "DEFAULT_LANGUAGE")
            {
                std::istringstream value_stream(line);
                value_stream.imbue(std::locale::classic());
                std::string language;
                if (value_stream >> std::quoted(language))
                    settings.SetDefaultLanguage(std::move(language));
            }
            else if (keyword == "INPUT_ACTION_ASSET")
            {
                std::istringstream value_stream(line);
                value_stream.imbue(std::locale::classic());
                std::string guid;
                if (value_stream >> std::quoted(guid))
                    settings.SetInputActionAssetGuid(std::move(guid));
            }
            else if (keyword == "UI_FOCUS_OUTLINE_ENABLED")
            {
                std::istringstream value_stream(line);
                int enabled = 1;
                if (value_stream >> enabled) settings.SetFocusOutlineEnabled(enabled != 0);
            }
            else if (keyword == "UI_FOCUS_OUTLINE_COLOR")
            {
                std::istringstream value_stream(line);
                DirectX::XMFLOAT4 color = settings.FocusOutlineColor();
                if (value_stream >> color.x >> color.y >> color.z >> color.w)
                    settings.SetFocusOutlineColor(color);
            }
            else if (keyword == "UI_FOCUS_OUTLINE_WIDTH")
            {
                std::istringstream value_stream(line);
                float value = settings.FocusOutlineWidth();
                if (value_stream >> value) settings.SetFocusOutlineWidth(value);
            }
            else if (keyword == "UI_FOCUS_CORNER_RADIUS")
            {
                std::istringstream value_stream(line);
                float value = settings.FocusCornerRadius();
                if (value_stream >> value) settings.SetFocusCornerRadius(value);
            }
            else if (keyword == "RENDER_SSAO")
            {
                std::istringstream value_stream(line);
                int enabled = settings.SsaoEnabled() ? 1 : 0;
                if (value_stream >> enabled) settings.SetSsaoEnabled(enabled != 0);
            }
            else if (keyword == "RENDER_SSR")
            {
                std::istringstream value_stream(line);
                int enabled = settings.SsrEnabled() ? 1 : 0;
                if (value_stream >> enabled) settings.SetSsrEnabled(enabled != 0);
            }
            else if (keyword == "RENDER_TAA")
            {
                std::istringstream value_stream(line);
                int enabled = settings.TaaEnabled() ? 1 : 0;
                if (value_stream >> enabled) settings.SetTaaEnabled(enabled != 0);
            }
            else if (keyword == "RENDER_DEPTH_PREPASS")
            {
                std::istringstream value_stream(line);
                int enabled = settings.DepthPrepassEnabled() ? 1 : 0;
                if (value_stream >> enabled) settings.SetDepthPrepassEnabled(enabled != 0);
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
            ApplySafeDefaults(settings);
            return false;
        }
        if (!ReadText(settings, stream, error))
        {
            ApplySafeDefaults(settings);
            return false;
        }
        return true;
    }
}
