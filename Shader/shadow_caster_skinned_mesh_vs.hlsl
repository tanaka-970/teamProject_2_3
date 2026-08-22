// スキンメッシュをPBRシャドウマップ用座標へ変換する頂点シェーダー。
#include "skinned_mesh.hlsli"
#include "pbr_common.hlsli"

struct SHADOW_VS_OUT
{
    float4 position : SV_POSITION;
    // アルファ抜き材質の影のために UV を影パスへも運ぶ。
    float2 texcoord : TEXCOORD;
};

SHADOW_VS_OUT main(VS_IN vin)
{
    SHADOW_VS_OUT vout;
    vin.position.xyz += vin.morph_position * gltf_morph.x;

    // スキニング (既存 skinned_mesh_vs と同じ)
    float4 blended_position = float4(0, 0, 0, 1);
    [unroll]
    for (int i = 0; i < 4; ++i)
    {
        blended_position += vin.bone_weights[i] *
            mul(vin.position, bone_transforms[vin.bone_indices[i]]);
    }
    blended_position.w = 1.0f;

    float4 world_position = mul(blended_position, world);
    vout.position = mul(world_position, light_view_projection);
    vout.texcoord = vin.texcoord;
    return vout;
}
