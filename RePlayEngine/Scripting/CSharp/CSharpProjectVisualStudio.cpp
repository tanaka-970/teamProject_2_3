#include "CSharpProject.h"

#include "../Core/ScriptLanguage.h"
#include "../Core/ScriptTypes.h"
#include "../Core/ScriptValue.h"
#include "../../Reflection/Registry/TypeGUID.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <random>
#include <regex>
#include <sstream>
#include <system_error>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <string.h>
#endif
#include "CSharpProjectInternal.h"

namespace ReplayEngine::Scripting::CSharp
{
    using namespace Detail;

    namespace
    {
        std::filesystem::path FindVisualStudioExecutable()
        {
#ifdef _WIN32
            const std::array<std::filesystem::path, 8> candidates =
            {
                L"C:\\Program Files\\Microsoft Visual Studio\\18\\Community\\Common7\\IDE\\devenv.exe",
                L"C:\\Program Files\\Microsoft Visual Studio\\18\\Professional\\Common7\\IDE\\devenv.exe",
                L"C:\\Program Files\\Microsoft Visual Studio\\18\\Enterprise\\Common7\\IDE\\devenv.exe",
                L"C:\\Program Files\\Microsoft Visual Studio\\17\\Community\\Common7\\IDE\\devenv.exe",
                L"C:\\Program Files\\Microsoft Visual Studio\\17\\Professional\\Common7\\IDE\\devenv.exe",
                L"C:\\Program Files\\Microsoft Visual Studio\\17\\Enterprise\\Common7\\IDE\\devenv.exe",
                L"C:\\Program Files (x86)\\Microsoft Visual Studio\\2019\\Community\\Common7\\IDE\\devenv.exe",
                L"devenv.exe",
            };
            std::error_code error;
            for (const std::filesystem::path& candidate : candidates)
            {
                if (candidate.filename() == candidate)
                {
                    return candidate;
                }
                if (std::filesystem::exists(candidate, error) && !error)
                {
                    return candidate;
                }
                error.clear();
            }
#endif
            return {};
        }

#ifdef _WIN32
        bool IsVisualStudioRunning()
        {
            const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
            if (snapshot == INVALID_HANDLE_VALUE) return false;

            PROCESSENTRY32W entry{};
            entry.dwSize = sizeof(entry);
            bool found = false;
            if (Process32FirstW(snapshot, &entry))
            {
                do
                {
                    if (_wcsicmp(entry.szExeFile, L"devenv.exe") == 0)
                    {
                        found = true;
                        break;
                    }
                } while (Process32NextW(snapshot, &entry));
            }
            CloseHandle(snapshot);
            return found;
        }

        // 指定パスから上へ辿って統合 Solution (3dgp.sln) を探す。
        //
        // Solution Explorer にツリーを出すには Solution が開いている
        // 必要がある。3dgp.sln には C++ と C# のプロジェクトが両方
        // 入っているので、これ 1 つ開けば .cs も見える。
        std::filesystem::path FindPrimarySolution(const std::filesystem::path& start)
        {
            std::error_code error;
            std::filesystem::path directory = start;
            if (!std::filesystem::is_directory(directory, error) || error)
            {
                directory = start.parent_path();
            }
            error.clear();

            for (int depth = 0; depth < 8 && !directory.empty(); ++depth)
            {
                const std::filesystem::path candidate = directory / "3dgp.sln";
                if (std::filesystem::exists(candidate, error) && !error) return candidate;
                error.clear();

                const std::filesystem::path parent = directory.parent_path();
                if (parent == directory) break;
                directory = parent;
            }
            return {};
        }

        bool LaunchVisualStudio(const std::filesystem::path& executable,
            const std::wstring& arguments, bool wait_for_idle)
        {
            const std::wstring executable_text = executable.wstring();

            SHELLEXECUTEINFOW info{};
            info.cbSize = sizeof(info);
            info.fMask = SEE_MASK_NOCLOSEPROCESS;
            info.lpVerb = L"open";
            info.lpFile = executable_text.c_str();
            info.lpParameters = arguments.c_str();
            info.nShow = SW_SHOWNORMAL;

            if (!ShellExecuteExW(&info)) return false;

            if (info.hProcess != nullptr)
            {
                // Solution を開く時だけ、Visual Studio のメッセージループが
                // 立ち上がるまで待つ。待たずに /edit を投げると、
                // 起動しきる前に届いて 2 つ目のインスタンスが開くことがある。
                if (wait_for_idle) WaitForInputIdle(info.hProcess, 60000);
                CloseHandle(info.hProcess);
            }
            return true;
        }
#endif
    }

    bool CSharpProject::OpenVisualStudio(const std::filesystem::path& file,
        int line, std::string& error)
    {
#ifdef _WIN32
        const std::filesystem::path executable = FindVisualStudioExecutable();
        if (executable.empty())
        {
            error = "Visual Studio (devenv.exe) could not be located.";
            return false;
        }

        // devenv /edit はファイル単体を開くコマンドで、Solution が
        // 開いていない Visual Studio では「その他のファイル」扱いになり、
        // Solution Explorer にツリーが出ない。
        //
        // devenv が 1 つも起動していない場合は、先に統合 Solution
        // (3dgp.sln) を開いてから /edit する。3dgp.sln には C++ と
        // C# のプロジェクトが両方入っているので、これ 1 つで
        // .cs も Solution Explorer から辿れる。
        if (!IsVisualStudioRunning())
        {
            const std::filesystem::path solution = FindPrimarySolution(file);
            if (!solution.empty())
            {
                LaunchVisualStudio(executable, Quote(solution), true);
            }
        }

        std::wstring arguments;
        if (line > 0)
        {
            arguments = L"/edit " + Quote(file) + L" /command " +
                QuoteText(L"Edit.Goto " + std::to_wstring(line));
        }
        else
        {
            arguments = L"/edit " + Quote(file);
        }

        if (LaunchVisualStudio(executable, arguments, false)) return true;

        error = "Visual Studio could not be started: " +
            executable.generic_u8string();
        return false;
#else
        (void)file;
        (void)line;
        error = "Visual Studio integration is only available on Windows.";
        return false;
#endif
    }
}
