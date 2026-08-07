#include "ShaderCompiler.h"

#include <d3dcompiler.h>

#include <cstring>
#include <fstream>
#include <regex>
#include <sstream>

#pragma comment(lib, "d3dcompiler.lib")

namespace ReplayEngine::Rendering
{
    const char* ToString(ShaderDiagnostic::Severity severity) noexcept
    {
        switch (severity)
        {
        case ShaderDiagnostic::Severity::Info:    return "Info";
        case ShaderDiagnostic::Severity::Warning: return "Warning";
        case ShaderDiagnostic::Severity::Error:   return "Error";
        default:                                  return "Unknown";
        }
    }

    std::size_t ShaderCompileResult::ErrorCount() const noexcept
    {
        std::size_t count = 0;
        for (const ShaderDiagnostic& item : diagnostics)
        {
            if (item.severity == ShaderDiagnostic::Severity::Error) ++count;
        }
        return count;
    }

    const ShaderDiagnostic* ShaderCompileResult::FirstError() const noexcept
    {
        for (const ShaderDiagnostic& item : diagnostics)
        {
            if (item.severity == ShaderDiagnostic::Severity::Error) return &item;
        }
        return nullptr;
    }

    std::string ShaderCompileResult::Summary() const
    {
        std::ostringstream stream;
        stream << (succeeded ? "成功" : "失敗");
        stream << " / 診断 " << diagnostics.size() << " 件";
        stream << " / エラー " << ErrorCount() << " 件";
        stream << " / " << duration.count() << " ms";
        if (const ShaderDiagnostic* first = FirstError())
        {
            stream << " / 最初のエラー: ";
            if (!first->code.empty()) stream << first->code << ": ";
            stream << first->message;
            if (!first->file.empty())
            {
                stream << " (" << first->file.filename().u8string();
                if (first->line > 0) stream << ':' << first->line;
                stream << ')';
            }
        }
        return stream.str();
    }

    namespace
    {
        // #include を解決する。
        //
        // D3D_COMPILE_STANDARD_FILE_INCLUDE は使わない。
        // あれはプロセスのカレントディレクトリ基準で動くため、
        // エディタの cwd に依存して壊れる。
        // 探索先を明示的に持つ実装にする。
        class IncludeHandler final : public ID3DInclude
        {
        public:
            IncludeHandler(const std::filesystem::path& source_directory,
                const std::vector<std::filesystem::path>& directories)
                : source_directory_(source_directory)
                , directories_(directories)
            {
            }

            HRESULT __stdcall Open(D3D_INCLUDE_TYPE /*type*/, LPCSTR file_name,
                LPCVOID /*parent_data*/, LPCVOID* out_data, UINT* out_bytes) override
            {
                if (file_name == nullptr || out_data == nullptr || out_bytes == nullptr)
                {
                    return E_INVALIDARG;
                }

                std::filesystem::path resolved;
                if (!Resolve(file_name, resolved)) return E_FAIL;

                std::ifstream stream(resolved, std::ios::binary);
                if (!stream) return E_FAIL;

                auto text = std::make_unique<std::string>(
                    (std::istreambuf_iterator<char>(stream)),
                    std::istreambuf_iterator<char>());

                *out_data = text->data();
                *out_bytes = static_cast<UINT>(text->size());

                // Close が呼ばれるまで生かす。
                opened_.push_back(std::move(text));
                return S_OK;
            }

            HRESULT __stdcall Close(LPCVOID data) override
            {
                for (auto it = opened_.begin(); it != opened_.end(); ++it)
                {
                    if ((*it)->data() == data)
                    {
                        opened_.erase(it);
                        return S_OK;
                    }
                }
                // 知らないポインタでも失敗にしない。
                // ここで失敗を返すとコンパイル全体が止まる。
                return S_OK;
            }

        private:
            bool Resolve(const char* file_name, std::filesystem::path& out) const
            {
                std::error_code error;
                const std::filesystem::path relative =
                    std::filesystem::path(std::string(file_name));

                // 1) ソースと同じフォルダ
                if (!source_directory_.empty())
                {
                    const std::filesystem::path candidate = source_directory_ / relative;
                    if (std::filesystem::exists(candidate, error) && !error)
                    {
                        out = candidate;
                        return true;
                    }
                    error.clear();
                }

                // 2) 指定された探索先を順に
                for (const std::filesystem::path& directory : directories_)
                {
                    const std::filesystem::path candidate = directory / relative;
                    if (std::filesystem::exists(candidate, error) && !error)
                    {
                        out = candidate;
                        return true;
                    }
                    error.clear();
                }
                return false;
            }

            std::filesystem::path source_directory_;
            std::vector<std::filesystem::path> directories_;
            std::vector<std::unique_ptr<std::string>> opened_;
        };

        std::string ReadAllText(const std::filesystem::path& path)
        {
            std::ifstream stream(path, std::ios::binary);
            if (!stream) return std::string();
            return std::string((std::istreambuf_iterator<char>(stream)),
                std::istreambuf_iterator<char>());
        }
    }

    ShaderCompiler::Options ShaderCompiler::DefaultOptions(bool debug_build)
    {
        Options options;
        options.debug_info = debug_build;
        options.optimize = !debug_build;
        options.warnings_as_errors = false;
        options.include_directories.push_back(
            std::filesystem::path("Shader") / "Include");
        options.include_directories.push_back(std::filesystem::path("Shader"));
        return options;
    }

    std::vector<ShaderDiagnostic> ShaderCompiler::ParseDiagnostics(
        const std::string& raw_output, const std::filesystem::path& fallback_file)
    {
        std::vector<ShaderDiagnostic> diagnostics;
        if (raw_output.empty()) return diagnostics;

        // path(line,col) : severity code : message
        // 列は "17" か "17-25" の形がある。終端は捨てる。
        static const std::regex expression(
            R"(^(.*?)\((\d+)(?:,(\d+)(?:-\d+)?)?\)\s*:\s*(error|warning|info)\s*([A-Za-z0-9_]*)\s*:\s*(.*)$)",
            std::regex::optimize);

        std::istringstream stream(raw_output);
        std::string line;
        while (std::getline(stream, line))
        {
            // 末尾の CR を落とす。
            while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
            {
                line.pop_back();
            }
            if (line.empty()) continue;

            std::smatch match;
            if (std::regex_match(line, match, expression))
            {
                ShaderDiagnostic item;
                item.file = match[1].str().empty()
                    ? fallback_file : std::filesystem::path(match[1].str());
                item.line = std::atoi(match[2].str().c_str());
                item.column = match[3].matched ? std::atoi(match[3].str().c_str()) : 0;

                const std::string severity = match[4].str();
                if (severity == "warning") item.severity = ShaderDiagnostic::Severity::Warning;
                else if (severity == "info") item.severity = ShaderDiagnostic::Severity::Info;
                else item.severity = ShaderDiagnostic::Severity::Error;

                item.code = match[5].str();
                item.message = match[6].str();
                diagnostics.push_back(std::move(item));
                continue;
            }

            // 分解できない行も捨てない。
            //
            // 捨てると「コンパイルは失敗しているのに一覧が空」になり、
            // 原因が一切追えなくなる。書式が想定と違うだけかもしれないので、
            // 本文だけ入れて残す。
            ShaderDiagnostic item;
            item.severity = ShaderDiagnostic::Severity::Error;
            item.file = fallback_file;
            item.message = line;
            diagnostics.push_back(std::move(item));
        }
        return diagnostics;
    }

    ShaderCompileResult ShaderCompiler::CompileSource(
        const std::string& source_text,
        const std::filesystem::path& source_name,
        const char* entry_point,
        const char* target,
        const Options& options,
        Microsoft::WRL::ComPtr<ID3DBlob>& out_bytecode)
    {
        ShaderCompileResult result;
        const auto started = std::chrono::steady_clock::now();

        if (source_text.empty())
        {
            result.raw_output = "ソースが空です: " + source_name.generic_u8string();
            result.diagnostics = ParseDiagnostics(result.raw_output, source_name);
            return result;
        }
        if (entry_point == nullptr || target == nullptr)
        {
            result.raw_output = "entry_point または target が未指定です";
            result.diagnostics = ParseDiagnostics(result.raw_output, source_name);
            return result;
        }

        UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
        if (options.debug_info) flags |= D3DCOMPILE_DEBUG;
        flags |= options.optimize
            ? D3DCOMPILE_OPTIMIZATION_LEVEL3
            : D3DCOMPILE_SKIP_OPTIMIZATION;
        if (options.warnings_as_errors) flags |= D3DCOMPILE_WARNINGS_ARE_ERRORS;

        // defines は最後に {nullptr, nullptr} で終端する決まり。
        std::vector<D3D_SHADER_MACRO> macros;
        macros.reserve(options.defines.size() + 1);
        for (const auto& pair : options.defines)
        {
            D3D_SHADER_MACRO macro{};
            macro.Name = pair.first.c_str();
            macro.Definition = pair.second.c_str();
            macros.push_back(macro);
        }
        macros.push_back(D3D_SHADER_MACRO{ nullptr, nullptr });

        IncludeHandler include_handler(source_name.parent_path(),
            options.include_directories);

        Microsoft::WRL::ComPtr<ID3DBlob> bytecode;
        Microsoft::WRL::ComPtr<ID3DBlob> errors;
        const std::string source_name_utf8 = source_name.generic_u8string();

        const HRESULT hr = D3DCompile(
            source_text.data(),
            source_text.size(),
            source_name_utf8.c_str(),
            macros.data(),
            &include_handler,
            entry_point,
            target,
            flags,
            0,
            bytecode.GetAddressOf(),
            errors.GetAddressOf());

        if (errors && errors->GetBufferSize() > 0)
        {
            result.raw_output.assign(
                static_cast<const char*>(errors->GetBufferPointer()),
                errors->GetBufferSize());
            // 末尾の NUL を落とす。
            while (!result.raw_output.empty() && result.raw_output.back() == '\0')
            {
                result.raw_output.pop_back();
            }
        }

        result.diagnostics = ParseDiagnostics(result.raw_output, source_name);
        result.succeeded = SUCCEEDED(hr) && bytecode != nullptr;

        if (result.succeeded)
        {
            // 成功したときだけ差し替える。
            //
            // 失敗時に out_bytecode を触らないのが要点。
            // 呼び出し側が持っている「直前に成功したバイトコード」を
            // そのまま使い続けられるようにする。
            // ここで nullptr を入れると画面が真っ黒になり、
            // 何を直せばよいか分からなくなる。
            out_bytecode = bytecode;
        }
        else if (result.diagnostics.empty())
        {
            // HRESULT だけ失敗して出力が空のことがある。
            // 「失敗したのに理由が 1 行も出ない」状態を作らない。
            ShaderDiagnostic item;
            item.severity = ShaderDiagnostic::Severity::Error;
            item.file = source_name;
            std::ostringstream text;
            text << "コンパイルに失敗しました (HRESULT=0x"
                 << std::hex << static_cast<unsigned int>(hr) << ")";
            item.message = text.str();
            result.diagnostics.push_back(std::move(item));
        }

        result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started);
        return result;
    }

    ShaderCompileResult ShaderCompiler::CompileFile(
        const std::filesystem::path& source,
        const char* entry_point,
        const char* target,
        const Options& options,
        Microsoft::WRL::ComPtr<ID3DBlob>& out_bytecode)
    {
        std::error_code error;
        if (!std::filesystem::exists(source, error) || error)
        {
            ShaderCompileResult result;
            result.raw_output =
                "シェーダファイルが見つかりません: " + source.generic_u8string();
            ShaderDiagnostic item;
            item.severity = ShaderDiagnostic::Severity::Error;
            item.file = source;
            item.message = result.raw_output;
            result.diagnostics.push_back(std::move(item));
            return result;
        }

        return CompileSource(ReadAllText(source), source,
            entry_point, target, options, out_bytecode);
    }
}
