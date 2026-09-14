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

        namespace
        {
            std::uint64_t HashBytes(std::uint64_t seed, const void* data,
                std::size_t size) noexcept
            {
                const auto* bytes = static_cast<const unsigned char*>(data);
                for (std::size_t i = 0; i < size; ++i)
                {
                    seed ^= static_cast<std::uint64_t>(bytes[i]);
                    seed *= 1099511628211ull;
                }
                return seed;
            }

            std::uint64_t HashText(std::uint64_t seed, const std::string& text) noexcept
            {
                return HashBytes(seed, text.data(), text.size());
            }

            void HashFileMetadata(std::uint64_t& seed, const std::filesystem::path& path,
                bool& had_error)
            {
                std::error_code error;
                const auto time = std::filesystem::last_write_time(path, error);
                if (error)
                {
                    had_error = true;
                    seed = HashText(seed, path.generic_u8string());
                    const std::uint64_t marker = 0xffffffffffffffffull;
                    seed = HashBytes(seed, &marker, sizeof(marker));
                    return;
                }

                const auto time_count = time.time_since_epoch().count();
                std::uintmax_t size = 0;
                if (std::filesystem::is_regular_file(path, error) && !error)
                    size = std::filesystem::file_size(path, error);
                if (error)
                {
                    had_error = true;
                    error.clear();
                }

                seed = HashText(seed, path.generic_u8string());
                seed = HashBytes(seed, &time_count, sizeof(time_count));
                seed = HashBytes(seed, &size, sizeof(size));
            }
        }

        CSharpBuildState QuerySourceTreeBuildState(
            const std::filesystem::path& source_root,
            const std::filesystem::path& output,
            const std::vector<std::filesystem::path>& dependencies)
        {
            CSharpBuildState state;
            state.build_required = false;
            std::uint64_t revision = 1469598103934665603ull;
            bool had_error = false;

            std::error_code error;
            const bool output_exists = std::filesystem::exists(output, error) && !error;
            std::filesystem::file_time_type output_time{};
            if (output_exists)
            {
                output_time = std::filesystem::last_write_time(output, error);
                if (error)
                {
                    state.build_required = true;
                    had_error = true;
                    error.clear();
                }
            }
            else
            {
                state.build_required = true;
                if (error) { had_error = true; error.clear(); }
            }

            std::vector<std::filesystem::path> inputs;
            inputs.reserve(64);
            for (const std::filesystem::path& dependency : dependencies)
                inputs.push_back(dependency.lexically_normal());

            std::filesystem::recursive_directory_iterator it(
                source_root, std::filesystem::directory_options::skip_permission_denied, error);
            const std::filesystem::recursive_directory_iterator end;
            for (; !error && it != end; it.increment(error))
            {
                if (it->is_directory(error))
                {
                    const std::string name = it->path().filename().generic_u8string();
                    if (name == "bin" || name == "obj" || name == ".vs")
                        it.disable_recursion_pending();
                    continue;
                }
                if (error || !it->is_regular_file(error)) continue;

                const std::string extension = it->path().extension().generic_u8string();
                if (extension != ".cs" && extension != ".csproj" &&
                    extension != ".props" && extension != ".targets")
                    continue;
                inputs.push_back(it->path().lexically_normal());
            }
            if (error)
            {
                state.build_required = true;
                had_error = true;
                error.clear();
            }

            std::sort(inputs.begin(), inputs.end(),
                [](const std::filesystem::path& left, const std::filesystem::path& right)
                {
                    return left.generic_u8string() < right.generic_u8string();
                });
            inputs.erase(std::unique(inputs.begin(), inputs.end()), inputs.end());

            for (const std::filesystem::path& input : inputs)
            {
                std::error_code input_error;
                const auto input_time = std::filesystem::last_write_time(input, input_error);
                if (input_error)
                {
                    state.build_required = true;
                    had_error = true;
                }
                else if (!output_exists || input_time > output_time)
                {
                    state.build_required = true;
                }
                HashFileMetadata(revision, input, had_error);
            }

            // 0 は framework 側で「未記録」に使うので避ける。
            if (revision == 0) revision = 1;
            if (had_error) revision ^= 0x9e3779b97f4a7c15ull;
            state.input_revision = revision;
            return state;
        }

        bool SourceTreeIsNewer(const std::filesystem::path& source_root,
            const std::filesystem::path& output,
            const std::vector<std::filesystem::path>& dependencies)
        {
            return QuerySourceTreeBuildState(source_root, output, dependencies).build_required;
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
