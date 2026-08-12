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

}
