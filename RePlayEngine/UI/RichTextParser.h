#pragma once

#include <DirectXMath.h>

#include <cstdint>
#include <string>
#include <vector>

namespace ReplayEngine::UI
{
    struct RichTextStyle final
    {
        DirectX::XMFLOAT4 color{ 1.0f, 1.0f, 1.0f, 1.0f };
        float font_size = 24.0f;
        bool bold = false;
        bool italic = false;
    };

    struct RichTextCharacter final
    {
        std::uint32_t codepoint = 0;
        RichTextStyle style{};
        int character_index = 0;
    };

    struct RichTextResult final
    {
        std::string plain_text;
        std::vector<RichTextCharacter> characters;
        int display_character_count = 0;
        bool markup_valid = true;
    };

    // Unity 互換の最小 Rich Text パーサ。
    // 対応: <color=#RRGGBB[AA]>, <size=N>, <b>, <i> と対応する閉じタグ。
    // 不正タグや閉じ忘れを検出した文字列は、誤りを隠さないよう全体を平文として返す。
    class RichTextParser final
    {
    public:
        static RichTextResult Parse(const std::string& text, bool enabled,
            float default_font_size);
    };
}
