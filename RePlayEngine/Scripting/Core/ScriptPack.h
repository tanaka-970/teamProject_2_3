#pragma once

#include "ScriptTypeCatalog.h"
#include <filesystem>
#include <string>
#include <vector>

namespace ReplayEngine::Scripting::ScriptPack
{
    bool Save(const std::filesystem::path& root, const ScriptTypeCatalog& catalog,
        const std::string& configuration, const std::filesystem::path& output,
        std::vector<std::string>& errors);
    bool Load(const std::filesystem::path& root, ScriptTypeCatalog& catalog,
        std::string& configuration, std::string& error);
    bool ValidateReferences(const std::filesystem::path& root,
        const ScriptTypeCatalog& catalog, std::vector<std::string>& errors);
}
