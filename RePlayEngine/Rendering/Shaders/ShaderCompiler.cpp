#include "ShaderCompiler.h"
#include "../DX12/D3D12ShaderCompiler.h"

#include <mutex>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <regex>
#include <sstream>

namespace ReplayEngine::Rendering
{
    const char* ToString(ShaderDiagnostic::Severity severity) noexcept
    {
        switch (severity)
        {
        case ShaderDiagnostic::Severity::Info: return "Info";
        case ShaderDiagnostic::Severity::Warning: return "Warning";
        case ShaderDiagnostic::Severity::Error: return "Error";
        default: return "Unknown";
        }
    }

    std::size_t ShaderCompileResult::ErrorCount() const noexcept
    {
        std::size_t count = 0;
        for (const ShaderDiagnostic& item : diagnostics)
            if (item.severity == ShaderDiagnostic::Severity::Error) ++count;
        return count;
    }

    const ShaderDiagnostic* ShaderCompileResult::FirstError() const noexcept
    {
        for (const ShaderDiagnostic& item : diagnostics)
            if (item.severity == ShaderDiagnostic::Severity::Error) return &item;
        return nullptr;
    }

    std::string ShaderCompileResult::Summary() const
    {
        std::ostringstream stream;
        stream << (succeeded ? "成功" : "失敗") << " / 診断 " << diagnostics.size()
            << " 件 / エラー " << ErrorCount() << " 件 / " << duration.count() << " ms";
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
        std::string ReadAllText(const std::filesystem::path& path)
        {
            std::ifstream stream(path, std::ios::binary);
            if (!stream) return {};
            return std::string((std::istreambuf_iterator<char>(stream)),
                std::istreambuf_iterator<char>());
        }

        std::wstring WidenAscii(const char* value)
        {
            std::wstring result;
            if (value == nullptr) return result;
            while (*value != '\0') result.push_back(static_cast<unsigned char>(*value++));
            return result;
        }

        std::wstring ToDxcProfile(const char* target)
        {
            std::wstring profile = WidenAscii(target);
            const std::size_t first = profile.find(L'_');
            const std::size_t second = first == std::wstring::npos
                ? std::wstring::npos : profile.find(L'_', first + 1);
            if (first != std::wstring::npos && second != std::wstring::npos)
            {
                const std::wstring major = profile.substr(first + 1, second - first - 1);
                if (major == L"4" || major == L"5") profile.replace(first + 1,
                    second - first - 1, L"6");
            }
            return profile;
        }
    }

    ShaderCompiler::Options ShaderCompiler::DefaultOptions(bool debug_build)
    {
        Options options;
        options.debug_info = debug_build;
        options.optimize = !debug_build;
        options.include_directories.push_back(std::filesystem::path("Shader") / "Include");
        options.include_directories.push_back(std::filesystem::path("Shader"));
        return options;
    }

    std::vector<ShaderDiagnostic> ShaderCompiler::ParseDiagnostics(
        const std::string& raw_output, const std::filesystem::path& fallback_file)
    {
        std::vector<ShaderDiagnostic> diagnostics;
        if (raw_output.empty()) return diagnostics;
        static const std::regex legacy_diagnostic_expression(
            R"(^(.*?)\((\d+)(?:,(\d+)(?:-\d+)?)?\)\s*:\s*(error|warning|info)\s*([A-Za-z0-9_]*)\s*:\s*(.*)$)",
            std::regex::optimize);
        static const std::regex dxc_expression(
            R"(^(.*?):(\d+):(\d+):\s*(error|warning|note)\s*:\s*(.*)$)",
            std::regex::optimize);
        std::istringstream stream(raw_output);
        std::string line;
        while (std::getline(stream, line))
        {
            while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
            if (line.empty()) continue;
            std::smatch match;
            ShaderDiagnostic item;
            if (std::regex_match(line, match, legacy_diagnostic_expression))
            {
                item.file = match[1].str().empty() ? fallback_file : std::filesystem::path(match[1].str());
                item.line = std::atoi(match[2].str().c_str());
                item.column = match[3].matched ? std::atoi(match[3].str().c_str()) : 0;
                const std::string severity = match[4].str();
                item.severity = severity == "warning" ? ShaderDiagnostic::Severity::Warning :
                    severity == "info" ? ShaderDiagnostic::Severity::Info : ShaderDiagnostic::Severity::Error;
                item.code = match[5].str();
                item.message = match[6].str();
            }
            else if (std::regex_match(line, match, dxc_expression))
            {
                item.file = match[1].str().empty() ? fallback_file : std::filesystem::path(match[1].str());
                item.line = std::atoi(match[2].str().c_str());
                item.column = std::atoi(match[3].str().c_str());
                const std::string severity = match[4].str();
                item.severity = severity == "warning" ? ShaderDiagnostic::Severity::Warning :
                    severity == "note" ? ShaderDiagnostic::Severity::Info : ShaderDiagnostic::Severity::Error;
                item.message = match[5].str();
                const std::size_t code_begin = item.message.find('[');
                const std::size_t code_end = code_begin == std::string::npos ? std::string::npos : item.message.find(']', code_begin + 1);
                if (code_begin != std::string::npos && code_end != std::string::npos)
                    item.code = item.message.substr(code_begin + 1, code_end - code_begin - 1);
            }
            else
            {
                item.file = fallback_file;
                item.severity = line.find("warning") != std::string::npos
                    ? ShaderDiagnostic::Severity::Warning : ShaderDiagnostic::Severity::Error;
                item.message = line;
            }
            diagnostics.push_back(std::move(item));
        }
        return diagnostics;
    }

    ShaderCompileResult ShaderCompiler::CompileSource(
        const std::string& source_text, const std::filesystem::path& source_name,
        const char* entry_point, const char* target, const Options& options,
        ShaderBytecode& out_bytecode)
    {
        ShaderCompileResult result;
        const auto started = std::chrono::steady_clock::now();
        if (source_text.empty() || entry_point == nullptr || target == nullptr)
        {
            result.raw_output = source_text.empty() ? "ソースが空です: " + source_name.generic_u8string()
                : "entry_point または target が未指定です";
            result.diagnostics = ParseDiagnostics(result.raw_output, source_name);
            result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - started);
            return result;
        }

        // dxcompiler.dll の読み込みは数十 ms かかる。1 本ごとに読み直すと起動が数秒延びる。
        static std::mutex compiler_mutex;
        static DX12::D3D12ShaderCompiler compiler;
        static bool compiler_ready = false;
        const std::lock_guard<std::mutex> compiler_lock(compiler_mutex);
        const std::filesystem::path library = DX12::D3D12ShaderCompiler::FindDefaultLibraryPath();
        if (!compiler_ready) compiler_ready = compiler.Initialize(library);
        if (!compiler_ready)
        {
            result.raw_output = "DXC を初期化できません: " + library.generic_u8string();
            result.diagnostics = ParseDiagnostics(result.raw_output, source_name);
            result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - started);
            return result;
        }
        DX12::D3D12ShaderCompileOptions dx_options;
        dx_options.debug = options.debug_info;
        dx_options.optimize = options.optimize;
        dx_options.warnings_as_errors = options.warnings_as_errors;
        dx_options.include_directories = options.include_directories;
        for (const auto& define : options.defines)
            dx_options.defines.push_back({ WidenAscii(define.first.c_str()), WidenAscii(define.second.c_str()) });
        const auto compiled = compiler.CompileSource(source_text, source_name,
            WidenAscii(entry_point), ToDxcProfile(target), dx_options);
        result.raw_output = compiled.diagnostics;
        result.diagnostics = ParseDiagnostics(result.raw_output, source_name);
        result.succeeded = compiled.succeeded && !compiled.bytecode.empty();
        if (result.succeeded) out_bytecode = std::make_shared<std::vector<std::uint8_t>>(compiled.bytecode);
        else if (result.diagnostics.empty())
        {
            ShaderDiagnostic item;
            item.severity = ShaderDiagnostic::Severity::Error;
            item.file = source_name;
            std::ostringstream text;
            text << "DXC コンパイルに失敗しました (HRESULT=0x" << std::hex
                << static_cast<unsigned int>(compiled.status) << ')';
            item.message = text.str();
            result.diagnostics.push_back(std::move(item));
        }
        result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started);
        return result;
    }

    ShaderCompileResult ShaderCompiler::CompileFile(
        const std::filesystem::path& source, const char* entry_point, const char* target,
        const Options& options, ShaderBytecode& out_bytecode)
    {
        std::error_code error;
        if (!std::filesystem::exists(source, error) || error)
        {
            ShaderCompileResult result;
            result.raw_output = "シェーダファイルが見つかりません: " + source.generic_u8string();
            result.diagnostics = ParseDiagnostics(result.raw_output, source);
            return result;
        }
        return CompileSource(ReadAllText(source), source, entry_point, target, options, out_bytecode);
    }
}
