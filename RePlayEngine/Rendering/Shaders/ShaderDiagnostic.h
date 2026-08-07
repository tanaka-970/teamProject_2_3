#pragma once

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

namespace ReplayEngine::Rendering
{
    // シェーダのコンパイル診断 1 件。
    //
    // 【CSharpDiagnostic と同じ形にしてある】
    //   C# 側は既に「Console のエラー行をクリックすると
    //   Visual Studio の該当行が開く」経路が動いている
    //   （framework_editor.cpp の editor_log_entry と
    //     CSharpProject::OpenVisualStudio）。
    //   構造を揃えておけば、表示とジャンプをそのまま再利用できる。
    //   ここで独自形式を作らないこと。
    struct ShaderDiagnostic final
    {
        enum class Severity
        {
            Info,
            Warning,
            Error,
        };

        Severity severity = Severity::Error;

        // "X3004" のようなコンパイラのコード。取れなければ空。
        std::string code;

        std::string message;

        std::filesystem::path file;

        // 1 始まり。取れなければ 0。
        int line = 0;
        int column = 0;
    };

    const char* ToString(ShaderDiagnostic::Severity severity) noexcept;

    // 1 回のコンパイル結果。
    //
    // 【失敗しても捨てないもの】
    //   diagnostics は成功時にも入る（警告）。
    //   raw_output はコンパイラの生の出力。分解に失敗したときの
    //   最後の手掛かりになるので必ず残すこと。
    //   「理由が分からない」状態を作らないための保険。
    struct ShaderCompileResult final
    {
        bool succeeded = false;

        std::vector<ShaderDiagnostic> diagnostics;

        // 分解前の生文字列。診断が 0 件でも失敗することがあるため残す。
        std::string raw_output;

        std::chrono::milliseconds duration{ 0 };

        // 失敗した診断だけを数える。警告は含めない。
        std::size_t ErrorCount() const noexcept;

        // 最初のエラー。無ければ nullptr。
        const ShaderDiagnostic* FirstError() const noexcept;

        // 1 行にまとめた要約。ログへ出す用。
        std::string Summary() const;
    };
}
