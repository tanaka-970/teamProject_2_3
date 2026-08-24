#include "framework.h"
#include "mainInternal.h"

#include "../../../RePlayEngine/Rendering/DX12/D3D12ShaderCompiler.h"

#include <cstdio>
#include <cstring>

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
        compiler.Shutdown();
        if (!vertex.succeeded || vertex.bytecode.empty() ||
            !pixel.succeeded || pixel.bytecode.empty())
        {
            std::fprintf(stderr,
                "DXC validation failed: SM6 compilation VS=0x%08lx PS=0x%08lx\nVS: %s\nPS: %s\n",
                static_cast<unsigned long>(vertex.status),
                static_cast<unsigned long>(pixel.status),
                vertex.diagnostics.c_str(), pixel.diagnostics.c_str());
            return 83;
        }
        std::fprintf(stderr, "DXC validation passed: VS=%zu PS=%zu\n",
            vertex.bytecode.size(), pixel.bytecode.size());
        return 0;
    }
}
