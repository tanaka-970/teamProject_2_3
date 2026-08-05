#include "ShaderCompileValidation.h"

#include "ShaderCompiler.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace ReplayEngine::Rendering::Validation
{
    namespace
    {
        // 既存の Validation と同じ形の判定器。
        // 失敗したら最初の番号を返し、全部の結果を stderr へ出す。
        class Checker final
        {
        public:
            explicit Checker(int first_code) : next_code_(first_code) {}

            void Expect(bool condition, const char* what)
            {
                const int code = next_code_++;
                ++total_;
                if (condition) return;
                ++failures_;
                if (first_failure_ == 0) first_failure_ = code;
                std::fprintf(stderr, "  [FAIL %d] %s\n", code, what);
            }

            int Report(const char* title) const
            {
                if (first_failure_ == 0)
                {
                    std::fprintf(stderr, "%s OK: %d checks passed\n", title, total_);
                    return 0;
                }
                std::fprintf(stderr, "%s FAILED: %d/%d checks failed (first=%d)\n",
                    title, failures_, total_, first_failure_);
                return first_failure_;
            }

        private:
            int next_code_ = 0;
            int first_failure_ = 0;
            int total_ = 0;
            int failures_ = 0;
        };

        bool WriteText(const std::filesystem::path& path, const std::string& text)
        {
            std::error_code error;
            if (!path.parent_path().empty())
            {
                std::filesystem::create_directories(path.parent_path(), error);
            }
            std::ofstream stream(path, std::ios::binary | std::ios::trunc);
            if (!stream) return false;
            stream << text;
            return static_cast<bool>(stream);
        }

        // 正常にコンパイルできる最小のピクセルシェーダ。
        std::string ValidPixelShader()
        {
            return
                "float4 main(float4 position : SV_POSITION) : SV_TARGET\n"
                "{\n"
                "    return float4(1.0f, 0.5f, 0.25f, 1.0f);\n"
                "}\n";
        }

        // 未宣言の識別子を使う。X3004 になる。
        std::string BrokenPixelShader()
        {
            return
                "float4 main(float4 position : SV_POSITION) : SV_TARGET\n"
                "{\n"
                "    return float4(undeclared_value, 0.0f, 0.0f, 1.0f);\n"
                "}\n";
        }

        // 暗黙の切り捨て警告（X3206）を出すが、コンパイルは通る。
        std::string WarningPixelShader()
        {
            return
                "float4 main(float4 position : SV_POSITION) : SV_TARGET\n"
                "{\n"
                "    float3 color = float4(1.0f, 1.0f, 1.0f, 1.0f);\n"
                "    return float4(color, 1.0f);\n"
                "}\n";
        }

        std::string IncludingPixelShader()
        {
            return
                "#include \"ValidationInclude.hlsli\"\n"
                "float4 main(float4 position : SV_POSITION) : SV_TARGET\n"
                "{\n"
                "    return ValidationColor();\n"
                "}\n";
        }

        std::string IncludedHeader()
        {
            return
                "float4 ValidationColor()\n"
                "{\n"
                "    return float4(0.25f, 0.5f, 0.75f, 1.0f);\n"
                "}\n";
        }

        std::string MissingIncludeShader()
        {
            return
                "#include \"ThisFileDoesNotExist.hlsli\"\n"
                "float4 main(float4 position : SV_POSITION) : SV_TARGET\n"
                "{\n"
                "    return float4(1.0f, 1.0f, 1.0f, 1.0f);\n"
                "}\n";
        }
    }

    int RunShaderCompileValidation()
    {
        Checker check(900);

        const std::filesystem::path folder =
            std::filesystem::path("Saved") / "Validation" / "Shader";
        std::error_code folder_error;
        std::filesystem::create_directories(folder, folder_error);

        const ShaderCompiler::Options options =
            ShaderCompiler::DefaultOptions(false);

        // ---- 1. 正常なシェーダをコンパイルできる -------------------------
        const std::filesystem::path valid_path = folder / "ValidationValid.hlsl";
        check.Expect(WriteText(valid_path, ValidPixelShader()),
            "検証用シェーダを書き出せる");

        Microsoft::WRL::ComPtr<ID3DBlob> valid_bytecode;
        const ShaderCompileResult valid = ShaderCompiler::CompileFile(
            valid_path, "main", "ps_5_0", options, valid_bytecode);

        check.Expect(valid.succeeded, "正常なシェーダをコンパイルできる");
        check.Expect(valid_bytecode != nullptr, "バイトコードが得られる");
        check.Expect(valid_bytecode == nullptr || valid_bytecode->GetBufferSize() > 0,
            "バイトコードが空でない");
        check.Expect(valid.ErrorCount() == 0, "正常なシェーダでエラーが出ない");

        // ---- 2. 構文エラーを検出し、位置が取れる -------------------------
        const std::filesystem::path broken_path = folder / "ValidationBroken.hlsl";
        check.Expect(WriteText(broken_path, BrokenPixelShader()),
            "壊れたシェーダを書き出せる");

        Microsoft::WRL::ComPtr<ID3DBlob> broken_bytecode;
        const ShaderCompileResult broken = ShaderCompiler::CompileFile(
            broken_path, "main", "ps_5_0", options, broken_bytecode);

        check.Expect(!broken.succeeded, "構文エラーを検出する");
        check.Expect(broken.ErrorCount() > 0, "エラー診断が 1 件以上ある");
        check.Expect(!broken.raw_output.empty(), "生の出力が残っている");

        const ShaderDiagnostic* first_error = broken.FirstError();
        check.Expect(first_error != nullptr, "最初のエラーを取り出せる");
        check.Expect(first_error != nullptr && first_error->line > 0,
            "エラーに行番号が付く");
        check.Expect(first_error != nullptr && !first_error->message.empty(),
            "エラーに本文が付く");
        check.Expect(first_error != nullptr && !first_error->code.empty(),
            "エラーにコードが付く");
        check.Expect(first_error != nullptr && !first_error->file.empty(),
            "エラーにファイル名が付く");

        // 【最重要】失敗時に out_bytecode を触らないこと。
        //
        // 直前に成功したバイトコードで描き続けられるようにするための約束。
        // ここが崩れるとコンパイル失敗のたびに画面が真っ黒になり、
        // 何を直せばよいか分からなくなる。
        check.Expect(broken_bytecode == nullptr,
            "失敗時に出力バイトコードを触らない");

        // 直前に成功したバイトコードを渡して失敗させ、維持されるか見る。
        Microsoft::WRL::ComPtr<ID3DBlob> kept_bytecode = valid_bytecode;
        const ShaderCompileResult kept = ShaderCompiler::CompileFile(
            broken_path, "main", "ps_5_0", options, kept_bytecode);
        check.Expect(!kept.succeeded, "2 回目も失敗する");
        check.Expect(kept_bytecode.Get() == valid_bytecode.Get(),
            "失敗しても直前に成功したバイトコードが維持される");

        // ---- 3. 警告だけなら成功扱い -------------------------------------
        const std::filesystem::path warning_path = folder / "ValidationWarning.hlsl";
        check.Expect(WriteText(warning_path, WarningPixelShader()),
            "警告シェーダを書き出せる");

        Microsoft::WRL::ComPtr<ID3DBlob> warning_bytecode;
        const ShaderCompileResult warning = ShaderCompiler::CompileFile(
            warning_path, "main", "ps_5_0", options, warning_bytecode);
        check.Expect(warning.succeeded, "警告だけなら成功する");
        check.Expect(warning_bytecode != nullptr, "警告時もバイトコードが得られる");

        // ---- 4. #include を解決できる ------------------------------------
        check.Expect(WriteText(folder / "ValidationInclude.hlsli", IncludedHeader()),
            "include されるヘッダを書き出せる");
        const std::filesystem::path including_path = folder / "ValidationInclude.hlsl";
        check.Expect(WriteText(including_path, IncludingPixelShader()),
            "include するシェーダを書き出せる");

        Microsoft::WRL::ComPtr<ID3DBlob> including_bytecode;
        const ShaderCompileResult including = ShaderCompiler::CompileFile(
            including_path, "main", "ps_5_0", options, including_bytecode);
        check.Expect(including.succeeded,
            "同じフォルダの #include を解決できる");

        // ---- 5. 見つからない #include はエラーになる ---------------------
        const std::filesystem::path missing_path = folder / "ValidationMissingInclude.hlsl";
        check.Expect(WriteText(missing_path, MissingIncludeShader()),
            "壊れた include のシェーダを書き出せる");

        Microsoft::WRL::ComPtr<ID3DBlob> missing_bytecode;
        const ShaderCompileResult missing = ShaderCompiler::CompileFile(
            missing_path, "main", "ps_5_0", options, missing_bytecode);
        check.Expect(!missing.succeeded, "見つからない #include を検出する");
        check.Expect(missing.ErrorCount() > 0, "include エラーの診断が出る");

        // ---- 6. 存在しないファイル ---------------------------------------
        Microsoft::WRL::ComPtr<ID3DBlob> absent_bytecode;
        const ShaderCompileResult absent = ShaderCompiler::CompileFile(
            folder / "ThisFileDoesNotExist.hlsl", "main", "ps_5_0",
            options, absent_bytecode);
        check.Expect(!absent.succeeded, "存在しないファイルで失敗する");
        check.Expect(absent.ErrorCount() > 0,
            "存在しないファイルでも理由が残る");

        // ---- 7. 診断の分解 ------------------------------------------------
        {
            const std::string sample =
                "C:/a/b.hlsl(42,17-25): error X3004: undeclared identifier 'foo'\n"
                "C:/a/b.hlsl(50,5): warning X3206: implicit truncation\n"
                "分解できない行\n";
            const std::vector<ShaderDiagnostic> parsed =
                ShaderCompiler::ParseDiagnostics(sample, "fallback.hlsl");

            check.Expect(parsed.size() == 3,
                "分解できない行も捨てずに残す");
            check.Expect(parsed.size() > 0 &&
                parsed[0].severity == ShaderDiagnostic::Severity::Error,
                "error を Error として分解する");
            check.Expect(parsed.size() > 0 && parsed[0].line == 42,
                "行番号を分解する");
            check.Expect(parsed.size() > 0 && parsed[0].column == 17,
                "列番号を分解する（範囲指定の始点）");
            check.Expect(parsed.size() > 0 && parsed[0].code == "X3004",
                "コードを分解する");
            check.Expect(parsed.size() > 0 &&
                parsed[0].message == "undeclared identifier 'foo'",
                "本文を分解する");
            check.Expect(parsed.size() > 1 &&
                parsed[1].severity == ShaderDiagnostic::Severity::Warning,
                "warning を Warning として分解する");
            check.Expect(parsed.size() > 2 &&
                parsed[2].message == "分解できない行",
                "分解できない行は本文だけ入れて残す");
        }

        // ---- 8. 繰り返しコンパイルしても壊れない -------------------------
        //
        // C# の「100 回 Reload」と同じ趣旨。
        // 1 回動くことと、何度も動くことは別物。
        bool repeated_ok = true;
        for (int index = 0; index < 100; ++index)
        {
            Microsoft::WRL::ComPtr<ID3DBlob> bytecode;
            const ShaderCompileResult repeat = ShaderCompiler::CompileFile(
                valid_path, "main", "ps_5_0", options, bytecode);
            if (!repeat.succeeded || bytecode == nullptr)
            {
                repeated_ok = false;
                break;
            }
        }
        check.Expect(repeated_ok, "100 回連続でコンパイルできる");

        // ---- 9. 文字列からのコンパイル -----------------------------------
        {
            Microsoft::WRL::ComPtr<ID3DBlob> bytecode;
            const ShaderCompileResult from_text = ShaderCompiler::CompileSource(
                ValidPixelShader(), "InMemory.hlsl", "main", "ps_5_0",
                options, bytecode);
            check.Expect(from_text.succeeded, "文字列から直接コンパイルできる");
            check.Expect(bytecode != nullptr, "文字列からバイトコードが得られる");
        }

        // ---- 10. 頂点シェーダも通る ---------------------------------------
        {
            const std::string vertex_source =
                "float4 main(float3 position : POSITION) : SV_POSITION\n"
                "{\n"
                "    return float4(position, 1.0f);\n"
                "}\n";
            Microsoft::WRL::ComPtr<ID3DBlob> bytecode;
            const ShaderCompileResult vertex = ShaderCompiler::CompileSource(
                vertex_source, "InMemoryVS.hlsl", "main", "vs_5_0",
                options, bytecode);
            check.Expect(vertex.succeeded, "頂点シェーダをコンパイルできる");
        }

        return check.Report("shader-compile");
    }
}
