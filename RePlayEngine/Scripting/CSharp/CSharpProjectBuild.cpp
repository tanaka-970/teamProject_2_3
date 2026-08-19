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

        if (ManagedApiBuildRequired(root, configuration))
        {
            CSharpBuildResult api = BuildManagedApi(root, configuration);
            if (!api.succeeded) return api;
        }

        CSharpBuildResult result = RunDotnet(L"build " +
            Quote(GameScriptsProjectPath(root)) + L" -c " +
            QuoteText(ToWide(configuration)) + L" --nologo",
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

    bool CSharpProject::GameScriptsBuildRequired(
        const std::filesystem::path& project_root, const std::string& configuration)
    {
        const std::filesystem::path root = NormalizeRoot(project_root);
        return SourceTreeIsNewer(ScriptsRoot(root),
            GameScriptsAssemblyPath(root, configuration),
            { ManagedApiAssemblyPath(root, configuration) });
    }
}
