#pragma once

#include "../../Core/ObjectID/ObjectID.h"

#include <cstdint>

namespace ReplayEngine::Core
{
    // Trigger の接触 1 件分。
    //
    // 【生ポインタを持たない理由】
    //   イベントはフレーム内で即時に配送するとは限らず、
    //   配送の途中で GameObject や Component が削除されうる。
    //   ポインタを持つと、その瞬間に宙に浮いた参照になる。
    //
    //   ObjectID と ColliderID だけを持てば、
    //   受け取った側が「まだ生きているか」を必ず自分で確かめることになる。
    //   確かめずに使う書き方がそもそもできない形にしてある。
    struct TriggerContact
    {
        // Trigger 側（is_trigger が true の Collider）。
        ObjectID trigger_object;
        std::uint32_t trigger_collider = 0;

        // Trigger へ入った側。
        ObjectID other_object;
        std::uint32_t other_collider = 0;
    };
}
