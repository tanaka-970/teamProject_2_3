// CSharpProject の責務を、パス・生成・探索・ビルド・Visual Studio 連携へ分ける。
//
//   CSharpProject.cpp                  … 共通パスとGUID生成（このファイル）
//   CSharpProjectInternal.cpp          … 分割内部で共有するファイル / dotnet ヘルパ
//   CSharpProjectInternal.h            … 分割内部ヘルパの宣言
//   CSharpProjectGeneration.cpp        … プロジェクトファイルとBehaviour生成
//   CSharpProjectDiscovery.cpp         … Behaviour探索とカタログ更新
//   CSharpProjectBuild.cpp             … Managed API / Game Scripts のビルド判定と実行
//   CSharpProjectVisualStudio.cpp      … Visual Studio起動とファイル移動

#include "CSharpProject.h"
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

namespace ReplayEngine::Scripting::CSharp
{
    using namespace Detail;

    namespace
    {
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
}
