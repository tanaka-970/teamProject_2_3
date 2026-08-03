#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace ReplayEngine::Scripting
{
    // スクリプトの記述言語。
    //
    // ここに列挙するのは「実行 Backend の種類」であって、
    // GameObject が持つ Component の型ではない。
    // Lua でも C# でも、GameObject へ付くのは常に 1 つの ScriptComponent。
    //
    // 【追加時の約束】
    //   末尾へ足すこと。Scene ファイルへは int として書き出されるため、
    //   途中へ挿入すると保存済みの Scene の言語が別のものへ化ける。
    enum class ScriptLanguage : std::int32_t
    {
        Lua = 0,
        CSharp = 1,
    };

    inline constexpr int script_language_count = 2;

    // Inspector の Enum ラベルと、診断表示に使う名前。
    //
    // PropertyType::Enum の内部表現は int なので、保存されるのは数値。
    // ここで返す文字列は表示とログのためだけに使う。
    const char* ToString(ScriptLanguage language) noexcept;

    // 文字列からの復元。解析できない場合は false を返し out は変更しない。
    // 大文字小文字は区別しない。"C#" も "CSharp" も受け付ける。
    bool TryParseScriptLanguage(std::string_view text, ScriptLanguage& out) noexcept;

    // 保存済みの int から復元する。範囲外なら Lua へ倒す。
    //
    // 例外を投げず既定へ倒すのは PropertyValue と同じ方針。
    // 壊れた Scene ファイルを読んでも読み込み全体を止めない。
    ScriptLanguage ScriptLanguageFromInt(int value) noexcept;

    // スクリプトアセットの標準拡張子（先頭のドットを含む）。
    const char* DefaultScriptExtension(ScriptLanguage language) noexcept;

    // Add Component の一覧で使うカテゴリ名（"Scripts/Lua" など）。
    // Phase 6 の ScriptTypeCatalog 合流で使う。
    std::string ScriptCategoryName(ScriptLanguage language);
}
