#pragma once

#include "ShaderComposerAsset.h"

#include <filesystem>
#include <string>
#include <vector>

namespace ReplayEngine::Rendering
{
    struct ShaderComposerDiagnostic final
    {
        std::uint64_t node_id = 0;
        std::string message;
    };

    struct ShaderComposerGenerateResult final
    {
        bool succeeded = false;
        std::string hlsl;
        std::vector<ShaderComposerDiagnostic> diagnostics;
    };

    class ShaderComposerGenerator final
    {
    public:
        static ShaderComposerGenerateResult Generate(const ShaderComposerAsset& asset);

        // generated_hlsl is resolved against project_root when it is relative.
        static bool GenerateToFile(const ShaderComposerAsset& asset,
            const std::filesystem::path& project_root, std::string& error);
    };
}
