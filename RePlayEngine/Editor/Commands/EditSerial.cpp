#include "EditSerial.h"

namespace ReplayEngine::Editor
{
    std::uint64_t NextEditSerial() noexcept
    {
        // 翻訳単位をまたぐ静的初期化順序に依存しないよう、関数内で保持する。
        static std::uint64_t serial = 0;
        return ++serial;
    }
}
