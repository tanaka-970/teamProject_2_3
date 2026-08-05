#include "BuiltInShaders.h"

namespace ReplayEngine::Rendering::BuiltInShaders
{
    const std::vector<Definition>& All()
    {
        static const std::vector<Definition> definitions = {
            { FbxDefault, 0, "標準",     "Materials/BuiltIn/FbxDefault.hlsl" },
            { Pbr,        1, "PBR",      "Materials/BuiltIn/Pbr.hlsl" },
            { Toon,       2, "Toon",     "Materials/BuiltIn/Toon.hlsl" },
            { Unlit,      3, "Unlit",    "Materials/BuiltIn/Unlit.hlsl" },
            { Pixelate,   4, "Pixelate", "Materials/BuiltIn/Pixelate.hlsl" },
        };
        return definitions;
    }

    ShaderID FromShadingModel(int shading_model) noexcept
    {
        for (const Definition& definition : All())
        {
            if (definition.shading_model == shading_model) return definition.id;
        }
        // 知らない番号は無効値。
        //
        // ここで PBR などへ丸めたくなるが、丸めてはいけない。
        // 丸めると「保存されていた選択と違うものが黙って付く」ことになり、
        // しかも画面には何も出ないので誰も気付けない。
        // 呼び出し側が「不明」として扱い、理由を出すこと。
        return ShaderID{};
    }

    bool IsBuiltIn(ShaderID id) noexcept
    {
        for (const Definition& definition : All())
        {
            if (definition.id == id) return true;
        }
        return false;
    }

    std::filesystem::path RelativePath(ShaderID id)
    {
        for (const Definition& definition : All())
        {
            if (definition.id == id)
            {
                return std::filesystem::path(definition.relative_path);
            }
        }
        return {};
    }
}
