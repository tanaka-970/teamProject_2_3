#pragma once

#include <string>

namespace ReplayEngine::Components
{
    inline constexpr int max_mesh_material_slots = 32;

    // Renderer 共通のサブセット名と Material Asset 参照を保持する。
    struct MeshMaterialSlot final
    {
        std::string name;
        std::string asset;
        // 空ならモデルが持つテクスチャをそのまま使う。
        std::string base_color_texture;
        std::string normal_texture;
        std::string orm_texture;
        std::string emissive_texture;
    };
}
