#include "D3D12ShaderCompiler.h"
#include <vector>
#include <cstdio>
#include <algorithm>

#include <windows.h>

#include <fstream>
#include <atomic>
#include <chrono>
#include <iterator>
#include <utility>

namespace ReplayEngine::Rendering::DX12
{
    namespace
    {
        std::atomic<std::uint64_t> g_compile_count{ 0 };
        std::atomic<std::uint64_t> g_failure_count{ 0 };
        std::atomic<std::uint64_t> g_compile_nanoseconds{ 0 };
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

    D3D12ShaderCompilerStats GetD3D12ShaderCompilerStats() noexcept
    {
        D3D12ShaderCompilerStats result{};
        result.compile_count = g_compile_count.load(std::memory_order_relaxed);
        result.failure_count = g_failure_count.load(std::memory_order_relaxed);
        result.total_milliseconds = static_cast<double>(
            g_compile_nanoseconds.load(std::memory_order_relaxed)) / 1000000.0;
        return result;
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


namespace
{
    // 起動のたびに 40 本以上コンパイルすると数秒かかる。内容が同じなら焼き直さない。
    std::uint64_t FnvHash(std::uint64_t seed, const void* data, std::size_t size) noexcept
    {
        const auto* bytes = static_cast<const std::uint8_t*>(data);
        std::uint64_t hash = seed;
        for (std::size_t i = 0; i < size; ++i)
        {
            hash ^= bytes[i];
            hash *= 1099511628211ull;
        }
        return hash;
    }

    // include されるヘッダが変わってもソース本体は変わらない。まとめて指紋に混ぜる。
    std::uint64_t ShaderIncludeTreeHash() noexcept
    {
        static const std::uint64_t cached = []() noexcept -> std::uint64_t
        {
            std::uint64_t hash = 14695981039346656037ull;
            std::error_code error;
            const std::filesystem::path root{ "Shader" };
            if (!std::filesystem::exists(root, error) || error) return hash;
            std::vector<std::filesystem::path> headers;
            for (std::filesystem::recursive_directory_iterator it(root, error), end;
                it != end && !error; it.increment(error))
            {
                if (!it->is_regular_file(error) || error) continue;
                if (it->path().extension() != ".hlsli") continue;
                headers.push_back(it->path());
            }
            std::sort(headers.begin(), headers.end());
            for (const std::filesystem::path& header : headers)
            {
                std::ifstream stream(header, std::ios::binary);
                if (!stream) continue;
                const std::string text{ std::istreambuf_iterator<char>(stream),
                    std::istreambuf_iterator<char>() };
                const std::string name = header.generic_string();
                hash = FnvHash(hash, name.data(), name.size());
                hash = FnvHash(hash, text.data(), text.size());
            }
            return hash;
        }();
        return cached;
    }

    std::filesystem::path ShaderCachePath(std::uint64_t key)
    {
        char name[32]{};
        std::snprintf(name, sizeof(name), "%016llx.dxil",
            static_cast<unsigned long long>(key));
        return std::filesystem::path("Saved") / "ShaderCache" / name;
    }

    bool ReadShaderCache(const std::filesystem::path& path, std::vector<std::uint8_t>& out)
    {
        std::ifstream stream(path, std::ios::binary);
        if (!stream) return false;
        out.assign(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
        return !out.empty();
    }

    void WriteShaderCache(const std::filesystem::path& path,
        const std::vector<std::uint8_t>& bytes) noexcept
    {
        std::error_code error;
        std::filesystem::create_directories(path.parent_path(), error);
        if (error) return;
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        if (!stream) return;
        stream.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
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

        // 内容が同じなら焼き直さない。指紋にはソース・入口・profile・オプションと
        // include ツリー全体を混ぜる。1 つでも変われば別のキーになる。
        std::uint64_t cache_key = ShaderIncludeTreeHash();
        cache_key = FnvHash(cache_key, source.data(), source.size());
        cache_key = FnvHash(cache_key, entry_point.data(),
            entry_point.size() * sizeof(wchar_t));
        cache_key = FnvHash(cache_key, target_profile.data(),
            target_profile.size() * sizeof(wchar_t));
        const std::uint8_t option_bits = static_cast<std::uint8_t>(
            (options.debug ? 1u : 0u) | (options.optimize ? 2u : 0u) |
            (options.warnings_as_errors ? 4u : 0u));
        cache_key = FnvHash(cache_key, &option_bits, sizeof(option_bits));
        for (const D3D12ShaderDefine& define : options.defines)
        {
            cache_key = FnvHash(cache_key, define.name.data(),
                define.name.size() * sizeof(wchar_t));
            cache_key = FnvHash(cache_key, define.value.data(),
                define.value.size() * sizeof(wchar_t));
        }
        const std::filesystem::path cache_path = ShaderCachePath(cache_key);
        if (ReadShaderCache(cache_path, result.bytecode))
        {
            result.succeeded = true;
            result.status = S_OK;
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
        const auto compile_begin = std::chrono::steady_clock::now();
        const HRESULT compile_result = compiler_->Compile(&buffer, arguments.data(),
            static_cast<UINT32>(arguments.size()), include_handler.Get(),
            IID_PPV_ARGS(&compilation));
        const auto compile_end = std::chrono::steady_clock::now();
        g_compile_count.fetch_add(1, std::memory_order_relaxed);
        g_compile_nanoseconds.fetch_add(static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                compile_end - compile_begin).count()), std::memory_order_relaxed);
        if (FAILED(compile_result) || compilation == nullptr)
        {
            result.status = compile_result;
            result.diagnostics = "DXC Compile call failed";
            g_failure_count.fetch_add(1, std::memory_order_relaxed);
            return result;
        }

        Microsoft::WRL::ComPtr<IDxcBlobEncoding> errors;
        if (compilation->HasOutput(DXC_OUT_ERRORS))
            compilation->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr);
        result.diagnostics = ReadBlobText(errors.Get());
        compilation->GetStatus(&result.status);
        if (FAILED(result.status))
        {
            g_failure_count.fetch_add(1, std::memory_order_relaxed);
            return result;
        }

        Microsoft::WRL::ComPtr<IDxcBlob> object;
        if (FAILED(compilation->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&object), nullptr)) ||
            object == nullptr || object->GetBufferPointer() == nullptr)
        {
            result.status = E_FAIL;
            result.diagnostics += "DXC object output is missing";
            g_failure_count.fetch_add(1, std::memory_order_relaxed);
            return result;
        }
        const auto* bytes = static_cast<const std::uint8_t*>(object->GetBufferPointer());
        result.bytecode.assign(bytes, bytes + object->GetBufferSize());
        result.succeeded = true;
        // 診断が出たシェーダはキャッシュしない。読み出しでは診断文を再現できず、
        // 2 回目以降に警告が黙って消えるため。綺麗に通ったものだけ保存する。
        if (result.diagnostics.empty()) WriteShaderCache(cache_path, result.bytecode);
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
