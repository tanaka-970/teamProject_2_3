#pragma once

#include "../Core/ScriptTypeCatalog.h"
#include "../../Assets/AssetDatabase.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace ReplayEngine::Scripting::CSharp
{
    struct CSharpBehaviourInfo final
    {
        std::filesystem::path source_path;
        std::string asset_guid;
        std::string type_guid;
        std::string class_name;
        std::string full_class_name;
    };

    struct CSharpDiagnostic final
    {
        enum class Severity
        {
            Info,
            Warning,
            Error,
        };

        Severity severity = Severity::Info;
        std::filesystem::path file;
        int line = 0;
        int column = 0;
        std::string code;
        std::string message;
    };

    struct CSharpBuildResult final
    {
        bool succeeded = false;
        int exit_code = -1;
        std::filesystem::path output_assembly;
        std::string output_text;
        std::vector<CSharpDiagnostic> diagnostics;
    };

    // Play 直前の freshness 判定と入力世代を 1 回の走査で取得する。
    // input_revision は同じソース状態なら同じ値になり、ソース / project /
    // Managed API dependency のどれかが変われば変化する。
    struct CSharpBuildState final
    {
        bool build_required = true;
        std::uint64_t input_revision = 0;
    };

    class CSharpProject final
    {
    public:
        static std::filesystem::path ManagedApiProjectPath(
            const std::filesystem::path& project_root);
        static std::filesystem::path ManagedApiAssemblyPath(
            const std::filesystem::path& project_root,
            const std::string& configuration = "Debug");
        static std::filesystem::path ManagedApiRuntimeConfigPath(
            const std::filesystem::path& project_root,
            const std::string& configuration = "Debug");

        static std::filesystem::path ScriptsRoot(
            const std::filesystem::path& project_root);
        static std::filesystem::path GameScriptsProjectPath(
            const std::filesystem::path& project_root);
        static std::filesystem::path GameScriptsSolutionPath(
            const std::filesystem::path& project_root);
        static std::filesystem::path GameScriptsAssemblyPath(
            const std::filesystem::path& project_root,
            const std::string& configuration = "Debug");

        static bool EnsureProjectFiles(const std::filesystem::path& project_root,
            std::string& error);

        // subfolder は Scripts/ からの相対フォルダ。空ならば Scripts/ 直下。
        // csproj は **/*.cs を暗黙に含むので、サブフォルダでも
        // そのままコンパイル対象になる。
        static bool CreateBehaviour(const std::filesystem::path& project_root,
            const std::string& class_name, const std::string& namespace_name,
            CSharpBehaviourInfo& out, std::string& error,
            const std::filesystem::path& subfolder = {});

        static std::vector<CSharpBehaviourInfo> DiscoverBehaviours(
            const std::filesystem::path& project_root);

        static bool RefreshCatalog(const std::filesystem::path& project_root,
            Assets::AssetDatabase& database, ScriptTypeCatalog& catalog,
            std::string& error);

        static CSharpBuildResult BuildManagedApi(
            const std::filesystem::path& project_root,
            const std::string& configuration = "Debug");
        static CSharpBuildResult BuildGameScripts(
            const std::filesystem::path& project_root,
            const std::string& configuration = "Debug");
        static bool ManagedApiBuildRequired(
            const std::filesystem::path& project_root,
            const std::string& configuration = "Debug");
        static bool GameScriptsBuildRequired(
            const std::filesystem::path& project_root,
            const std::string& configuration = "Debug");
        static CSharpBuildState QueryGameScriptsBuildState(
            const std::filesystem::path& project_root,
            const std::string& configuration = "Debug");

        static bool OpenVisualStudio(const std::filesystem::path& file,
            int line, std::string& error);

        static std::string GenerateTypeGuid();
        static bool TryReadBehaviourInfo(const std::filesystem::path& source_path,
            CSharpBehaviourInfo& out);
    };
}
