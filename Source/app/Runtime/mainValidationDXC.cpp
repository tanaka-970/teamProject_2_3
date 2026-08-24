#include "framework.h"
#include "mainInternal.h"

#include "../../../RePlayEngine/Rendering/DX12/D3D12ShaderCompiler.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace ReplayEngine::Runtime::Detail
{
    int RunHeadlessDXCValidation(const char* command_line)
    {
        if (command_line == nullptr ||
            std::strstr(command_line, "--validate-dxc") == nullptr)
            return -1;

        const auto library_path = Rendering::DX12::D3D12ShaderCompiler::FindDefaultLibraryPath();
        Rendering::DX12::D3D12ShaderCompiler compiler;
        if (library_path.empty() || !compiler.Initialize(library_path))
        {
            std::fprintf(stderr, "DXC validation failed: compiler initialization\n");
            return 82;
        }

        constexpr char shader_source[] = R"(
struct VertexInput { float3 position : POSITION; };
struct VertexOutput { float4 position : SV_POSITION; };
VertexOutput VSMain(VertexInput input)
{
    VertexOutput output;
    output.position = float4(input.position, 1.0f);
    return output;
}
float4 PSMain() : SV_TARGET
{
    return float4(1.0f, 0.25f, 0.1f, 1.0f);
})";

        const auto source_name = std::filesystem::current_path() / "DX12Validation.hlsl";
        const auto vertex = compiler.CompileSource(shader_source, source_name,
            L"VSMain", L"vs_6_0");
        const auto pixel = compiler.CompileSource(shader_source, source_name,
            L"PSMain", L"ps_6_0");

        const std::filesystem::path include_directory =
            std::filesystem::current_path() / "DX12ValidationIncludes";
        const std::filesystem::path include_path =
            include_directory / "DX12ValidationCommon.hlsli";
        std::error_code filesystem_error;
        std::filesystem::create_directories(include_directory, filesystem_error);
        bool include_written = !filesystem_error;
        if (include_written)
        {
            std::ofstream include_file(include_path, std::ios::binary | std::ios::trunc);
            include_file << "static const float4 DX12_VALIDATION_COLOR = "
                "float4(0.1f, 0.2f, 0.3f, 1.0f);\n";
            include_written = static_cast<bool>(include_file);
        }

        constexpr char options_shader_source[] = R"(
#include "DX12ValidationCommon.hlsli"
#ifndef PHASE2_DXC_OPTIONS
#error PHASE2_DXC_OPTIONS missing
#endif
float4 main() : SV_TARGET
{
    return DX12_VALIDATION_COLOR * PHASE2_DXC_OPTIONS;
})";
        Rendering::DX12::D3D12ShaderCompileOptions options;
        options.debug = false;
        options.optimize = true;
        options.warnings_as_errors = true;
        options.include_directories.push_back(include_directory);
        options.defines.push_back({ L"PHASE2_DXC_OPTIONS", L"1" });
        Rendering::DX12::D3D12ShaderCompileResult options_pixel;
        if (include_written)
        {
            options_pixel = compiler.CompileSource(options_shader_source,
                std::filesystem::current_path() / "DX12ValidationOptions.hlsl",
                L"main", L"ps_6_0", options);
        }
        std::filesystem::remove(include_path, filesystem_error);
        filesystem_error.clear();
        std::filesystem::remove(include_directory, filesystem_error);

        compiler.Shutdown();
        if (!vertex.succeeded || vertex.bytecode.empty() ||
            !pixel.succeeded || pixel.bytecode.empty() || !include_written ||
            !options_pixel.succeeded || options_pixel.bytecode.empty())
        {
            std::fprintf(stderr,
                "DXC validation failed: SM6 compilation VS=0x%08lx PS=0x%08lx\nVS: %s\nPS: %s\n",
                static_cast<unsigned long>(vertex.status),
                static_cast<unsigned long>(pixel.status),
                vertex.diagnostics.c_str(), pixel.diagnostics.c_str());
            if (!include_written)
                std::fprintf(stderr, "DXC Phase2 include test failed: temp include write\n");
            else if (!options_pixel.succeeded)
                std::fprintf(stderr, "DXC Phase2 options test failed: %s\n",
                    options_pixel.diagnostics.c_str());
            return 83;
        }
        std::fprintf(stderr,
            "DXC validation passed: VS=%zu PS=%zu options/include=%zu\n",
            vertex.bytecode.size(), pixel.bytecode.size(), options_pixel.bytecode.size());
        return 0;
    }
}
