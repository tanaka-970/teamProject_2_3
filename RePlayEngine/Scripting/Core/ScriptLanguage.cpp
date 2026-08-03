#include "ScriptLanguage.h"

#include <algorithm>
#include <cctype>

namespace ReplayEngine::Scripting
{
    namespace
    {
        std::string ToLowerAscii(std::string_view text)
        {
            std::string result(text);
            std::transform(result.begin(), result.end(), result.begin(),
                [](unsigned char character)
                {
                    return static_cast<char>(std::tolower(character));
                });
            return result;
        }
    }

    const char* ToString(ScriptLanguage language) noexcept
    {
        switch (language)
        {
        case ScriptLanguage::Lua:    return "Lua";
        case ScriptLanguage::CSharp: return "C#";
        }
        return "Lua";
    }

    bool TryParseScriptLanguage(std::string_view text, ScriptLanguage& out) noexcept
    {
        // noexcept のまま std::string を作るのは危ないので、
        // 短い入力だけを想定した固定長比較にはせず、例外を握り潰す形にする。
        // 言語名は数文字しかないため、確保が失敗する現実的な状況は無い。
        try
        {
            const std::string lowered = ToLowerAscii(text);
            if (lowered == "lua")
            {
                out = ScriptLanguage::Lua;
                return true;
            }
            if (lowered == "c#" || lowered == "csharp" || lowered == "cs")
            {
                out = ScriptLanguage::CSharp;
                return true;
            }
        }
        catch (...)
        {
            return false;
        }
        return false;
    }

    ScriptLanguage ScriptLanguageFromInt(int value) noexcept
    {
        // 範囲外は既定へ倒す。壊れた Scene ファイルで読み込み全体を止めない。
        if (value < 0 || value >= script_language_count) return ScriptLanguage::Lua;
        return static_cast<ScriptLanguage>(value);
    }

    const char* DefaultScriptExtension(ScriptLanguage language) noexcept
    {
        switch (language)
        {
        case ScriptLanguage::Lua:    return ".lua";
        case ScriptLanguage::CSharp: return ".cs";
        }
        return ".lua";
    }

    std::string ScriptCategoryName(ScriptLanguage language)
    {
        // Add Component の階層表示に使う。
        //   Scripts
        //   ├─ C#
        //   └─ Lua
        return std::string("Scripts/") + ToString(language);
    }
}
