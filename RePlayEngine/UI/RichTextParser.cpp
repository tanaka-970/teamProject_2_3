#include "RichTextParser.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <string_view>
#include <utility>

namespace ReplayEngine::UI
{
    namespace
    {
        enum class TagKind { Color, Size, Bold, Italic };
        struct Token final
        {
            bool tag = false;
            bool closing = false;
            TagKind kind = TagKind::Bold;
            std::string text;
            DirectX::XMFLOAT4 color{ 1, 1, 1, 1 };
            float size = 0.0f;
        };

        std::uint32_t DecodeUtf8(std::string_view text, std::size_t& offset) noexcept
        {
            if (offset >= text.size()) return 0;
            const unsigned char c0 = static_cast<unsigned char>(text[offset++]);
            if (c0 < 0x80) return c0;
            int count = 0;
            std::uint32_t value = 0;
            if ((c0 & 0xE0) == 0xC0) { count = 1; value = c0 & 0x1F; }
            else if ((c0 & 0xF0) == 0xE0) { count = 2; value = c0 & 0x0F; }
            else if ((c0 & 0xF8) == 0xF0) { count = 3; value = c0 & 0x07; }
            else return 0xFFFDu;
            for (int i = 0; i < count; ++i)
            {
                if (offset >= text.size()) return 0xFFFDu;
                const unsigned char cx = static_cast<unsigned char>(text[offset]);
                if ((cx & 0xC0) != 0x80) return 0xFFFDu;
                ++offset;
                value = (value << 6) | (cx & 0x3F);
            }
            if (value > 0x10FFFFu || (value >= 0xD800u && value <= 0xDFFFu)) return 0xFFFDu;
            return value;
        }

        bool HexNibble(char c, unsigned& out) noexcept
        {
            if (c >= '0' && c <= '9') { out = static_cast<unsigned>(c - '0'); return true; }
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (c >= 'a' && c <= 'f') { out = 10u + static_cast<unsigned>(c - 'a'); return true; }
            return false;
        }

        bool ParseHexColor(std::string_view value, DirectX::XMFLOAT4& color) noexcept
        {
            if (value.empty()) return false;
            if (value.front() != '#')
            {
                const std::pair<std::string_view, DirectX::XMFLOAT4> named[] = {
                    {"white",{1,1,1,1}}, {"black",{0,0,0,1}}, {"red",{1,0,0,1}},
                    {"green",{0,1,0,1}}, {"blue",{0,0,1,1}}, {"yellow",{1,1,0,1}},
                    {"cyan",{0,1,1,1}}, {"magenta",{1,0,1,1}}, {"gray",{0.5f,0.5f,0.5f,1}},
                    {"grey",{0.5f,0.5f,0.5f,1}}, {"orange",{1,0.5f,0,1}}, {"clear",{0,0,0,0}}
                };
                std::string lower(value);
                std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
                for (const auto& entry : named) if (entry.first == lower) { color = entry.second; return true; }
                return false;
            }
            value.remove_prefix(1);
            if (value.size() != 6 && value.size() != 8) return false;
            std::array<unsigned,8> n{};
            for (std::size_t i=0;i<value.size();++i) if (!HexNibble(value[i],n[i])) return false;
            const auto byte_at=[&](std::size_t i){ return static_cast<float>((n[i]<<4)|n[i+1]) / 255.0f; };
            color = { byte_at(0), byte_at(2), byte_at(4), value.size()==8 ? byte_at(6) : 1.0f };
            return true;
        }

        bool ParseTag(std::string_view body, Token& out)
        {
            if (body == "b") { out.tag=true; out.kind=TagKind::Bold; return true; }
            if (body == "i") { out.tag=true; out.kind=TagKind::Italic; return true; }
            if (body == "/b") { out.tag=true; out.closing=true; out.kind=TagKind::Bold; return true; }
            if (body == "/i") { out.tag=true; out.closing=true; out.kind=TagKind::Italic; return true; }
            if (body == "/color") { out.tag=true; out.closing=true; out.kind=TagKind::Color; return true; }
            if (body == "/size") { out.tag=true; out.closing=true; out.kind=TagKind::Size; return true; }
            if (body.rfind("color=",0)==0)
            {
                DirectX::XMFLOAT4 color{};
                if (!ParseHexColor(body.substr(6), color)) return false;
                out.tag=true; out.kind=TagKind::Color; out.color=color; return true;
            }
            if (body.rfind("size=",0)==0)
            {
                const std::string value(body.substr(5));
                char* end=nullptr; const float parsed=std::strtof(value.c_str(), &end);
                if (end==value.c_str() || *end!='\0' || !std::isfinite(parsed) || parsed < 1.0f || parsed > 2048.0f) return false;
                out.tag=true; out.kind=TagKind::Size; out.size=parsed; return true;
            }
            return false;
        }

        RichTextResult Plain(std::string_view text, float font_size)
        {
            RichTextResult result; result.plain_text.assign(text); result.markup_valid=false;
            std::size_t offset=0; int index=0;
            while (offset < text.size())
            {
                RichTextCharacter ch; ch.codepoint=DecodeUtf8(text, offset); ch.character_index=index++; ch.style.font_size=font_size;
                result.characters.push_back(ch);
            }
            result.display_character_count=index;
            return result;
        }
    }

    RichTextResult RichTextParser::Parse(const std::string& text, bool enabled,
        float default_font_size)
    {
        default_font_size = (std::max)(1.0f, default_font_size);
        if (!enabled) return Plain(text, default_font_size);

        std::vector<Token> tokens;
        std::vector<TagKind> validation_stack;
        std::size_t cursor=0, literal_start=0;
        bool malformed=false;
        while (cursor < text.size())
        {
            if (text[cursor] != '<') { ++cursor; continue; }
            const std::size_t close=text.find('>', cursor+1);
            if (close==std::string::npos) { malformed=true; break; }
            Token tag;
            if (!ParseTag(std::string_view(text).substr(cursor+1,close-cursor-1),tag))
            {
                // 未知/不正タグはタグではなく平文。後ろに正しいタグがあっても解析は続ける。
                cursor=close+1; continue;
            }
            if (cursor>literal_start) { Token t; t.text=text.substr(literal_start,cursor-literal_start); tokens.push_back(std::move(t)); }
            tokens.push_back(tag);
            if (!tag.closing) validation_stack.push_back(tag.kind);
            else if (validation_stack.empty() || validation_stack.back()!=tag.kind) { malformed=true; break; }
            else validation_stack.pop_back();
            cursor=close+1; literal_start=cursor;
        }
        if (!malformed && literal_start<text.size()) { Token t; t.text=text.substr(literal_start); tokens.push_back(std::move(t)); }
        if (malformed || !validation_stack.empty()) return Plain(text, default_font_size);

        RichTextResult result; result.markup_valid=true;
        RichTextStyle style; style.font_size=default_font_size;
        struct Frame { TagKind kind; RichTextStyle previous; };
        std::vector<Frame> stack;
        int index=0;
        for (const Token& token : tokens)
        {
            if (token.tag)
            {
                if (!token.closing)
                {
                    stack.push_back({token.kind,style});
                    switch(token.kind)
                    {
                    case TagKind::Color: style.color=token.color; break;
                    case TagKind::Size: style.font_size=token.size; break;
                    case TagKind::Bold: style.bold=true; break;
                    case TagKind::Italic: style.italic=true; break;
                    }
                }
                else { style=stack.back().previous; stack.pop_back(); }
                continue;
            }
            result.plain_text += token.text;
            std::size_t offset=0;
            while(offset<token.text.size())
            {
                RichTextCharacter ch; ch.codepoint=DecodeUtf8(token.text,offset); ch.style=style; ch.character_index=index++;
                result.characters.push_back(ch);
            }
        }
        result.display_character_count=index;
        return result;
    }
}
