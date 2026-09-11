#pragma once

#include "ShaderCatalog.h"
#include "../DX12/D3D12ShaderCompiler.h"

#include <filesystem>
#include <string>
#include <vector>

namespace ReplayEngine::Rendering::ShaderPack
{
    bool IsStandalone() noexcept;
    bool IsRecordingExport() noexcept;
    bool Configure(bool standalone, const std::filesystem::path& root, std::string& error);
    std::string RelativeSource(const std::filesystem::path& source);
    DX12::D3D12ShaderCompileOptions SurfaceOptions(bool debug);
    DX12::D3D12ShaderCompileOptions UIOptions(bool debug);
    std::string ComposeSource(const std::string& body, const std::string& declaration,
        const std::filesystem::path& path, bool ui);
    std::string Key(const std::filesystem::path& source, std::wstring_view entry,
        std::wstring_view target, const DX12::D3D12ShaderCompileOptions& options);
    DX12::D3D12ShaderCompileResult Lookup(const std::filesystem::path& source,
        std::wstring_view entry, std::wstring_view target,
        const DX12::D3D12ShaderCompileOptions& options);
    void Record(const std::filesystem::path& source, std::wstring_view entry,
        std::wstring_view target, const DX12::D3D12ShaderCompileOptions& options,
        const DX12::D3D12ShaderCompileResult& result);
    bool RestoreCatalog(ShaderCatalog& catalog, std::string& error);
    bool Build(const std::filesystem::path& root, const std::filesystem::path& output,
        std::vector<std::string>& errors);
    bool ValidateReferences(const std::filesystem::path& root, std::vector<std::string>& errors);
}
