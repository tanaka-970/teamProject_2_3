#include "ScriptValue.h"

#include <cctype>

namespace ReplayEngine::Scripting
{
    namespace
    {
        bool StartsWith(std::string_view text, std::string_view prefix) noexcept
        {
            return text.size() >= prefix.size() &&
                text.compare(0, prefix.size(), prefix) == 0;
        }

        bool IsUpperAscii(char character) noexcept
        {
            return character >= 'A' && character <= 'Z';
        }

        bool IsLowerAscii(char character) noexcept
        {
            return character >= 'a' && character <= 'z';
        }

        bool IsDigitAscii(char character) noexcept
        {
            return character >= '0' && character <= '9';
        }
    }

    namespace ScriptNames
    {
        std::string MakeFieldSavedName(std::string_view field_name)
        {
            std::string result(field_prefix);
            result.append(field_name);
            return result;
        }

        bool IsFieldSavedName(std::string_view saved_name) noexcept
        {
            return StartsWith(saved_name, field_prefix);
        }

        std::string_view StripFieldPrefix(std::string_view saved_name) noexcept
        {
            if (!IsFieldSavedName(saved_name)) return saved_name;
            return saved_name.substr(std::string_view(field_prefix).size());
        }

        bool IsInternalSavedName(std::string_view saved_name) noexcept
        {
            return StartsWith(saved_name, internal_prefix);
        }
    }

    std::string HumanizeFieldName(std::string_view field_name)
    {
        std::string result;
        result.reserve(field_name.size() + 4);

        bool pending_boundary = false;

        for (std::size_t index = 0; index < field_name.size(); ++index)
        {
            const char character = field_name[index];

            // 区切り文字は空白 1 つへ畳む。連続しても空白は増えない。
            if (character == '_' || character == '-' || character == ' ')
            {
                if (!result.empty()) pending_boundary = true;
                continue;
            }

            if (!result.empty())
            {
                const char previous = field_name[index - 1];

                // 小文字 -> 大文字 の境目で割る（"maxHP" -> "max HP"）。
                if (IsUpperAscii(character) && (IsLowerAscii(previous) || IsDigitAscii(previous)))
                {
                    pending_boundary = true;
                }
                // 大文字が続いたあと小文字が来たら、その 1 つ手前で割る
                // （"HTMLParser" -> "HTML Parser"）。
                else if (IsUpperAscii(character) && IsUpperAscii(previous) &&
                    index + 1 < field_name.size() && IsLowerAscii(field_name[index + 1]))
                {
                    pending_boundary = true;
                }
                // 文字 -> 数字 の境目で割る（"slot2" -> "Slot 2"）。
                else if (IsDigitAscii(character) && !IsDigitAscii(previous))
                {
                    pending_boundary = true;
                }
            }

            const bool word_start = result.empty() || pending_boundary;

            if (pending_boundary)
            {
                result.push_back(' ');
                pending_boundary = false;
            }

            // 単語の先頭だけ大文字へ寄せる。それ以外は元の字面を保つ
            // （"HP" を "Hp" にしてしまわないため）。
            //
            // 区切り文字の直後も単語の先頭として扱う。
            // ここを先頭 1 文字だけにすると "target_object" が
            // "Target object" になる。
            if (word_start && IsLowerAscii(character))
            {
                result.push_back(static_cast<char>(std::toupper(
                    static_cast<unsigned char>(character))));
                continue;
            }

            result.push_back(character);
        }

        // 全部が区切り文字だった場合など、何も残らなければ元の名前を返す。
        if (result.empty()) return std::string(field_name);
        return result;
    }
}
