#pragma once

#include "ShaderDiagnostic.h"

#include <d3d11.h>
#include <wrl.h>

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace ReplayEngine::Rendering
{
    // .hlsl を実行時にコンパイルする。
    //
    // 【なぜ実行時にコンパイルするのか】
    //   従来は .cso をビルド時に焼き、C++ へファイル名を直書きしていた
    //   （create_ps_from_cso("static_mesh_pbr_ps.cso", ...) が 53 箇所）。
    //   この形だとシェーダを 1 行直すたびにエンジンの再ビルドが要る。
    //
    //   フォトリアルの詰めは「値を変える」作業ではなく「式を変える」作業で、
    //   1 回の試行に再ビルド 10 分が乗ると試行回数が 1/20 になる。
    //   絵の質は試行回数でほぼ決まるので、ここが最大のボトルネックだった。
    //
    // 【このクラスがやらないこと】
    //   ファイルの走査、キャッシュ、GUID の管理、差し替えの同期。
    //   それらは ShaderLibrary / ShaderCatalog の仕事。
    //   ここは「1 枚の .hlsl をバイトコードにする」だけに絞る。
    class ShaderCompiler final
    {
    public:
        struct Options final
        {
            // Debug ビルドでは debug_info=true / optimize=false にする。
            // 逆にすると PIX や RenderDoc で行が追えない。
            bool debug_info = false;
            bool optimize = true;

            // 警告をエラーとして扱うか。
            // 既定は false。警告 1 つで絵が出なくなるのは困る。
            bool warnings_as_errors = false;

            // #include の探索先。前から順に見る。
            // ソースと同じフォルダは常に暗黙で先頭に入る。
            std::vector<std::filesystem::path> include_directories;

            std::vector<std::pair<std::string, std::string>> defines;
        };

        // 既定の探索先（Shader/Include, Shader）を入れた Options を返す。
        static Options DefaultOptions(bool debug_build);

        // source を entry_point / target でコンパイルする。
        //
        // target は "vs_5_0" / "ps_5_0" / "gs_5_0" / "cs_5_0"。
        //
        // 成功しても診断（警告）が入ることがある。
        // 失敗時 out_bytecode は触らない。呼び出し側が持っている
        // 直前の成功バイトコードをそのまま使い続けられるようにするため。
        static ShaderCompileResult CompileFile(
            const std::filesystem::path& source,
            const char* entry_point,
            const char* target,
            const Options& options,
            Microsoft::WRL::ComPtr<ID3DBlob>& out_bytecode);

        // 文字列から直接コンパイルする。
        // source_name は診断のファイル名として使うだけ。実在しなくてよい。
        // 自動生成した cbuffer を差し込んだあとのソースを渡す用途。
        static ShaderCompileResult CompileSource(
            const std::string& source_text,
            const std::filesystem::path& source_name,
            const char* entry_point,
            const char* target,
            const Options& options,
            Microsoft::WRL::ComPtr<ID3DBlob>& out_bytecode);

        // コンパイラの生出力を診断の配列へ分解する。
        //
        // 期待する形:
        //   path(42,17-25): error X3004: undeclared identifier 'foo'
        //   path(50,5): warning X3206: implicit truncation
        //
        // 分解できない行も捨てない。message だけ入れて残す。
        // 捨てると「エラーは出ているのに一覧が空」になり、
        // 原因が追えなくなる。
        static std::vector<ShaderDiagnostic> ParseDiagnostics(
            const std::string& raw_output,
            const std::filesystem::path& fallback_file);
    };
}
