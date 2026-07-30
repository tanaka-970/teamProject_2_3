#pragma once

#include "../../Rendering/Materials/CharacterMaterialProfile.h"

namespace ReplayEngine::Editor
{
    class CharacterMaterialEditor final
    {
    public:
        static void Draw(Rendering::CharacterMaterialProfile& profile);
    };
}
