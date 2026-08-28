#include "framework.h"
#include "mainInternal.h"

#include "../../../RePlayEngine/Rendering/DX12/D3D12ShaderCompiler.h"

#include <cstdio>
#include <cstring>
#include <filesystem>

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

        const std::filesystem::path shader_directory =
            std::filesystem::current_path() / "Shader";
        const auto vertex = compiler.CompileFile(
            shader_directory / "dx12_dxc_validation_vs.hlsl", L"main", L"vs_6_0");
        const auto pixel = compiler.CompileFile(
            shader_directory / "dx12_dxc_validation_ps.hlsl", L"main", L"ps_6_0");

        Rendering::DX12::D3D12ShaderCompileOptions options;
        options.debug = false;
        options.optimize = true;
        options.warnings_as_errors = true;
        options.include_directories.push_back(shader_directory);
        options.defines.push_back({ L"PHASE2_DXC_OPTIONS", L"1" });
        const auto options_pixel = compiler.CompileFile(
            shader_directory / "dx12_dxc_options_validation_ps.hlsl",
            L"main", L"ps_6_0", options);

        compiler.Shutdown();
        if (!vertex.succeeded || vertex.bytecode.empty() ||
            !pixel.succeeded || pixel.bytecode.empty() ||
            !options_pixel.succeeded || options_pixel.bytecode.empty())
        {
            std::fprintf(stderr,
                "DXC validation failed: SM6 compilation VS=0x%08lx PS=0x%08lx\nVS: %s\nPS: %s\n",
                static_cast<unsigned long>(vertex.status),
                static_cast<unsigned long>(pixel.status),
                vertex.diagnostics.c_str(), pixel.diagnostics.c_str());
            if (!options_pixel.succeeded)
                std::fprintf(stderr, "DXC Phase2 options/include test failed: %s\n",
                    options_pixel.diagnostics.c_str());
            return 83;
        }
        std::fprintf(stderr,
            "DXC validation passed: VS=%zu PS=%zu options/include=%zu\n",
            vertex.bytecode.size(), pixel.bytecode.size(), options_pixel.bytecode.size());
        return 0;
    }
}
