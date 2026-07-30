#pragma once

#include "../ShaderStack/ShaderLayerStack.h"
#include "../Materials/CharacterMaterialProfile.h"

#include <filesystem>
#include <string>

namespace ReplayEngine::Rendering
{
    struct ShaderPreset
    {
        std::string name{ "Shader Preset" };
        int base_shader = 1;
        bool outline = false;
        ShaderLayerStack layers;
        CharacterMaterialProfile character;
    };

    class ShaderPresetSerializer final
    {
    public:
        static bool Save(const ShaderPreset& preset,
            const std::filesystem::path& path, std::string& error);
        static bool Load(ShaderPreset& preset,
            const std::filesystem::path& path, std::string& error);
    };
}
