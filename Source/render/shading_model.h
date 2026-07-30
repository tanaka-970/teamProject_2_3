#pragma once
#include <cstdint>

enum class shading_model : uint32_t
{
    fbx_default = 0,   // skinned/static_mesh 標準PS (Lambert+テクスチャ)
    pbr         = 1,   // Cook-Torrance / GGX / IBL
    toon        = 2,   // 3階調ランプ + リム + 異方性ハイライト
    unlit       = 3,   // ライティング無し、ベースカラーのみ
    pixelate    = 4,   // モデル色を画面上の四角いセル単位で低解像度化
};

// HLSL側にもこの定義をミラーするためのマクロ
#define SHADING_MODEL_FBX_DEFAULT 0
#define SHADING_MODEL_PBR         1
#define SHADING_MODEL_TOON        2
#define SHADING_MODEL_UNLIT       3
#define SHADING_MODEL_PIXELATE    4
