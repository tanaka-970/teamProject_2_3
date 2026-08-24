#include "D3D12ShaderCompiler.h"

#include <windows.h>

#include <fstream>
#include <iterator>
#include <utility>

namespace ReplayEngine::Rendering::DX12
{
    namespace
    {
        std::string ReadBlobText(IDxcBlobEncoding* blob)
        {
            if (blob == nullptr || blob->GetBufferPointer() == nullptr ||
                blob->GetBufferSize() == 0)
                return {};
            const char* text = static_cast<const char*>(blob->GetBufferPointer());
            return std::string(text, text + blob->GetBufferSize());
        }

        void AppendArgument(std::vector<std::wstring>& storage, std::wstring value)
        {
            storage.push_back(std::move(value));
        }
    }

    D3D12ShaderCompiler::~D3D12ShaderCompiler()
    {
        Shutdown();
    }

    bool D3D12ShaderCompiler::Initialize(
        const std::filesystem::path& library_path) noexcept
    {
        Shutdown();
        if (library_path.empty()) return false;
        library_ = LoadLibraryW(library_path.wstring().c_str());
        if (library_ == nullptr) return false;
        create_instance_ = reinterpret_cast<DxcCreateInstanceProc>(
            GetProcAddress(library_, "DxcCreateInstance"));
        if (create_instance_ == nullptr ||
            FAILED(create_instance_(CLSID_DxcUtils, IID_PPV_ARGS(&utils_))) ||
            FAILED(create_instance_(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler_))))
        {
            Shutdown();
            return false;
        }
        return true;
    }

    void D3D12ShaderCompiler::Shutdown() noexcept
    {
        compiler_.Reset();
        utils_.Reset();
        create_instance_ = nullptr;
        if (library_ != nullptr)
        {
            FreeLibrary(library_);
            library_ = nullptr;
        }
    }

    D3D12ShaderCompileResult D3D12ShaderCompiler::CompileSource(
        std::string_view source, const std::filesystem::path& source_name,
        std::wstring_view entry_point, std::wstring_view target_profile,
        bool debug) const
    {
        D3D12ShaderCompileOptions options;
        options.debug = debug;
        options.optimize = !debug;
        return CompileSource(source, source_name, entry_point, target_profile, options);
    }

    D3D12ShaderCompileResult D3D12ShaderCompiler::CompileSource(
        std::string_view source, const std::filesystem::path& source_name,
        std::wstring_view entry_point, std::wstring_view target_profile,
        const D3D12ShaderCompileOptions& options) const
    {
        D3D12ShaderCompileResult result;
        if (!IsInitialized() || source.empty() || entry_point.empty() ||
            target_profile.empty())
        {
            result.diagnostics = "DXC compiler is not initialized or input is empty";
            return result;
        }

        std::vector<std::wstring> argument_storage;
        std::vector<LPCWSTR> arguments;
        AppendArgument(argument_storage, L"-E");
        AppendArgument(argument_storage, std::wstring(entry_point));
        AppendArgument(argument_storage, L"-T");
        AppendArgument(argument_storage, std::wstring(target_profile));
        AppendArgument(argument_storage, L"-HV");
        AppendArgument(argument_storage, L"2021");
        AppendArgument(argument_storage, L"-Ges");
        if (options.debug)
        {
            AppendArgument(argument_storage, L"-Zi");
            AppendArgument(argument_storage, L"-Qembed_debug");
        }
        AppendArgument(argument_storage, options.optimize ? L"-O3" : L"-Od");
        if (options.warnings_as_errors) AppendArgument(argument_storage, L"-WX");

        const std::filesystem::path include_directory = source_name.has_parent_path()
            ? source_name.parent_path() : std::filesystem::current_path();
        AppendArgument(argument_storage, L"-I");
        AppendArgument(argument_storage, include_directory.wstring());
        for (const std::filesystem::path& include : options.include_directories)
        {
            if (include.empty()) continue;
            AppendArgument(argument_storage, L"-I");
            AppendArgument(argument_storage, include.wstring());
        }
        for (const D3D12ShaderDefine& define : options.defines)
        {
            if (define.name.empty()) continue;
            AppendArgument(argument_storage, L"-D");
            AppendArgument(argument_storage, define.value.empty()
                ? define.name : define.name + L"=" + define.value);
        }
        arguments.reserve(argument_storage.size());
        for (const auto& argument : argument_storage)
            arguments.push_back(argument.c_str());

        DxcBuffer buffer{};
        buffer.Ptr = source.data();
        buffer.Size = source.size();
        buffer.Encoding = DXC_CP_UTF8;
        Microsoft::WRL::ComPtr<IDxcIncludeHandler> include_handler;
        if (FAILED(utils_->CreateDefaultIncludeHandler(&include_handler)))
        {
            result.diagnostics = "DXC include handler creation failed";
            return result;
        }

        Microsoft::WRL::ComPtr<IDxcResult> compilation;
        const HRESULT compile_result = compiler_->Compile(&buffer, arguments.data(),
            static_cast<UINT32>(arguments.size()), include_handler.Get(),
            IID_PPV_ARGS(&compilation));
        if (FAILED(compile_result) || compilation == nullptr)
        {
            result.status = compile_result;
            result.diagnostics = "DXC Compile call failed";
            return result;
        }

        Microsoft::WRL::ComPtr<IDxcBlobEncoding> errors;
        if (compilation->HasOutput(DXC_OUT_ERRORS))
            compilation->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr);
        result.diagnostics = ReadBlobText(errors.Get());
        compilation->GetStatus(&result.status);
        if (FAILED(result.status)) return result;

        Microsoft::WRL::ComPtr<IDxcBlob> object;
        if (FAILED(compilation->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&object), nullptr)) ||
            object == nullptr || object->GetBufferPointer() == nullptr)
        {
            result.status = E_FAIL;
            result.diagnostics += "DXC object output is missing";
            return result;
        }
        const auto* bytes = static_cast<const std::uint8_t*>(object->GetBufferPointer());
        result.bytecode.assign(bytes, bytes + object->GetBufferSize());
        result.succeeded = true;
        return result;
    }

    D3D12ShaderCompileResult D3D12ShaderCompiler::CompileFile(
        const std::filesystem::path& source_path, std::wstring_view entry_point,
        std::wstring_view target_profile, bool debug) const
    {
        D3D12ShaderCompileOptions options;
        options.debug = debug;
        options.optimize = !debug;
        return CompileFile(source_path, entry_point, target_profile, options);
    }

    D3D12ShaderCompileResult D3D12ShaderCompiler::CompileFile(
        const std::filesystem::path& source_path, std::wstring_view entry_point,
        std::wstring_view target_profile,
        const D3D12ShaderCompileOptions& options) const
    {
        std::ifstream file(source_path, std::ios::binary);
        if (!file)
        {
            D3D12ShaderCompileResult result;
            result.diagnostics = "Shader source file could not be opened";
            return result;
        }
        const std::string source((std::istreambuf_iterator<char>(file)),
            std::istreambuf_iterator<char>());
        return CompileSource(source, source_path, entry_point, target_profile, options);
    }

    std::filesystem::path D3D12ShaderCompiler::FindDefaultLibraryPath()
    {
        std::vector<std::filesystem::path> candidates;
        const std::filesystem::path relative =
            std::filesystem::path("ThirdParty") / "DXC" / "bin" / "x64" /
            "dxcompiler.dll";
        candidates.push_back(std::filesystem::current_path() / relative);

        wchar_t executable_path[MAX_PATH]{};
        const DWORD length = GetModuleFileNameW(nullptr, executable_path,
            static_cast<DWORD>(std::size(executable_path)));
        if (length != 0 && length < std::size(executable_path))
        {
            const std::filesystem::path executable_directory(
                std::wstring(executable_path, executable_path + length));
            const auto root = executable_directory.parent_path().parent_path();
            candidates.push_back(root / relative);
        }
        for (const auto& candidate : candidates)
            if (std::filesystem::is_regular_file(candidate)) return candidate;
        return {};
    }
}
