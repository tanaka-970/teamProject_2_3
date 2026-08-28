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

        // v7: Editor の Template Component 表示方針。旧ファイルは Reset() の false のまま。
        stream << "EDITOR_SHOW_GAME_TEMPLATE_COMPONENTS "
            << (settings.ShowGameTemplateComponents() ? 1 : 0) << '\n';

        // 描画トグル。行が無い旧ファイルは Reset() の既定値のまま読まれる。
        stream << "RENDER_SSAO " << (settings.SsaoEnabled() ? 1 : 0) << '\n';
        stream << "RENDER_SSR " << (settings.SsrEnabled() ? 1 : 0) << '\n';
        stream << "RENDER_TAA " << (settings.TaaEnabled() ? 1 : 0) << '\n';
        stream << "RENDER_DEPTH_PREPASS "
            << (settings.DepthPrepassEnabled() ? 1 : 0) << '\n';

        const ProjectSettings::ScreenSpaceSettings& screen = settings.ScreenSpace();
        stream << std::setprecision(9);
        stream << "RENDER_SSAO_RADIUS " << screen.ssao_radius << '\n';
        stream << "RENDER_SSAO_INTENSITY " << screen.ssao_intensity << '\n';
        stream << "RENDER_SSAO_POWER " << screen.ssao_power << '\n';
        stream << "RENDER_SSAO_THIN_OCCLUDER " << screen.ssao_thin_occluder << '\n';
        stream << "RENDER_SSAO_SLICE_COUNT " << screen.ssao_slice_count << '\n';
        stream << "RENDER_SSAO_STEP_COUNT " << screen.ssao_step_count << '\n';
        stream << "RENDER_SSAO_FADE_START " << screen.ssao_fade_start << '\n';
        stream << "RENDER_SSAO_FADE_END " << screen.ssao_fade_end << '\n';
        stream << "RENDER_SSAO_NORMAL_BIAS " << screen.ssao_normal_bias << '\n';
        stream << "RENDER_SSAO_BLUR_SHARPNESS " << screen.ssao_blur_sharpness << '\n';
        stream << "RENDER_SSAO_BLUR_ENABLED " << (screen.ssao_blur_enabled ? 1 : 0) << '\n';
        stream << "RENDER_SSR_MAX_DISTANCE " << screen.ssr_max_distance << '\n';
        stream << "RENDER_SSR_THICKNESS " << screen.ssr_thickness << '\n';
        stream << "RENDER_SSR_STRIDE " << screen.ssr_stride << '\n';
        stream << "RENDER_SSR_MAX_STEP " << screen.ssr_max_step << '\n';
        stream << "RENDER_SSR_REFINE_STEP " << screen.ssr_refine_step << '\n';
        stream << "RENDER_SSR_MAX_ROUGHNESS " << screen.ssr_max_roughness << '\n';
        stream << "RENDER_SSR_INTENSITY " << screen.ssr_intensity << '\n';
        stream << "RENDER_SSR_EDGE_FADE " << screen.ssr_edge_fade << '\n';
        stream << "RENDER_SSR_RAY_BIAS " << screen.ssr_ray_bias << '\n';
        stream << "RENDER_SSR_RESOLVE_RADIUS " << screen.ssr_resolve_radius << '\n';
        stream << "RENDER_SSR_RESOLVE_TAP_COUNT " << screen.ssr_resolve_tap_count << '\n';
        stream << "RENDER_TAA_BLEND " << screen.taa_blend << '\n';
        stream << "RENDER_TAA_VARIANCE_GAMMA " << screen.taa_variance_gamma << '\n';
        stream << "RENDER_TAA_SHARPNESS " << screen.taa_sharpness << '\n';
        stream << "RENDER_TAA_MAX_VELOCITY " << screen.taa_max_velocity << '\n';

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
        ProjectSettings::ScreenSpaceSettings& screen = settings.MutableScreenSpace();
        const auto parse_float = [](const std::string& line, float& value)
        {
            std::istringstream value_stream(line);
            value_stream.imbue(std::locale::classic());
            float parsed = value;
            if (value_stream >> parsed) value = parsed;
        };
        const auto parse_int = [](const std::string& line, int& value)
        {
            std::istringstream value_stream(line);
            value_stream.imbue(std::locale::classic());
            int parsed = value;
            if (value_stream >> parsed) value = parsed;
        };
        const auto parse_bool = [](const std::string& line, bool& value)
        {
            std::istringstream value_stream(line);
            value_stream.imbue(std::locale::classic());
            int parsed = value ? 1 : 0;
            if (value_stream >> parsed) value = parsed != 0;
        };
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
            else if (keyword == "EDITOR_SHOW_GAME_TEMPLATE_COMPONENTS")
            {
                std::istringstream value_stream(line);
                int enabled = settings.ShowGameTemplateComponents() ? 1 : 0;
                if (value_stream >> enabled)
                    settings.SetShowGameTemplateComponents(enabled != 0);
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
            else if (keyword == "RENDER_SSAO_RADIUS") parse_float(line, screen.ssao_radius);
            else if (keyword == "RENDER_SSAO_INTENSITY") parse_float(line, screen.ssao_intensity);
            else if (keyword == "RENDER_SSAO_POWER") parse_float(line, screen.ssao_power);
            else if (keyword == "RENDER_SSAO_THIN_OCCLUDER") parse_float(line, screen.ssao_thin_occluder);
            else if (keyword == "RENDER_SSAO_SLICE_COUNT") parse_int(line, screen.ssao_slice_count);
            else if (keyword == "RENDER_SSAO_STEP_COUNT") parse_int(line, screen.ssao_step_count);
            else if (keyword == "RENDER_SSAO_FADE_START") parse_float(line, screen.ssao_fade_start);
            else if (keyword == "RENDER_SSAO_FADE_END") parse_float(line, screen.ssao_fade_end);
            else if (keyword == "RENDER_SSAO_NORMAL_BIAS") parse_float(line, screen.ssao_normal_bias);
            else if (keyword == "RENDER_SSAO_BLUR_SHARPNESS") parse_float(line, screen.ssao_blur_sharpness);
            else if (keyword == "RENDER_SSAO_BLUR_ENABLED") parse_bool(line, screen.ssao_blur_enabled);
            else if (keyword == "RENDER_SSR_MAX_DISTANCE") parse_float(line, screen.ssr_max_distance);
            else if (keyword == "RENDER_SSR_THICKNESS") parse_float(line, screen.ssr_thickness);
            else if (keyword == "RENDER_SSR_STRIDE") parse_float(line, screen.ssr_stride);
            else if (keyword == "RENDER_SSR_MAX_STEP") parse_int(line, screen.ssr_max_step);
            else if (keyword == "RENDER_SSR_REFINE_STEP") parse_int(line, screen.ssr_refine_step);
            else if (keyword == "RENDER_SSR_MAX_ROUGHNESS") parse_float(line, screen.ssr_max_roughness);
            else if (keyword == "RENDER_SSR_INTENSITY") parse_float(line, screen.ssr_intensity);
            else if (keyword == "RENDER_SSR_EDGE_FADE") parse_float(line, screen.ssr_edge_fade);
            else if (keyword == "RENDER_SSR_RAY_BIAS") parse_float(line, screen.ssr_ray_bias);
            else if (keyword == "RENDER_SSR_RESOLVE_RADIUS") parse_float(line, screen.ssr_resolve_radius);
            else if (keyword == "RENDER_SSR_RESOLVE_TAP_COUNT") parse_int(line, screen.ssr_resolve_tap_count);
            else if (keyword == "RENDER_TAA_BLEND") parse_float(line, screen.taa_blend);
            else if (keyword == "RENDER_TAA_VARIANCE_GAMMA") parse_float(line, screen.taa_variance_gamma);
            else if (keyword == "RENDER_TAA_SHARPNESS") parse_float(line, screen.taa_sharpness);
            else if (keyword == "RENDER_TAA_MAX_VELOCITY") parse_float(line, screen.taa_max_velocity);
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
