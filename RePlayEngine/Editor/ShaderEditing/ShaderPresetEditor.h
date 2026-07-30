#pragma once

#include "../../Rendering/Materials/CharacterMaterialProfile.h"
#include "../../Rendering/ShaderStack/ShaderLayerStack.h"

#include <Windows.h>

#include <string>

namespace ReplayEngine::Editor
{
    class ShaderPresetEditor final
    {
    public:
        static void Draw(HWND owner, int& base_shader, bool& outline_pass,
            Rendering::ShaderLayerStack& layers,
            Rendering::CharacterMaterialProfile& profile, std::string& status);
    };
}
