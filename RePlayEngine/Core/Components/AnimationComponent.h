#pragma once

#include "IComponent.h"

namespace ReplayEngine::Core
{
    class AnimationComponent final : public IComponent
    {
    public:
        int clip_index = 0;
        float speed = 1.0f;
        bool loop = true;
        bool playing = true;
    };
}
