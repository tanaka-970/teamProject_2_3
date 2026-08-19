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
#include <string_view>
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

        const std::filesystem::path solution_path = GameScriptsSolutionPath(root);
        const std::wstring solution = Quote(solution_path);
        const std::wstring project = Quote(GameScriptsProjectPath(root));
        const std::wstring managed = Quote(ManagedApiProjectPath(root));

        // 既存の .sln は再生成しない。
        //
        // 以前は毎回 `dotnet new sln --force` を実行していたが、これには
        // 2つの実害があった。
        //   1. Visual Studio が .sln を開いている最中に外部から上書きされ、
        //      再読込ダイアログが出る。
        //   2. 再生成後に RePlayGameScripts しか add し直さないため、
        //      RePlayEngine.Managed が .sln から消える。
        // Behaviour を1つ作るたびにこれが起きるので、無い時だけ作る。
        //
        // `--format sln` は .slnx ではなく従来形式の .sln を作るために必須。
        std::error_code solution_error;
        if (!std::filesystem::exists(solution_path, solution_error))
        {
            const CSharpBuildResult created = RunDotnet(
                L"new sln --format sln -n RePlayScripts -o " + Quote(scripts), {});
            // RunDotnet の succeeded は output_assembly の存在も見るため、
            // Assembly を伴わない sln 操作では exit_code で判定する。
            if (created.exit_code != 0)
            {
                error = "dotnet new sln failed: " + created.output_text;
                return false;
            }

            const CSharpBuildResult added_managed =
                RunDotnet(L"sln " + solution + L" add " + managed, {});
            if (added_managed.exit_code != 0)
            {
                error = "dotnet sln add (Managed API) failed: " +
                    added_managed.output_text;
                return false;
            }

            const CSharpBuildResult added_scripts =
                RunDotnet(L"sln " + solution + L" add " + project, {});
            if (added_scripts.exit_code != 0)
            {
                error = "dotnet sln add (Game Scripts) failed: " +
                    added_scripts.output_text;
                return false;
            }
            return true;
        }

        // 既存 .sln から Managed API が抜けている場合だけ補う。
        // 旧実装が一度でも走った .sln を自己修復するための経路で、
        // 失敗しても Behaviour 生成自体は妨げない。
        if (ReadAllText(solution_path).find("RePlayEngine.Managed") == std::string::npos)
        {
            RunDotnet(L"sln " + solution + L" add " + managed, {});
        }
        return true;
    }

    bool CSharpProject::CreateBehaviour(const std::filesystem::path& project_root,
        const std::string& class_name, const std::string& namespace_name,
        CSharpBehaviourInfo& out, std::string& error,
        const std::filesystem::path& subfolder)
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

        // subfolder が Scripts/ の外へ出る指定は無視して Scripts/ 直下に作る。
        std::filesystem::path folder = ScriptsRoot(root);
        if (!subfolder.empty() && !subfolder.is_absolute() &&
            subfolder.generic_u8string().find("..") == std::string::npos)
        {
            folder /= subfolder;
            std::error_code folder_error;
            std::filesystem::create_directories(folder, folder_error);
            if (folder_error) folder = ScriptsRoot(root);
        }

        std::filesystem::path path = folder / (class_name + ".cs");
        for (int suffix = 2; std::filesystem::exists(path) && suffix < 10000; ++suffix)
        {
            path = folder / (class_name + std::to_string(suffix) + ".cs");
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
}
