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
        bool RestoreRequired(const CSharpBuildResult& result)
        {
            return result.output_text.find("NETSDK1004") != std::string::npos ||
                result.output_text.find("NU1100") != std::string::npos ||
                result.output_text.find("project.assets.json") != std::string::npos;
        }

        CSharpBuildResult BuildWithExistingRestore(const std::filesystem::path& project,
            const std::string& configuration, const std::filesystem::path& assembly)
        {
            const std::wstring base = L"build " + Quote(project) + L" -c " +
                QuoteText(ToWide(configuration)) + L" --nologo";
            CSharpBuildResult result = RunDotnet(base + L" --no-restore", assembly);
            if (!result.succeeded && RestoreRequired(result))
                result = RunDotnet(base, assembly);
            return result;
        }
    }

    CSharpBuildResult CSharpProject::BuildManagedApi(
        const std::filesystem::path& project_root, const std::string& configuration)
    {
        const std::filesystem::path root = NormalizeRoot(project_root);
        CSharpBuildResult result = BuildWithExistingRestore(
            ManagedApiProjectPath(root), configuration,
            ManagedApiAssemblyPath(root, configuration));
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

        if (ManagedApiBuildRequired(root, configuration))
        {
            CSharpBuildResult api = BuildManagedApi(root, configuration);
            if (!api.succeeded) return api;
        }

        CSharpBuildResult result = BuildWithExistingRestore(
            GameScriptsProjectPath(root), configuration,
            GameScriptsAssemblyPath(root, configuration));
        ParseDiagnostics(result);
        return result;
    }

    bool CSharpProject::ManagedApiBuildRequired(
        const std::filesystem::path& project_root, const std::string& configuration)
    {
        const std::filesystem::path root = NormalizeRoot(project_root);
        std::error_code error;
        if (!std::filesystem::exists(
            ManagedApiRuntimeConfigPath(root, configuration), error) || error)
            return true;
        return SourceTreeIsNewer(ManagedApiProjectPath(root).parent_path(),
            ManagedApiAssemblyPath(root, configuration));
    }

    CSharpBuildState CSharpProject::QueryGameScriptsBuildState(
        const std::filesystem::path& project_root, const std::string& configuration)
    {
        const std::filesystem::path root = NormalizeRoot(project_root);

        // GameScripts だけでなく Managed API の入力世代も同じ状態値へ含める。
        // これが無いと Managed API 側のソースだけ修正した場合、直前の失敗 revision と
        // 同一に見えて Play 前ビルドを永遠に再試行しない可能性がある。
        const CSharpBuildState managed = QuerySourceTreeBuildState(
            ManagedApiProjectPath(root).parent_path(),
            ManagedApiAssemblyPath(root, configuration));
        const CSharpBuildState scripts = QuerySourceTreeBuildState(
            ScriptsRoot(root), GameScriptsAssemblyPath(root, configuration),
            { ManagedApiAssemblyPath(root, configuration) });

        std::error_code error;
        const bool runtime_config_missing = !std::filesystem::exists(
            ManagedApiRuntimeConfigPath(root, configuration), error) || error;

        CSharpBuildState combined;
        combined.build_required = managed.build_required || scripts.build_required ||
            runtime_config_missing;

        // boost::hash_combine と同じ考え方で 2 系統の世代を 1 値へ畳む。
        // 0 は framework 側の「未記録」なので最後に避ける。
        std::uint64_t revision = managed.input_revision;
        revision ^= scripts.input_revision + 0x9e3779b97f4a7c15ull +
            (revision << 6) + (revision >> 2);
        if (runtime_config_missing)
            revision ^= 0xd6e8feb86659fd93ull;
        combined.input_revision = revision != 0 ? revision : 1;
        return combined;
    }

    bool CSharpProject::GameScriptsBuildRequired(
        const std::filesystem::path& project_root, const std::string& configuration)
    {
        return QueryGameScriptsBuildState(project_root, configuration).build_required;
    }
}
