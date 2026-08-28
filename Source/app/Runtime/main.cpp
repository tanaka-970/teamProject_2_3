// Runtime main のうち「起動設定とコマンドライン解釈」を持つ。
//
//   main.cpp                   … 起動設定・コマンドライン解釈（このファイル）
//   mainApplication.cpp        … window_procedure / WinMain とアプリケーション実行
//   mainDiagnostics.cpp        … D3D11 / DXGI Live Object 診断
//   mainValidation.cpp         … 検証共通処理、結果出力、検証ディスパッチ
//   mainValidationScene.cpp    … アセット・Prefab 検証
//   mainValidationLandscape.cpp … Landscape 検証
//   mainValidationSceneWorld.cpp … 大規模Scene・Scene・永続化検証
//   mainValidationRuntime.cpp  … Runtime Component 検証
//   mainValidationPhysics.cpp  … Physics 検証
//   mainValidationMotion.cpp   … Motion / PropertyLink 検証
//   mainInternal.h             … 上記内部実装の共有宣言
//
// 関数本体は分割前のまま移動し、起動順と検証の呼び出し順は変更しない。
#include <time.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdint>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#if defined(_DEBUG)
#include <dxgidebug.h>
#endif

#include "framework.h"
#include "mainInternal.h"

using ReplayEngine::Runtime::Detail::ValidationFolder;
using ReplayEngine::Runtime::Detail::RunHeadlessLargeSceneValidation;
using ReplayEngine::Runtime::Detail::RunHeadlessSceneValidation;
using ReplayEngine::Runtime::Detail::RunHeadlessMaterialValidation;
using ReplayEngine::Runtime::Detail::RunHeadlessLandscapeValidation;
using ReplayEngine::Runtime::Detail::RunHeadlessPrefabValidation;
using ReplayEngine::Runtime::Detail::RunHeadlessHandleValidation;
using ReplayEngine::Runtime::Detail::RunHeadlessCameraComponentValidation;
using ReplayEngine::Runtime::Detail::RunHeadlessPlayerSpeedValidation;
using ReplayEngine::Runtime::Detail::RunHeadlessSerializationValidation;

namespace ReplayEngine::Runtime::Detail
{
    bool NormalizeScreenSpaceOverride(const std::string& text,
        std::string& normalized, std::string& warning, std::string& error)
    {
        const std::size_t separator = text.find('=');
        if (separator == std::string::npos || separator == 0 || separator + 1 >= text.size())
        {
            error = "--screen-space requires key=value";
            return false;
        }

        const std::string key = text.substr(0, separator);
        const std::string value_text = text.substr(separator + 1);
        enum class ValueKind { Float, Integer, Boolean };
        struct Range final
        {
            ValueKind kind;
            double minimum;
            double maximum;
        };
        const auto range_for = [&key](Range& range) noexcept
        {
            if (key == "ssao.radius") range = { ValueKind::Float, 0.05, 4.0 };
            else if (key == "ssao.intensity") range = { ValueKind::Float, 0.0, 1.0 };
            else if (key == "ssao.power") range = { ValueKind::Float, 0.5, 4.0 };
            else if (key == "ssao.thin_occluder") range = { ValueKind::Float, 0.0, 1.0 };
            else if (key == "ssao.slice_count") range = { ValueKind::Integer, 1.0, 8.0 };
            else if (key == "ssao.step_count") range = { ValueKind::Integer, 2.0, 12.0 };
            else if (key == "ssao.fade_start") range = { ValueKind::Float, 1.0, 400.0 };
            else if (key == "ssao.fade_end") range = { ValueKind::Float, 2.0, 800.0 };
            else if (key == "ssao.normal_bias") range = { ValueKind::Float, 0.0, 2.0 };
            else if (key == "ssao.blur_sharpness") range = { ValueKind::Float, 0.0, 1.0 };
            else if (key == "ssao.blur_enabled") range = { ValueKind::Boolean, 0.0, 1.0 };
            else if (key == "ssr.max_distance") range = { ValueKind::Float, 1.0, 200.0 };
            else if (key == "ssr.thickness") range = { ValueKind::Float, 0.01, 2.0 };
            else if (key == "ssr.stride") range = { ValueKind::Float, 1.0, 16.0 };
            else if (key == "ssr.max_step") range = { ValueKind::Integer, 4.0, 64.0 };
            else if (key == "ssr.refine_step") range = { ValueKind::Integer, 0.0, 8.0 };
            else if (key == "ssr.max_roughness") range = { ValueKind::Float, 0.05, 1.0 };
            else if (key == "ssr.intensity") range = { ValueKind::Float, 0.0, 2.0 };
            else if (key == "ssr.edge_fade") range = { ValueKind::Float, 0.01, 0.4 };
            else if (key == "ssr.ray_bias") range = { ValueKind::Float, 0.0, 4.0 };
            else if (key == "ssr.resolve_radius") range = { ValueKind::Float, 0.0, 40.0 };
            else if (key == "ssr.resolve_tap_count") range = { ValueKind::Integer, 1.0, 16.0 };
            else if (key == "taa.blend") range = { ValueKind::Float, 0.0, 0.98 };
            else if (key == "taa.variance_gamma") range = { ValueKind::Float, 0.25, 3.0 };
            else if (key == "taa.sharpness") range = { ValueKind::Float, 0.0, 1.0 };
            else if (key == "taa.max_velocity") range = { ValueKind::Float, 4.0, 200.0 };
            else if (key == "ssao.enabled" || key == "ssr.enabled" || key == "taa.enabled")
                range = { ValueKind::Boolean, 0.0, 1.0 };
            else
                return false;
            return true;
        };

        Range range{};
        if (!range_for(range))
        {
            error = "--screen-space の未知のキー: " + key;
            return false;
        }
        if (range.kind == ValueKind::Boolean)
        {
            if (value_text != "0" && value_text != "1")
            {
                error = "--screen-space の bool 値は 0 または 1 です: " + key;
                return false;
            }
            normalized = key + "=" + value_text;
            return true;
        }
        try
        {
            std::size_t consumed = 0;
            const double parsed = range.kind == ValueKind::Integer
                ? static_cast<double>(std::stoll(value_text, &consumed, 10))
                : std::stod(value_text, &consumed);
            if (consumed != value_text.size() || !std::isfinite(parsed))
            {
                error = "--screen-space の数値が不正です: " + key;
                return false;
            }
            const double clamped = (std::max)(range.minimum, (std::min)(range.maximum, parsed));
            if (clamped != parsed)
            {
                std::ostringstream message;
                message << "--screen-space " << key << " を " << range.minimum
                    << ".." << range.maximum << " に clamp しました。";
                warning = message.str();
            }
            if (range.kind == ValueKind::Integer)
                normalized = key + "=" + std::to_string(static_cast<int>(clamped));
            else
            {
                std::ostringstream value;
                value << std::setprecision(9) << clamped;
                normalized = key + "=" + value.str();
            }
            return true;
        }
        catch (...)
        {
            error = "--screen-space の数値が不正です: " + key;
            return false;
        }
    }

    // --game が指定されたら、Startup Scene から Runtime を開始する。
    //
    // 既定（引数なし）は Editor 起動。
    // Editor 起動で Runtime World を有効にすると、編集対象が空の World へ
    // すり替わり、配置した内容と保存内容が食い違う。
    // --validate-shutdown : 終了時のリソース解放を確かめる。
    //
    // D3D デバイスが要るのでヘッドレス Validation には載せられない。
    // smoke test と同じ「N フレーム描画して終了」の経路を使い、
    // 終了後の Live Object Report で合否を見る。
    bool ParseShutdownRegression(const char* command_line)
    {
        std::istringstream arguments(command_line != nullptr ? command_line : "");
        std::string token;
        while (arguments >> token)
        {
            if (token == "--validate-shutdown") return true;
        }
        return false;
    }

    bool ParseStartupSceneBoot(const char* command_line)
    {
        std::istringstream arguments(command_line != nullptr ? command_line : "");
        std::string token;
        while (arguments >> token)
        {
            if (token == "--game") return true;
        }
        return false;
    }

    bool ParseCaptureFrame(const char* command_line, std::string& capture_name)
    {
        capture_name = "claude";

        std::istringstream arguments(command_line != nullptr ? command_line : "");
        std::string token;
        while (arguments >> token)
        {
            if (token != "--capture-frame") continue;

            std::string candidate;
            if (!(arguments >> candidate) || candidate.rfind("--", 0) == 0)
                return true;

            const bool contains_control_character = std::any_of(
                candidate.begin(), candidate.end(), [](unsigned char character) noexcept
                {
                    return std::iscntrl(character) != 0;
                });
            const bool contains_invalid_path =
                candidate.find("..") != std::string::npos ||
                candidate.find_first_of("\\/:*?\"<>|") != std::string::npos;
            if (contains_control_character || contains_invalid_path)
            {
                // 撮影名はそのままファイル名になるため、親フォルダへ抜けられる文字や
                // Windows のファイル名に使えない文字は受け入れず、既定名へ戻す。
                return true;
            }

            capture_name = candidate;
            return true;
        }
        return false;
    }

    std::string TrimCopy(std::string text)
    {
        const auto is_space = [](unsigned char c) noexcept
        {
            return std::isspace(c) != 0;
        };
        while (!text.empty() && is_space(static_cast<unsigned char>(text.front())))
            text.erase(text.begin());
        while (!text.empty() && is_space(static_cast<unsigned char>(text.back())))
            text.pop_back();
        return text;
    }

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

    std::string LowerCopy(std::string text)
    {
        for (char& character : text)
        {
            character = static_cast<char>(
                std::tolower(static_cast<unsigned char>(character)));
        }
        return text;
    }

    ExecutableLayout ResolveExecutableLayout()
    {
        ExecutableLayout layout;
        std::array<wchar_t, 32768> executable_path{};
        const DWORD path_length = GetModuleFileNameW(nullptr, executable_path.data(),
            static_cast<DWORD>(executable_path.size()));
        if (path_length > 0 && path_length < executable_path.size())
        {
            layout.executable_path = std::filesystem::path(
                std::wstring(executable_path.data(), path_length));
            layout.executable_directory = layout.executable_path.parent_path();
            layout.content_root = layout.executable_directory;

            std::error_code error;
            const std::filesystem::path packaged_resources =
                layout.executable_directory / "resources";
            if (!std::filesystem::is_directory(packaged_resources, error) || error)
            {
                const std::filesystem::path visual_studio_root =
                    (layout.executable_directory / ".." / "..").lexically_normal();
                error.clear();
                if (std::filesystem::is_directory(
                    visual_studio_root / "resources", error) && !error)
                {
                    layout.content_root = visual_studio_root;
                }
            }
        }
        else
        {
            std::error_code error;
            layout.content_root = std::filesystem::current_path(error);
            layout.executable_directory = layout.content_root;
        }
        return layout;
    }

    std::filesystem::path FindReplayGameFile(const std::filesystem::path& folder,
        std::vector<std::string>& warnings)
    {
        std::error_code error;
        if (!std::filesystem::is_directory(folder, error) || error) return {};

        std::vector<std::filesystem::path> files;
        for (std::filesystem::directory_iterator it(folder, error), end;
            !error && it != end; it.increment(error))
        {
            if (!it->is_regular_file(error)) continue;
            if (LowerCopy(it->path().extension().string()) == ".replaygame")
                files.push_back(it->path());
        }
        std::sort(files.begin(), files.end());
        if (files.size() > 1)
        {
            warnings.push_back("複数の .replaygame があるため先頭を使います: " +
                files.front().filename().generic_u8string());
        }
        return files.empty() ? std::filesystem::path{} : files.front();
    }

    GameLaunchConfig LoadGameLaunchConfig(const std::filesystem::path& executable_directory)
    {
        GameLaunchConfig config;
        config.file = FindReplayGameFile(executable_directory, config.warnings);
        config.file_found = !config.file.empty();
        if (!config.file_found) return config;

        std::ifstream stream(config.file, std::ios::binary);
        if (!stream)
        {
            config.warnings.push_back(".replaygame を開けません。既定値で続行します: " +
                config.file.generic_u8string());
            return config;
        }

        std::string line;
        if (!std::getline(stream, line))
        {
            config.warnings.push_back(".replaygame の先頭行が不正です。既定値で続行します: " +
                config.file.generic_u8string());
            return config;
        }
        // PowerShell の UTF-8 出力などが付ける BOM はヘッダー文字列ではない。
        // 先頭行だけから除去し、2 行目以降のデータはそのまま解釈する。
        StripUtf8Bom(line);
        if (TrimCopy(line) != "REPLAY_GAME 1")
        {
            config.warnings.push_back(".replaygame の先頭行が不正です。既定値で続行します: " +
                config.file.generic_u8string());
            return config;
        }

        config.file_loaded = true;
        int line_number = 1;
        while (std::getline(stream, line))
        {
            ++line_number;
            line = TrimCopy(line);
            if (line.empty() || line.front() == '#') continue;

            std::istringstream parser(line);
            std::string key;
            parser >> key;
            if (key == "NAME")
            {
                std::string rest;
                std::getline(parser, rest);
                rest = TrimCopy(rest);
                if (!rest.empty()) config.name = rest;
                else config.warnings.push_back("NAME が空です。既定名を使います");
            }
            else if (key == "STARTUP_SCENE")
            {
                std::string rest;
                std::getline(parser, rest);
                rest = TrimCopy(rest);
                if (!rest.empty()) config.startup_scene = std::filesystem::u8path(rest);
            }
            else if (key == "WINDOW")
            {
                int width = 0;
                int height = 0;
                if ((parser >> width >> height) && width > 0 && height > 0)
                {
                    config.window_width = static_cast<UINT>((std::max)(width, 1));
                    config.window_height = static_cast<UINT>((std::max)(height, 1));
                }
                else
                {
                    config.warnings.push_back("WINDOW が不正です: line " +
                        std::to_string(line_number));
                }
            }
            else if (key == "FULLSCREEN")
            {
                int value = 0;
                if (parser >> value) config.fullscreen = value != 0;
                else config.warnings.push_back("FULLSCREEN が不正です: line " +
                    std::to_string(line_number));
            }
        }
        return config;
    }

    std::uint32_t ParseAutomatedSmokeTestFrames(const char* command_line)
    {
        std::istringstream arguments(command_line != nullptr ? command_line : "");
        std::string command;
        int frames = 0;
        if (!(arguments >> command) || command != "--smoke-test") return 0;
        if (!(arguments >> frames)) frames = 120;
        return static_cast<std::uint32_t>((std::clamp)(frames, 30, 3600));
    }

    ProfileBenchmarkConfig ParseProfileBenchmark(const char* command_line)
    {
        ProfileBenchmarkConfig config{};
        const std::string raw = command_line != nullptr ? command_line : "";

        // WinMain の LPSTR は executable 名を除いた生コマンドラインなので、
        // istringstream だけでは "path with spaces" を扱えない。
        // この用途に必要な引用符だけを小さく解釈し、既存 CLI の挙動には触れない。
        std::vector<std::string> tokens;
        std::string current;
        bool quoted = false;
        for (std::size_t i = 0; i < raw.size(); ++i)
        {
            const char c = raw[i];
            if (c == '"')
            {
                quoted = !quoted;
                continue;
            }
            if (!quoted && std::isspace(static_cast<unsigned char>(c)) != 0)
            {
                if (!current.empty())
                {
                    tokens.push_back(current);
                    current.clear();
                }
                continue;
            }
            current.push_back(c);
        }
        if (!current.empty()) tokens.push_back(current);
        if (quoted)
        {
            config.valid = false;
            config.error = "profile command line has an unclosed quote";
        }

        auto parse_u32 = [](const std::string& text, std::uint32_t fallback,
            std::uint32_t minimum, std::uint32_t maximum) noexcept
        {
            try
            {
                std::size_t consumed = 0;
                const unsigned long value = std::stoul(text, &consumed, 10);
                if (consumed != text.size()) return fallback;
                return static_cast<std::uint32_t>((std::max)(
                    static_cast<unsigned long>(minimum),
                    (std::min)(static_cast<unsigned long>(maximum), value)));
            }
            catch (...)
            {
                return fallback;
            }
        };

        for (std::size_t i = 0; i < tokens.size(); ++i)
        {
            const std::string& token = tokens[i];
            if (token == "--profile-scene")
            {
                config.requested = true;
                if (i + 1 >= tokens.size() || tokens[i + 1].rfind("--", 0) == 0)
                {
                    config.valid = false;
                    config.error = "--profile-scene requires a scene path";
                    continue;
                }
                config.scene = std::filesystem::u8path(tokens[++i]);
            }
            else if (token == "--frames")
            {
                if (i + 1 < tokens.size())
                    config.frames = parse_u32(tokens[++i], config.frames, 1u, 10000u);
            }
            else if (token == "--warmup")
            {
                if (i + 1 < tokens.size())
                    config.warmup_frames = parse_u32(tokens[++i],
                        config.warmup_frames, 0u, 10000u);
            }
            else if (token == "--render-output")
            {
                if (i + 1 >= tokens.size() || tokens[i + 1].rfind("--", 0) == 0)
                {
                    config.valid = false;
                    config.error = "--render-output requires a value";
                    continue;
                }
                const std::string& value_text = tokens[++i];
                try
                {
                    std::size_t consumed = 0;
                    const unsigned long value = std::stoul(value_text, &consumed, 10);
                    if (consumed != value_text.size() || value > 10ul)
                    {
                        config.valid = false;
                        config.error = "--render-output must be in range 0..10";
                    }
                    else
                    {
                        config.render_output = static_cast<std::uint32_t>(value);
                    }
                }
                catch (...)
                {
                    config.valid = false;
                    config.error = "--render-output must be in range 0..10";
                }
            }
            else if (token == "--out")
            {
                if (i + 1 < tokens.size() && tokens[i + 1].rfind("--", 0) != 0)
                    config.output_name = tokens[++i];
            }
            else if (token == "--screen-space")
            {
                if (i + 1 >= tokens.size() || tokens[i + 1].rfind("--", 0) == 0)
                {
                    config.valid = false;
                    config.error = "--screen-space requires key=value";
                    continue;
                }
                std::string normalized;
                std::string warning;
                std::string error;
                if (!NormalizeScreenSpaceOverride(tokens[++i], normalized, warning, error))
                {
                    config.valid = false;
                    config.error = error;
                    continue;
                }
                config.screen_space_overrides.push_back(std::move(normalized));
                if (!warning.empty()) config.screen_space_warnings.push_back(std::move(warning));
            }
        }

        if (config.requested && config.scene.empty())
        {
            config.valid = false;
            if (config.error.empty()) config.error = "profile scene path is empty";
        }
        return config;
    }

}
