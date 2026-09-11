#pragma once

#include "../../Object/Component/Component.h"

namespace ReplayEngine::Components
{
    // GameObject をまとめる入れ物を示す、エディター専用の印。
    class FolderComponent final : public Core::Component
    {
        REPLAY_COMPONENT_BODY(FolderComponent)
    };
}
