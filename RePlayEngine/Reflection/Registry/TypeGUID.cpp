#include "TypeGUID.h"

namespace ReplayEngine::Reflection
{
    namespace
    {
        void AppendHex64(std::string& text, std::uint64_t value)
        {
            constexpr char digits[] = "0123456789abcdef";
            for (int shift = 60; shift >= 0; shift -= 4)
            {
                text.push_back(digits[(value >> shift) & 0xFull]);
            }
        }
    }

    std::string TypeGUID::ToString() const
    {
        std::string text;
        text.reserve(32);
        AppendHex64(text, high);
        AppendHex64(text, low);
        return text;
    }

    bool TypeGUID::TryParse(std::string_view text, TypeGUID& out) noexcept
    {
        // ハイフン入りの表記 (8-4-4-4-12) でも受け付ける。
        // Editor でユーザーが貼り付けた文字列をそのまま通せるようにするため。
        std::uint64_t high = 0;
        std::uint64_t low = 0;
        std::size_t digits = 0;

        for (const char character : text)
        {
            if (character == '-' || character == '{' || character == '}') continue;

            const std::uint8_t value = Detail::HexDigitValue(character);
            if (value == 0xFFu) return false;

            if (digits >= 32) return false;
            if (digits < 16) high = (high << 4) | value;
            else             low = (low << 4) | value;
            ++digits;
        }

        if (digits != 32) return false;

        out.high = high;
        out.low = low;
        return true;
    }
}
