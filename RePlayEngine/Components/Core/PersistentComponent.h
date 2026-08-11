#pragma once

#include "../../Object/Component/Component.h"

namespace ReplayEngine::Components
{
    // Scene 遷移をまたいで同じ Runtime World に残すルート印。
    // Scene 直下に付いた場合だけ有効で、子 GameObject の印は警告して無視する。
    // 所有権の移動は Scene が行い、この Component 自身は状態を持たない。
    class PersistentComponent final : public Core::Component
    {
        REPLAY_COMPONENT_BODY(PersistentComponent)
    };
}
