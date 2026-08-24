#pragma once

#include <windows.h>
#include <unknwn.h>
#include <wrl.h>
#include <dxcapi.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace ReplayEngine::Rendering::DX12
{
    struct D3D12ShaderCompileResult final
    {
        bool succeeded = false;
        HRESULT status = E_FAIL;
        std::vector<std::uint8_t> bytecode;
        std::string diagnostics;
    };

    class D3D12ShaderCompiler final
    {
    public:
        D3D12ShaderCompiler() = default;
        ~D3D12ShaderCompiler();

        D3D12ShaderCompiler(const D3D12ShaderCompiler&) = delete;
        D3D12ShaderCompiler& operator=(const D3D12ShaderCompiler&) = delete;

        bool Initialize(const std::filesystem::path& library_path) noexcept;
        void Shutdown() noexcept;

        D3D12ShaderCompileResult CompileSource(std::string_view source,
            const std::filesystem::path& source_name, std::wstring_view entry_point,
            std::wstring_view target_profile, bool debug = false) const;
        D3D12ShaderCompileResult CompileFile(const std::filesystem::path& source_path,
            std::wstring_view entry_point, std::wstring_view target_profile,
            bool debug = false) const;

        bool IsInitialized() const noexcept
        {
            return library_ != nullptr && utils_ != nullptr && compiler_ != nullptr;
        }

        static std::filesystem::path FindDefaultLibraryPath();

    private:
        HMODULE library_ = nullptr;
        DxcCreateInstanceProc create_instance_ = nullptr;
        Microsoft::WRL::ComPtr<IDxcUtils> utils_;
        Microsoft::WRL::ComPtr<IDxcCompiler3> compiler_;
    };
}
