#include "CSharpProjectInternal.h"

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
namespace ReplayEngine::Scripting::CSharp::Detail
{
        std::filesystem::path NormalizeRoot(std::filesystem::path root)
        {
            std::error_code error;
            if (root.empty()) root = std::filesystem::current_path(error);
            if (error) return root.lexically_normal();
            return std::filesystem::absolute(root, error).lexically_normal();
        }

        std::string ReadAllText(const std::filesystem::path& path)
        {
            std::ifstream stream(path, std::ios::binary);
            if (!stream) return std::string();
            return std::string((std::istreambuf_iterator<char>(stream)),
                std::istreambuf_iterator<char>());
        }

        bool WriteTextIfChanged(const std::filesystem::path& path,
            const std::string& text, std::string& error)
        {
            std::error_code filesystem_error;
            std::filesystem::create_directories(path.parent_path(), filesystem_error);
            if (filesystem_error)
            {
                error = "folder create failed: " + path.parent_path().generic_u8string();
                return false;
            }

            if (std::filesystem::exists(path, filesystem_error) &&
                ReadAllText(path) == text)
            {
                return true;
            }

            std::ofstream stream(path, std::ios::binary | std::ios::trunc);
            if (!stream)
            {
                error = "write failed: " + path.generic_u8string();
                return false;
            }
            stream << text;
            return static_cast<bool>(stream);
        }

        bool SourceTreeIsNewer(const std::filesystem::path& source_root,
            const std::filesystem::path& output,
            const std::vector<std::filesystem::path>& dependencies)
        {
            std::error_code error;
            if (!std::filesystem::exists(output, error) || error) return true;
            const auto output_time = std::filesystem::last_write_time(output, error);
            if (error) return true;

            for (const std::filesystem::path& dependency : dependencies)
            {
                const auto dependency_time =
                    std::filesystem::last_write_time(dependency, error);
                if (error || dependency_time > output_time) return true;
            }

            std::filesystem::recursive_directory_iterator it(
                source_root, std::filesystem::directory_options::skip_permission_denied, error);
            const std::filesystem::recursive_directory_iterator end;
            for (; !error && it != end; it.increment(error))
            {
                if (it->is_directory(error))
                {
                    const std::string name = it->path().filename().generic_u8string();
                    if (name == "bin" || name == "obj") it.disable_recursion_pending();
                    continue;
                }
                if (error || !it->is_regular_file(error)) continue;

                const std::string extension = it->path().extension().generic_u8string();
                if (extension != ".cs" && extension != ".csproj" &&
                    extension != ".props" && extension != ".targets")
                    continue;

                const auto source_time = std::filesystem::last_write_time(it->path(), error);
                if (error || source_time > output_time) return true;
            }
            return static_cast<bool>(error);
        }

        std::wstring ToWide(const std::string& text)
        {
            if (text.empty()) return std::wstring();
            const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(),
                static_cast<int>(text.size()), nullptr, 0);
            if (size <= 0) return std::wstring();
            std::wstring result(static_cast<std::size_t>(size), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                result.data(), size);
            return result;
        }

        std::string FromWide(const std::wstring& text)
        {
            if (text.empty()) return std::string();
            const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(),
                static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
            if (size <= 0) return std::string();
            std::string result(static_cast<std::size_t>(size), '\0');
            WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                result.data(), size, nullptr, nullptr);
            return result;
        }

        std::wstring Quote(const std::filesystem::path& path)
        {
            std::wstring text = path.wstring();
            std::wstring result = L"\"";
            for (const wchar_t c : text)
            {
                if (c == L'"') result += L"\\\"";
                else result.push_back(c);
            }
            result.push_back(L'"');
            return result;
        }

        std::wstring QuoteText(const std::wstring& text)
        {
            std::wstring result = L"\"";
            for (const wchar_t c : text)
            {
                if (c == L'"') result += L"\\\"";
                else result.push_back(c);
            }
            result.push_back(L'"');
            return result;
        }

        CSharpBuildResult RunDotnet(const std::wstring& arguments,
            const std::filesystem::path& expected_assembly)
        {
            CSharpBuildResult result;
            result.output_assembly = expected_assembly;

#ifdef _WIN32
            // 以前は _wpopen を使っていたが、_wpopen は cmd.exe を起こすため
            // GUI アプリ（WinMain）から呼ぶと dotnet 実行のたびに
            // コンソール窓が一瞬表示される。Editor 起動時の
            // BuildManagedApi / CompileAndReload と、保存ごとの再コンパイルで
            // 毎回出てしまうので、CreateProcessW + CREATE_NO_WINDOW で
            // 窓を出さずに起動し、stdout / stderr を匿名パイプで受ける。
            //
            // 旧実装の " 2>&1" は hStdOutput と hStdError を
            // 同じパイプに向けることで置き換えている。
            SECURITY_ATTRIBUTES security{};
            security.nLength = sizeof(security);
            security.bInheritHandle = TRUE;
            security.lpSecurityDescriptor = nullptr;

            HANDLE read_pipe = nullptr;
            HANDLE write_pipe = nullptr;
            if (!CreatePipe(&read_pipe, &write_pipe, &security, 0))
            {
                result.output_text = "dotnet pipe could not be created.";
                return result;
            }

            // 読み側は子プロセスへ継承させない。
            SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0);

            STARTUPINFOW startup{};
            startup.cb = sizeof(startup);
            startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
            startup.wShowWindow = SW_HIDE;
            startup.hStdInput = nullptr;
            startup.hStdOutput = write_pipe;
            startup.hStdError = write_pipe;

            // CreateProcessW は第2引数を書き換えるため可変バッファが必要。
            const std::wstring command_line = L"dotnet " + arguments;
            std::vector<wchar_t> mutable_command(
                command_line.begin(), command_line.end());
            mutable_command.push_back(L'\0');

            PROCESS_INFORMATION process{};
            const BOOL started = CreateProcessW(nullptr, mutable_command.data(),
                nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                nullptr, nullptr, &startup, &process);

            // 親側の書き込みハンドルは必ずここで閉じる。
            // 残したままだと子の終了後もパイプが開いたままになり、
            // ReadFile が返らなくなる。
            CloseHandle(write_pipe);

            if (!started)
            {
                CloseHandle(read_pipe);
                result.output_text = "dotnet command failed to start.";
                return result;
            }

            std::array<char, 4096> buffer{};
            DWORD read_bytes = 0;
            while (ReadFile(read_pipe, buffer.data(),
                static_cast<DWORD>(buffer.size()), &read_bytes, nullptr) &&
                read_bytes > 0)
            {
                result.output_text.append(buffer.data(), read_bytes);
            }
            CloseHandle(read_pipe);

            WaitForSingleObject(process.hProcess, INFINITE);
            DWORD process_exit_code = 0;
            if (!GetExitCodeProcess(process.hProcess, &process_exit_code))
            {
                process_exit_code = 1;
            }
            CloseHandle(process.hThread);
            CloseHandle(process.hProcess);

            result.exit_code = static_cast<int>(process_exit_code);
#else
            result.output_text = "dotnet execution is only supported on Windows.";
            return result;
#endif
            result.succeeded = result.exit_code == 0 &&
                std::filesystem::exists(expected_assembly);
            return result;
        }

        CSharpDiagnostic::Severity ParseSeverity(const std::string& text)
        {
            if (text == "error") return CSharpDiagnostic::Severity::Error;
            if (text == "warning") return CSharpDiagnostic::Severity::Warning;
            return CSharpDiagnostic::Severity::Info;
        }

        void ParseDiagnostics(CSharpBuildResult& result)
        {
            const std::regex diagnostic(
                R"(^(.+)\((\d+),(\d+)\):\s+(error|warning)\s+([^:]+):\s+(.*)$)",
                std::regex::icase);
            std::istringstream stream(result.output_text);
            std::string line;
            while (std::getline(stream, line))
            {
                std::smatch match;
                if (!std::regex_match(line, match, diagnostic)) continue;

                CSharpDiagnostic entry;
                entry.file = std::filesystem::u8path(match[1].str());
                entry.line = std::stoi(match[2].str());
                entry.column = std::stoi(match[3].str());
                entry.severity = ParseSeverity(match[4].str());
                entry.code = match[5].str();
                entry.message = match[6].str();
                result.diagnostics.push_back(std::move(entry));
            }
        }
}
