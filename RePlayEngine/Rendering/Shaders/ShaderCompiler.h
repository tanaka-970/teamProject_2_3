#pragma once

#include "ShaderDiagnostic.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace ReplayEngine::Rendering
{
    using ShaderBytecode = std::shared_ptr<std::vector<std::uint8_t>>;

    class ShaderCompiler final
    {
    public:
        struct Options final
        {
            bool debug_info = false;
            bool optimize = true;
            bool warnings_as_errors = false;
            std::vector<std::filesystem::path> include_directories;
            std::vector<std::pair<std::string, std::string>> defines;
        };

        static Options DefaultOptions(bool debug_build);
        static ShaderCompileResult CompileFile(
            const std::filesystem::path& source,
            const char* entry_point,
            const char* target,
            const Options& options,
            ShaderBytecode& out_bytecode);
        static ShaderCompileResult CompileSource(
            const std::string& source_text,
            const std::filesystem::path& source_name,
            const char* entry_point,
            const char* target,
            const Options& options,
            ShaderBytecode& out_bytecode);
        static std::vector<ShaderDiagnostic> ParseDiagnostics(
            const std::string& raw_output,
            const std::filesystem::path& fallback_file);
    };
}
