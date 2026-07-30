#pragma once//

#include "ShaderPresetSerializer.h"

namespace ReplayEngine::Rendering
{
    class BuiltInShaderPresets final
    {
    public:
        static ShaderPreset WutheringStylized();
        static ShaderPreset EndfieldLayered();
        static ShaderPreset CrystalToon();
        static ShaderPreset SoftAnime();
        static ShaderPreset GraphicCel();
        static ShaderPreset MoonlitCrystal();
    };
}
