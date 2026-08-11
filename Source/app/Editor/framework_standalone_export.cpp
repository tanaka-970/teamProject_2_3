#include "framework.h"

#include "../../../RePlayEngine/Scripting/CSharp/CSharpProject.h"

#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace
{
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

    std::string LowerCopy(std::string text)
    {
        for (char& character : text)
        {
            character = static_cast<char>(
                std::tolower(static_cast<unsigned char>(character)));
        }
        return text;
    }

    std::string SafeGameName(std::string name)
    {
        name = TrimCopy(std::move(name));
        for (char& character : name)
        {
            if (character == '<' || character == '>' || character == ':' ||
                character == '"' || character == '/' || character == '\\' ||
                character == '|' || character == '?' || character == '*')
            {
                character = '_';
            }
        }
        while (!name.empty() && (name.back() == ' ' || name.back() == '.'))
            name.pop_back();
        return name.empty() ? "MyGame" : name;
    }

    template<std::size_t Size>
    void CopyToBuffer(char (&buffer)[Size], const std::string& text)
    {
        const std::size_t count = (std::min)(Size - 1, text.size());
        std::copy_n(text.data(), count, buffer);
        buffer[count] = '\0';
    }

    std::filesystem::path BrowseFolder(HWND owner)
    {
        BROWSEINFOW info{};
        info.hwndOwner = owner;
        info.lpszTitle = L"ゲームの書き出し先フォルダー";
        info.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
        PIDLIST_ABSOLUTE item = SHBrowseForFolderW(&info);
        if (item == nullptr) return {};

        wchar_t path[MAX_PATH]{};
        const BOOL ok = SHGetPathFromIDListW(item, path);
        CoTaskMemFree(item);
        return ok ? std::filesystem::path(path) : std::filesystem::path{};
    }

    std::filesystem::path BrowseSceneFile(HWND owner,
        const std::filesystem::path& current)
    {
        wchar_t filename[32768]{};
        if (!current.empty())
        {
            const std::wstring initial = std::filesystem::absolute(current).wstring();
            wcsncpy_s(filename, initial.c_str(), _TRUNCATE);
        }

        static const wchar_t filter[] =
            L"RePlayEngine Scene (*.replayscene)\0*.replayscene\0All Files (*.*)\0*.*\0\0";
        OPENFILENAMEW dialog{};
        dialog.lStructSize = sizeof(dialog);
        dialog.hwndOwner = owner;
        dialog.lpstrFile = filename;
        dialog.nMaxFile = static_cast<DWORD>(_countof(filename));
        dialog.lpstrFilter = filter;
        dialog.lpstrDefExt = L"replayscene";
        dialog.lpstrTitle = L"最初に開く Scene";
        dialog.Flags = OFN_EXPLORER | OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST |
            OFN_FILEMUSTEXIST;
        return GetOpenFileNameW(&dialog)
            ? std::filesystem::path(filename)
            : std::filesystem::path{};
    }

    bool IsDirectoryEmpty(const std::filesystem::path& folder)
    {
        std::error_code error;
        return std::filesystem::is_directory(folder, error) && !error &&
            std::filesystem::directory_iterator(folder, error) ==
            std::filesystem::directory_iterator{} && !error;
    }

    bool ContainsParentTraversal(const std::filesystem::path& path)
    {
        for (const std::filesystem::path& part : path)
        {
            if (part == "..") return true;
        }
        return false;
    }

    bool IsSameOrAncestorOf(const std::filesystem::path& candidate,
        const std::filesystem::path& child)
    {
        std::error_code error;
        const std::filesystem::path child_absolute =
            std::filesystem::absolute(child, error);
        if (error) return false;
        const std::filesystem::path candidate_absolute =
            std::filesystem::absolute(candidate, error);
        if (error) return false;
        const std::filesystem::path relative = std::filesystem::relative(
            child_absolute, candidate_absolute, error);
        if (error) return false;
        if (relative.is_absolute()) return false;
        return relative.empty() || !ContainsParentTraversal(relative);
    }

    bool ShouldSkipResourceEntry(const std::filesystem::path& relative)
    {
        if (relative.empty()) return false;
        for (const std::filesystem::path& part : relative)
        {
            const std::string name = LowerCopy(part.generic_u8string());
            if (name == ".replay_cache") return true;
        }

        const std::string filename = LowerCopy(relative.filename().generic_u8string());
        return filename == "imgui.ini" ||
            filename.size() >= 4 && filename.substr(filename.size() - 4) == ".bak" ||
            filename.size() >= 19 &&
                filename.substr(filename.size() - 19) == ".replaymaterial.bak";
    }

    bool CopyFileChecked(const std::filesystem::path& source,
        const std::filesystem::path& destination, std::vector<std::string>& errors)
    {
        std::error_code error;
        std::filesystem::create_directories(destination.parent_path(), error);
        if (error)
        {
            errors.push_back("フォルダーを作成できません: " +
                destination.parent_path().generic_u8string());
            return false;
        }

        std::filesystem::copy_file(source, destination,
            std::filesystem::copy_options::overwrite_existing, error);
        if (error)
        {
            errors.push_back("コピーに失敗: " + source.generic_u8string() +
                " -> " + destination.generic_u8string());
            return false;
        }
        return true;
    }

    void CopyDirectoryFiltered(const std::filesystem::path& source_root,
        const std::filesystem::path& destination_root,
        const std::function<bool(const std::filesystem::path&)>& skip,
        std::vector<std::string>& errors)
    {
        std::error_code error;
        if (!std::filesystem::is_directory(source_root, error) || error)
        {
            errors.push_back("フォルダーが見つかりません: " +
                source_root.generic_u8string());
            return;
        }

        std::filesystem::create_directories(destination_root, error);
        if (error)
        {
            errors.push_back("フォルダーを作成できません: " +
                destination_root.generic_u8string());
            return;
        }

        for (std::filesystem::recursive_directory_iterator it(
            source_root, std::filesystem::directory_options::skip_permission_denied, error),
            end; !error && it != end; it.increment(error))
        {
            const std::filesystem::path relative =
                std::filesystem::relative(it->path(), source_root, error);
            if (error)
            {
                error.clear();
                continue;
            }
            if (skip && skip(relative))
            {
                if (it->is_directory(error)) it.disable_recursion_pending();
                error.clear();
                continue;
            }

            const std::filesystem::path destination = destination_root / relative;
            if (it->is_directory(error))
            {
                std::filesystem::create_directories(destination, error);
                if (error)
                {
                    errors.push_back("フォルダーを作成できません: " +
                        destination.generic_u8string());
                    error.clear();
                }
                continue;
            }
            if (it->is_regular_file(error))
            {
                CopyFileChecked(it->path(), destination, errors);
                error.clear();
            }
        }
        if (error)
        {
            errors.push_back("フォルダー走査に失敗: " +
                source_root.generic_u8string());
        }
    }

    void CopyDirectoryIfExists(const std::filesystem::path& source_root,
        const std::filesystem::path& destination_root,
        std::vector<std::string>& errors)
    {
        std::error_code error;
        if (!std::filesystem::is_directory(source_root, error) || error) return;
        CopyDirectoryFiltered(source_root, destination_root,
            [](const std::filesystem::path&) { return false; }, errors);
    }
}

void framework::open_export_game_dialog()
{
    const std::string default_name = object_scene_path.stem().empty()
        ? "MyGame"
        : object_scene_path.stem().u8string();
    CopyToBuffer(export_game_name, SafeGameName(default_name));
    CopyToBuffer(export_folder, saved_path("Build").u8string());
    CopyToBuffer(export_startup_scene, object_scene_path.u8string());

    export_status.clear();
    export_errors.clear();
    export_pending_root.clear();
    export_pending_scene.clear();
    export_overwrite_prompt_open = false;
    export_game_dialog_open = true;
}

bool framework::export_standalone_game(const std::filesystem::path& export_root,
    const std::filesystem::path& startup_scene, bool overwrite_existing)
{
    export_errors.clear();
    export_status.clear();

    const std::string game_name = SafeGameName(export_game_name);
    const std::filesystem::path destination =
        export_root / std::filesystem::u8path(game_name);

    if (export_root.empty())
    {
        export_errors.push_back("書き出し先フォルダーを指定してください");
        return false;
    }
    if (IsSameOrAncestorOf(destination, content_root_path()))
    {
        export_errors.push_back("プロジェクト本体またはその親フォルダーへは書き出せません: " +
            destination.generic_u8string());
        return false;
    }
    if (IsSameOrAncestorOf(content_path("resources"), destination) ||
        IsSameOrAncestorOf(content_path("Shader"), destination))
    {
        export_errors.push_back("コピー元フォルダーの内側へは書き出せません: " +
            destination.generic_u8string());
        return false;
    }

    std::error_code error;
    if (std::filesystem::exists(destination, error) && !error &&
        !IsDirectoryEmpty(destination))
    {
        if (!overwrite_existing)
        {
            export_errors.push_back("既存フォルダーが空ではありません: " +
                destination.generic_u8string());
            return false;
        }
        std::filesystem::remove_all(destination, error);
        if (error)
        {
            export_errors.push_back("既存フォルダーを削除できません: " +
                destination.generic_u8string());
            return false;
        }
    }

    std::filesystem::create_directories(destination, error);
    if (error)
    {
        export_errors.push_back("書き出し先フォルダーを作成できません: " +
            destination.generic_u8string());
        return false;
    }

    const std::filesystem::path source_exe_release =
        content_path(std::filesystem::path("x64") / "Release" / "3dgp.exe");
    const std::filesystem::path source_exe_debug =
        content_path(std::filesystem::path("x64") / "Debug" / "3dgp.exe");
    const std::filesystem::path source_exe =
        std::filesystem::exists(source_exe_release, error) && !error
        ? source_exe_release
        : source_exe_debug;
    error.clear();
    if (!std::filesystem::exists(source_exe, error) || error)
    {
        export_errors.push_back("コピー元 exe が見つかりません: " +
            source_exe_release.generic_u8string());
    }
    else
    {
        CopyFileChecked(source_exe,
            destination / std::filesystem::u8path(game_name + ".exe"), export_errors);
    }

    CopyDirectoryFiltered(content_path("resources"), destination / "resources",
        [](const std::filesystem::path& relative)
        {
            return ShouldSkipResourceEntry(relative);
        },
        export_errors);

    CopyDirectoryFiltered(
        content_path(std::filesystem::path("Shader") / "compiled"),
        destination / "Shader" / "compiled",
        [](const std::filesystem::path&) { return false; },
        export_errors);

    namespace CSharp = ReplayEngine::Scripting::CSharp;
    for (const std::string configuration : { "Release", "Debug" })
    {
        CopyDirectoryIfExists(
            CSharp::CSharpProject::ManagedApiAssemblyPath(
                content_root_path(), configuration).parent_path(),
            CSharp::CSharpProject::ManagedApiAssemblyPath(
                destination, configuration).parent_path(),
            export_errors);
        CopyDirectoryIfExists(
            CSharp::CSharpProject::GameScriptsAssemblyPath(
                content_root_path(), configuration).parent_path(),
            CSharp::CSharpProject::GameScriptsAssemblyPath(
                destination, configuration).parent_path(),
            export_errors);
    }

    const std::filesystem::path startup_absolute = startup_scene.is_absolute()
        ? startup_scene.lexically_normal()
        : content_path(startup_scene);
    const std::filesystem::path startup_relative =
        std::filesystem::relative(startup_absolute, content_root_path(), error);
    if (error || ContainsParentTraversal(startup_relative))
    {
        export_errors.push_back("Startup Scene はプロジェクト内を指定してください: " +
            startup_absolute.generic_u8string());
    }
    else
    {
        const std::filesystem::path packaged_startup = destination / startup_relative;
        if (!std::filesystem::exists(packaged_startup, error) || error)
        {
            export_errors.push_back("Startup Scene が書き出し結果に含まれていません: " +
                startup_relative.generic_u8string());
        }
        error.clear();

        std::ofstream config(
            destination / std::filesystem::u8path(game_name + ".replaygame"),
            std::ios::binary | std::ios::trunc);
        if (!config)
        {
            export_errors.push_back(".replaygame を作成できません");
        }
        else
        {
            config << "REPLAY_GAME 1\n";
            config << "NAME " << game_name << '\n';
            config << "STARTUP_SCENE " << startup_relative.generic_u8string() << '\n';
            config << "WINDOW " << client_width << ' ' << client_height << '\n';
            config << "FULLSCREEN 0\n";
            if (!config) export_errors.push_back(".replaygame の書き込みに失敗しました");
        }
    }

    if (!export_errors.empty())
    {
        export_status = "書き出しに失敗しました";
        return false;
    }

    export_status = "書き出しました: " + destination.generic_u8string();
    ShellExecuteW(nullptr, L"open", destination.wstring().c_str(),
        nullptr, nullptr, SW_SHOWNORMAL);
    return true;
}

void framework::draw_export_game_dialog()
{
    if (!export_game_dialog_open && !export_overwrite_prompt_open) return;

    if (export_game_dialog_open) ImGui::OpenPopup(u8"ゲームを書き出す");
    ImGui::SetNextWindowSize(ImVec2(640.0f, 0.0f), ImGuiCond_FirstUseEver);
    if (ImGui::BeginPopupModal(u8"ゲームを書き出す", &export_game_dialog_open,
        ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::InputText(u8"ゲーム名", export_game_name, sizeof(export_game_name));
        ImGui::InputText(u8"書き出し先", export_folder, sizeof(export_folder));
        ImGui::SameLine();
        if (ImGui::Button(u8"選択##ExportFolder"))
        {
            const std::filesystem::path selected = BrowseFolder(hwnd);
            if (!selected.empty()) CopyToBuffer(export_folder, selected.u8string());
        }

        ImGui::InputText(u8"最初の Scene", export_startup_scene,
            sizeof(export_startup_scene));
        ImGui::SameLine();
        if (ImGui::Button(u8"選択##StartupScene"))
        {
            const std::filesystem::path selected = BrowseSceneFile(
                hwnd, std::filesystem::u8path(export_startup_scene));
            if (!selected.empty()) CopyToBuffer(export_startup_scene, selected.u8string());
        }

        if (!export_status.empty()) ImGui::TextWrapped("%s", export_status.c_str());
        for (const std::string& item : export_errors)
            ImGui::BulletText("%s", item.c_str());

        ImGui::Separator();
        if (export_exporting)
        {
            ImGui::TextDisabled(u8"書き出し中...");
        }
        else if (ImGui::Button(u8"書き出す"))
        {
            export_pending_root = std::filesystem::u8path(export_folder);
            export_pending_scene = std::filesystem::u8path(export_startup_scene);
            const std::filesystem::path destination =
                export_pending_root /
                std::filesystem::u8path(SafeGameName(export_game_name));
            std::error_code error;
            if (std::filesystem::exists(destination, error) && !error &&
                !IsDirectoryEmpty(destination))
            {
                export_overwrite_prompt_open = true;
            }
            else
            {
                export_exporting = true;
                export_standalone_game(export_pending_root, export_pending_scene, false);
                export_exporting = false;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button(u8"閉じる")) export_game_dialog_open = false;

        ImGui::EndPopup();
    }

    if (export_overwrite_prompt_open)
        ImGui::OpenPopup(u8"既存フォルダーの上書き");
    if (ImGui::BeginPopupModal(u8"既存フォルダーの上書き", nullptr,
        ImGuiWindowFlags_AlwaysAutoResize))
    {
        const std::filesystem::path destination =
            export_pending_root /
            std::filesystem::u8path(SafeGameName(export_game_name));
        ImGui::TextWrapped(u8"既存フォルダーを上書きしますか？");
        ImGui::TextWrapped("%s", destination.generic_u8string().c_str());

        if (ImGui::Button(u8"上書き"))
        {
            export_exporting = true;
            export_standalone_game(export_pending_root, export_pending_scene, true);
            export_exporting = false;
            export_overwrite_prompt_open = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(u8"キャンセル"))
        {
            export_overwrite_prompt_open = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}
