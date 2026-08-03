#include "ObjectID.h"

#include <cerrno>
#include <cstdlib>

namespace ReplayEngine::Core
{
    std::string ObjectID::ToString() const
    {
        return std::to_string(value_);
    }

    bool ObjectID::TryParse(const std::string& text, ObjectID& out) noexcept
    {
        if (text.empty()) return false;

        // 先頭の空白のみ許容し、符号や 16 進表記は受け付けない。
        // Scene ファイルの ID は必ず 10 進の符号なし整数で書き出しているため、
        // それ以外を弾くことで壊れたファイルを静かに読み飛ばさないようにする。
        std::size_t begin = 0;
        while (begin < text.size() && (text[begin] == ' ' || text[begin] == '\t')) ++begin;
        if (begin >= text.size()) return false;
        for (std::size_t i = begin; i < text.size(); ++i)
        {
            if (text[i] < '0' || text[i] > '9') return false;
        }

        errno = 0;
        char* end = nullptr;
        const unsigned long long parsed = std::strtoull(text.c_str() + begin, &end, 10);
        if (errno == ERANGE || end == text.c_str() + begin) return false;

        out = ObjectID{ static_cast<ValueType>(parsed) };
        return true;
    }
}
