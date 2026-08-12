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
