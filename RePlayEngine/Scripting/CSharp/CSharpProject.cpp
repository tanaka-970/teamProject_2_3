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

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

namespace ReplayEngine::Scripting::CSharp
{
    namespace
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

        bool IsIdentifier(std::string_view text) noexcept
        {
            if (text.empty()) return false;
            const auto valid_start = [](char c) noexcept
            {
                return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
            };
            const auto valid_body = [valid_start](char c) noexcept
            {
                return valid_start(c) || (c >= '0' && c <= '9');
            };
            if (!valid_start(text.front())) return false;
            for (const char c : text)
            {
                if (!valid_body(c)) return false;
            }
            return true;
        }

        bool IsNamespace(std::string_view text) noexcept
        {
            if (text.empty()) return false;
            std::size_t start = 0;
            while (start <= text.size())
            {
                const std::size_t dot = text.find('.', start);
                const std::string_view part = dot == std::string_view::npos
                    ? text.substr(start)
                    : text.substr(start, dot - start);
                if (!IsIdentifier(part)) return false;
                if (dot == std::string_view::npos) return true;
                start = dot + 1;
            }
            return false;
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

        CSharpBuildResult RunDotnet(const std::wstring& arguments,
            const std::filesystem::path& expected_assembly)
        {
            CSharpBuildResult result;
            result.output_assembly = expected_assembly;

            const std::wstring command = L"dotnet " + arguments + L" 2>&1";
            FILE* pipe = _wpopen(command.c_str(), L"r");
            if (pipe == nullptr)
            {
                result.output_text = "dotnet command failed to start.";
                return result;
            }

            std::array<char, 4096> buffer{};
            while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr)
            {
                result.output_text += buffer.data();
            }

            const int exit_code = _pclose(pipe);
            result.exit_code = exit_code;
            result.succeeded = exit_code == 0 &&
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

        std::string ProjectReferencePath(const std::filesystem::path& from_project,
            const std::filesystem::path& target_project)
        {
            std::error_code error;
            const std::filesystem::path relative = std::filesystem::relative(
                target_project, from_project.parent_path(), error);
            return (error ? target_project : relative).generic_u8string();
        }

        std::string GameScriptsProjectText(const std::filesystem::path& project_root)
        {
            const std::filesystem::path project_path =
                CSharpProject::GameScriptsProjectPath(project_root);
            const std::filesystem::path managed_project =
                CSharpProject::ManagedApiProjectPath(project_root);

            std::ostringstream stream;
            stream <<
                "<Project Sdk=\"Microsoft.NET.Sdk\">\n"
                "  <PropertyGroup>\n"
                "    <TargetFramework>net8.0</TargetFramework>\n"
                "    <Nullable>enable</Nullable>\n"
                "    <ImplicitUsings>enable</ImplicitUsings>\n"
                "    <LangVersion>latest</LangVersion>\n"
                "    <OutputType>Library</OutputType>\n"
                "    <AssemblyName>RePlayGameScripts</AssemblyName>\n"
                "    <RootNamespace>Game</RootNamespace>\n"
                "  </PropertyGroup>\n"
                "  <ItemGroup>\n"
                "    <ProjectReference Include=\"" <<
                ProjectReferencePath(project_path, managed_project) << "\" />\n"
                "  </ItemGroup>\n"
                "</Project>\n";
            return stream.str();
        }

        std::string BehaviourSourceText(const std::string& guid,
            const std::string& namespace_name, const std::string& class_name)
        {
            std::ostringstream stream;
            stream <<
                "using ReplayEngine;\n"
                "\n"
                "namespace " << namespace_name << ";\n"
                "\n"
                "[ReplayGuid(\"" << guid << "\")]\n"
                "public sealed class " << class_name << " : ScriptBehaviour\n"
                "{\n"
                "    public float Speed = 1.0f;\n"
                "    public ObjectReference Target;\n"
                "\n"
                "    public override void Awake()\n"
                "    {\n"
                "    }\n"
                "\n"
                "    public override void Update(float deltaTime)\n"
                "    {\n"
                "    }\n"
                "}\n";
            return stream.str();
        }

        bool ExtractReplayGuid(const std::string& text, std::string& out)
        {
            const std::regex expression(
                R"rgx(\[\s*(?:ReplayEngine\.)?ReplayGuid\s*\(\s*"([0-9a-fA-F\-\{\}]{32,38})"\s*\)\s*\])rgx");
            std::smatch match;
            if (!std::regex_search(text, match, expression)) return false;
            Reflection::TypeGUID parsed;
            if (!Reflection::TypeGUID::TryParse(match[1].str(), parsed)) return false;
            out = parsed.ToString();
            return true;
        }

        bool ExtractNamespace(const std::string& text, std::string& out)
        {
            const std::regex expression(R"(\bnamespace\s+([A-Za-z_][A-Za-z0-9_\.]*)\s*(?:;|\{))");
            std::smatch match;
            if (!std::regex_search(text, match, expression)) return false;
            out = match[1].str();
            return true;
        }

        bool ExtractClassName(const std::string& text, std::string& out)
        {
            const std::regex expression(
                R"(\[\s*(?:ReplayEngine\.)?ReplayGuid\s*\([^\)]*\)\s*\][\s\S]*?\bclass\s+([A-Za-z_][A-Za-z0-9_]*))");
            std::smatch match;
            if (!std::regex_search(text, match, expression)) return false;
            out = match[1].str();
            return true;
        }

        bool IsUnderIgnoredFolder(const std::filesystem::path& path)
        {
            for (const auto& part : path)
            {
                const std::string text = part.generic_u8string();
                if (text == "bin" || text == "obj") return true;
            }
            return false;
        }

        std::string Hex64(std::uint64_t value)
        {
            std::ostringstream stream;
            stream << std::hex << std::setfill('0') << std::setw(16) << value;
            return stream.str();
        }
    }

    std::filesystem::path CSharpProject::ManagedApiProjectPath(
        const std::filesystem::path& project_root)
    {
        return NormalizeRoot(project_root) / "Managed" / "RePlayEngine.Managed" /
            "RePlayEngine.Managed.csproj";
    }

    std::filesystem::path CSharpProject::ManagedApiAssemblyPath(
        const std::filesystem::path& project_root, const std::string& configuration)
    {
        return NormalizeRoot(project_root) / "Managed" / "RePlayEngine.Managed" /
            "bin" / configuration / "net8.0" / "RePlayEngine.Managed.dll";
    }

    std::filesystem::path CSharpProject::ManagedApiRuntimeConfigPath(
        const std::filesystem::path& project_root, const std::string& configuration)
    {
        return NormalizeRoot(project_root) / "Managed" / "RePlayEngine.Managed" /
            "bin" / configuration / "net8.0" / "RePlayEngine.Managed.runtimeconfig.json";
    }

    std::filesystem::path CSharpProject::ScriptsRoot(
        const std::filesystem::path& project_root)
    {
        return NormalizeRoot(project_root) / "Scripts";
    }

    std::filesystem::path CSharpProject::GameScriptsProjectPath(
        const std::filesystem::path& project_root)
    {
        return ScriptsRoot(project_root) / "RePlayGameScripts.csproj";
    }

    std::filesystem::path CSharpProject::GameScriptsSolutionPath(
        const std::filesystem::path& project_root)
    {
        return ScriptsRoot(project_root) / "RePlayScripts.sln";
    }

    std::filesystem::path CSharpProject::GameScriptsAssemblyPath(
        const std::filesystem::path& project_root, const std::string& configuration)
    {
        return ScriptsRoot(project_root) / "bin" / configuration / "net8.0" /
            "RePlayGameScripts.dll";
    }

    bool CSharpProject::EnsureProjectFiles(const std::filesystem::path& project_root,
        std::string& error)
    {
        const std::filesystem::path root = NormalizeRoot(project_root);
        const std::filesystem::path scripts = ScriptsRoot(root);
        std::error_code filesystem_error;
        std::filesystem::create_directories(scripts, filesystem_error);
        if (filesystem_error)
        {
            error = "Scripts folder create failed: " + scripts.generic_u8string();
            return false;
        }

        if (!std::filesystem::exists(ManagedApiProjectPath(root), filesystem_error))
        {
            error = "Managed API project is missing: " +
                ManagedApiProjectPath(root).generic_u8string();
            return false;
        }

        if (!WriteTextIfChanged(GameScriptsProjectPath(root),
            GameScriptsProjectText(root), error))
        {
            return false;
        }

        const std::wstring solution = Quote(GameScriptsSolutionPath(root));
        const std::wstring project = Quote(GameScriptsProjectPath(root));
        RunDotnet(L"new sln --force --format sln -n RePlayScripts -o " +
            Quote(scripts), {});
        RunDotnet(L"sln " + solution + L" add " + project, {});
        return true;
    }

    bool CSharpProject::CreateBehaviour(const std::filesystem::path& project_root,
        const std::string& class_name, const std::string& namespace_name,
        CSharpBehaviourInfo& out, std::string& error)
    {
        const std::filesystem::path root = NormalizeRoot(project_root);
        if (!IsIdentifier(class_name))
        {
            error = "invalid C# class name: " + class_name;
            return false;
        }
        if (!IsNamespace(namespace_name))
        {
            error = "invalid C# namespace: " + namespace_name;
            return false;
        }
        if (!EnsureProjectFiles(root, error)) return false;

        std::filesystem::path path = ScriptsRoot(root) / (class_name + ".cs");
        for (int suffix = 2; std::filesystem::exists(path) && suffix < 10000; ++suffix)
        {
            path = ScriptsRoot(root) / (class_name + std::to_string(suffix) + ".cs");
        }

        const std::string guid = GenerateTypeGuid();
        if (!WriteTextIfChanged(path, BehaviourSourceText(guid, namespace_name, class_name),
            error))
        {
            return false;
        }

        out.source_path = path;
        out.type_guid = guid;
        out.class_name = class_name;
        out.full_class_name = namespace_name + "." + class_name;
        return true;
    }

    std::vector<CSharpBehaviourInfo> CSharpProject::DiscoverBehaviours(
        const std::filesystem::path& project_root)
    {
        const std::filesystem::path scripts = ScriptsRoot(project_root);
        std::vector<CSharpBehaviourInfo> result;
        std::error_code error;
        if (!std::filesystem::exists(scripts, error) || error) return result;

        for (std::filesystem::recursive_directory_iterator it(scripts, error), end;
            !error && it != end; it.increment(error))
        {
            if (it->is_directory())
            {
                if (IsUnderIgnoredFolder(it->path())) it.disable_recursion_pending();
                continue;
            }
            if (!it->is_regular_file() || it->path().extension() != ".cs") continue;

            CSharpBehaviourInfo info;
            if (TryReadBehaviourInfo(it->path(), info))
            {
                result.push_back(std::move(info));
            }
        }

        std::sort(result.begin(), result.end(),
            [](const CSharpBehaviourInfo& lhs, const CSharpBehaviourInfo& rhs)
            {
                return lhs.full_class_name < rhs.full_class_name;
            });
        return result;
    }

    bool CSharpProject::RefreshCatalog(const std::filesystem::path& project_root,
        Assets::AssetDatabase& database, ScriptTypeCatalog& catalog, std::string& error)
    {
        if (!EnsureProjectFiles(project_root, error)) return false;

        for (CSharpBehaviourInfo& info : DiscoverBehaviours(project_root))
        {
            const Assets::AssetRecord& record =
                database.Register(info.source_path, Assets::AssetKind::Script);
            info.asset_guid = record.guid;

            Reflection::TypeGUID type_guid;
            if (!Reflection::TypeGUID::TryParse(info.type_guid, type_guid)) continue;

            ScriptTypeDescriptor descriptor;
            descriptor.type_id = type_guid;
            descriptor.language = ScriptLanguage::CSharp;
            descriptor.script_name = info.class_name;
            descriptor.display_name = HumanizeFieldName(info.class_name);
            descriptor.asset_guid = info.asset_guid;
            descriptor.class_name = info.full_class_name;
            descriptor.category = ScriptCategoryName(ScriptLanguage::CSharp);
            catalog.Register(std::move(descriptor));
        }

        if (!database.Save(error)) return false;
        return true;
    }

    CSharpBuildResult CSharpProject::BuildManagedApi(
        const std::filesystem::path& project_root, const std::string& configuration)
    {
        const std::filesystem::path root = NormalizeRoot(project_root);
        CSharpBuildResult result = RunDotnet(L"build " +
            Quote(ManagedApiProjectPath(root)) + L" -c " + QuoteText(ToWide(configuration)) +
            L" --nologo", ManagedApiAssemblyPath(root, configuration));
        ParseDiagnostics(result);
        return result;
    }

    CSharpBuildResult CSharpProject::BuildGameScripts(
        const std::filesystem::path& project_root, const std::string& configuration)
    {
        const std::filesystem::path root = NormalizeRoot(project_root);
        std::string error;
        if (!EnsureProjectFiles(root, error))
        {
            CSharpBuildResult result;
            result.output_text = error;
            return result;
        }

        CSharpBuildResult api = BuildManagedApi(root, configuration);
        if (!api.succeeded) return api;

        CSharpBuildResult result = RunDotnet(L"build " +
            Quote(GameScriptsProjectPath(root)) + L" -c " +
            QuoteText(ToWide(configuration)) + L" --nologo",
            GameScriptsAssemblyPath(root, configuration));
        ParseDiagnostics(result);
        return result;
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

        const HINSTANCE result = ShellExecuteW(nullptr, L"open",
            executable.wstring().c_str(),
            arguments.c_str(), nullptr, SW_SHOWNORMAL);
        if (reinterpret_cast<intptr_t>(result) > 32) return true;

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

    std::string CSharpProject::GenerateTypeGuid()
    {
        std::random_device device;
        const auto now = static_cast<std::uint64_t>(
            std::chrono::high_resolution_clock::now().time_since_epoch().count());
        std::uint64_t high =
            (static_cast<std::uint64_t>(device()) << 32) ^ device() ^ now;
        std::uint64_t low =
            (static_cast<std::uint64_t>(device()) << 32) ^ device() ^ (now << 1);
        if (high == 0 && low == 0) low = 1;
        return Hex64(high) + Hex64(low);
    }

    bool CSharpProject::TryReadBehaviourInfo(const std::filesystem::path& source_path,
        CSharpBehaviourInfo& out)
    {
        const std::string text = ReadAllText(source_path);
        if (text.empty()) return false;

        std::string guid;
        std::string class_name;
        if (!ExtractReplayGuid(text, guid)) return false;
        if (!ExtractClassName(text, class_name)) return false;

        std::string namespace_name;
        ExtractNamespace(text, namespace_name);

        out.source_path = source_path;
        out.type_guid = guid;
        out.class_name = class_name;
        out.full_class_name = namespace_name.empty()
            ? class_name
            : namespace_name + "." + class_name;
        return true;
    }
}
