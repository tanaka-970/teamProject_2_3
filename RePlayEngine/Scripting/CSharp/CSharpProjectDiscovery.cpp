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
#include <iterator>
#include <random>
#include <regex>
#include <sstream>
#include <system_error>
#include <unordered_map>
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
        bool ExtractNamespace(const std::string& text, std::string& out)
        {
            const std::regex expression(R"(\bnamespace\s+([A-Za-z_][A-Za-z0-9_\.]*)\s*(?:;|\{))");
            std::smatch match;
            if (!std::regex_search(text, match, expression)) return false;
            out = match[1].str();
            return true;
        }

        std::vector<CSharpBehaviourInfo> ExtractBehaviourInfos(
            const std::filesystem::path& source_path, const std::string& text)
        {
            std::vector<CSharpBehaviourInfo> result;
            const std::regex expression(
                R"rgx(\[\s*(?:ReplayEngine\.)?ReplayGuid\s*\(\s*"([0-9a-fA-F\-\{\}]{32,38})"\s*\)\s*\](?:\s*\[[^\]]*\])*(?:\s+(?:public|internal|private|protected|abstract|sealed|partial|static|new))*\s*class\s+([A-Za-z_][A-Za-z0-9_]*))rgx");

            std::string namespace_name;
            ExtractNamespace(text, namespace_name);
            for (std::sregex_iterator it(text.begin(), text.end(), expression), end;
                it != end; ++it)
            {
                Reflection::TypeGUID parsed;
                if (!Reflection::TypeGUID::TryParse((*it)[1].str(), parsed)) continue;

                CSharpBehaviourInfo info;
                info.source_path = source_path;
                info.type_guid = parsed.ToString();
                info.class_name = (*it)[2].str();
                info.full_class_name = namespace_name.empty()
                    ? info.class_name
                    : namespace_name + "." + info.class_name;
                result.push_back(std::move(info));
            }
            return result;
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

            const std::string text = ReadAllText(it->path());
            std::vector<CSharpBehaviourInfo> file_entries =
                ExtractBehaviourInfos(it->path(), text);
            result.insert(result.end(),
                std::make_move_iterator(file_entries.begin()),
                std::make_move_iterator(file_entries.end()));
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

        std::vector<CSharpBehaviourInfo> behaviours = DiscoverBehaviours(project_root);
        std::unordered_map<std::string, std::filesystem::path> guid_sources;
        std::unordered_map<std::string, std::filesystem::path> class_sources;
        for (const CSharpBehaviourInfo& info : behaviours)
        {
            const auto guid_inserted = guid_sources.emplace(info.type_guid, info.source_path);
            if (!guid_inserted.second)
            {
                error = "duplicate C# ReplayGuid " + info.type_guid + ": " +
                    guid_inserted.first->second.u8string() + " and " +
                    info.source_path.u8string();
                return false;
            }
            const auto class_inserted = class_sources.emplace(
                info.full_class_name, info.source_path);
            if (!class_inserted.second)
            {
                error = "duplicate C# Behaviour class " + info.full_class_name + ": " +
                    class_inserted.first->second.u8string() + " and " +
                    info.source_path.u8string();
                return false;
            }

            Reflection::TypeGUID type_guid;
            if (!Reflection::TypeGUID::TryParse(info.type_guid, type_guid)) continue;
            const ScriptTypeDescriptor* existing = catalog.Find(type_guid);
            if (existing != nullptr && existing->language != ScriptLanguage::CSharp)
            {
                error = "C# ReplayGuid conflicts with another script language: " +
                    info.type_guid;
                return false;
            }
        }

        std::vector<ScriptTypeDescriptor> descriptors;
        descriptors.reserve(behaviours.size());
        for (CSharpBehaviourInfo& info : behaviours)
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
            if (const ScriptTypeDescriptor* existing = catalog.Find(type_guid))
            {
                descriptor.status = existing->status;
                descriptor.last_error = existing->last_error;
                descriptor.schema = existing->schema;
            }
            descriptors.push_back(std::move(descriptor));
        }

        if (!database.Save(error)) return false;
        catalog.RemoveLanguage(ScriptLanguage::CSharp);
        for (ScriptTypeDescriptor& descriptor : descriptors)
            catalog.Register(std::move(descriptor));
        return true;
    }

    bool CSharpProject::TryReadBehaviourInfo(const std::filesystem::path& source_path,
        CSharpBehaviourInfo& out)
    {
        const std::string text = ReadAllText(source_path);
        if (text.empty()) return false;

        std::vector<CSharpBehaviourInfo> entries = ExtractBehaviourInfos(source_path, text);
        if (entries.empty()) return false;
        out = std::move(entries.front());
        return true;
    }
}
