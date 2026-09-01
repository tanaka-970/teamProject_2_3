// スキンメッシュをCSMの深度描画用座標へ変換する頂点シェーダー。
#include "skinned_mesh.hlsli"

struct GS_IN
{
    float4 world_position : POSITION;
    // アルファ抜き材質の影のために UV を影パスへも運ぶ。
    float2 texcoord : TEXCOORD;
};

GS_IN main(VS_IN vin)
{
    vin.position.xyz += vin.morph_position * gltf_morph.x;
    float4 blended = (float4) 0;
    for (int i = 0; i < 4; ++i)
    {
        blended += vin.bone_weights[i] *
                   mul(vin.position, bone_transforms[vin.bone_indices[i]]);
    }
    GS_IN o;
    o.world_position = mul(blended, world);
    o.texcoord = vin.texcoord;
    return o;
}
