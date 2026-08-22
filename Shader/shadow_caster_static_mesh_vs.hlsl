// 静的メッシュをPBRシャドウマップ用座標へ変換する頂点シェーダー。
#include "static_mesh.hlsli"   // world (b0) と light_direction (b1)
#include "pbr_common.hlsli"    // light_view_projection (b3)

struct SHADOW_VS_OUT
{
    float4 position : SV_POSITION;
    // アルファ抜き材質の影のために UV を影パスへも運ぶ。
    float2 texcoord : TEXCOORD;
    // Model Effect Stack の面消しを影でも評価するためワールド座標を運ぶ。
    float3 world_position : TEXCOORD1;
};

SHADOW_VS_OUT main(float4 position : POSITION, float4 normal : NORMAL, float2 texcoord : TEXCOORD)
{
    SHADOW_VS_OUT vout;
    float4 world_position = mul(position, world);
    vout.position = mul(world_position, light_view_projection);
    vout.texcoord = texcoord;
    vout.world_position = world_position.xyz;
    return vout;
}
