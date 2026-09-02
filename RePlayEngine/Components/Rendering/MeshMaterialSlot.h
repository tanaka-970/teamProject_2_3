#pragma once

#include <string>

namespace ReplayEngine::Components
{
    inline constexpr int max_mesh_material_slots = 16;

    // Renderer 共通のサブセット名と Material Asset 参照を保持する。
    struct MeshMaterialSlot final
    {
        std::string name;
        std::string asset;
    };
}
