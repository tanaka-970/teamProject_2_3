#include "RuntimeIdentity.h"

#include <atomic>

namespace ReplayEngine::Core
{
    WorldInstanceID AcquireWorldInstanceID() noexcept
    {
        // 関数ローカル static なので、翻訳単位をまたぐ静的初期化順序の問題が起きない。
        // ComponentRegistry が静的な表を関数ローカル static で持っているのと同じ方針。
        //
        // atomic にしてあるのは、将来 World の構築をワーカースレッドへ逃がしたときに
        // ここだけが競合点にならないようにするため。現状はメインスレッドからしか呼ばれない。
        static std::atomic<WorldInstanceID> next{ 1 };
        return next.fetch_add(1, std::memory_order_relaxed);
    }
}
