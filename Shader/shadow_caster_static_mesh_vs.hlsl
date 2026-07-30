// 静的メッシュをPBRシャドウマップ用座標へ変換する頂点シェーダー。
#include "static_mesh.hlsli"   // world (b0) と light_direction (b1)
#include "pbr_common.hlsli"    // light_view_projection (b3)

struct SHADOW_VS_OUT
{
    float4 position : SV_POSITION;
};

SHADOW_VS_OUT main(float4 position : POSITION, float4 normal : NORMAL, float2 texcoord : TEXCOORD)
{
    SHADOW_VS_OUT vout;
    float4 world_position = mul(position, world);
    vout.position = mul(world_position, light_view_projection);
    return vout;
}
