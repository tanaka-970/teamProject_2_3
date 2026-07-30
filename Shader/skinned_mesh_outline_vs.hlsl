// スキンメッシュを法線方向へ膨らませて輪郭を作る頂点シェーダー。
#include "skinned_mesh.hlsli"

cbuffer OUTLINE_CONSTANT_BUFFER : register(b7)
{
    float4 outline_color;  // rgb=color a=未使用
    float4 outline_params; // x=width(world), y=screen_corrected_width, z/w=未使用
};

VS_OUT main(VS_IN vin)
{
    VS_OUT vout;

    float4 blended_pos    = (float4) 0;
    float4 blended_normal = (float4) 0;
    float4 blended_tangent = (float4) 0;
    for (int i = 0; i < 4; ++i)
    {
        uint  bi = vin.bone_indices[i];
        float bw = vin.bone_weights[i];
        blended_pos     += bw * mul(vin.position, bone_transforms[bi]);
        blended_normal  += bw * mul(float4(vin.normal.xyz,  0), bone_transforms[bi]);
        blended_tangent += bw * mul(float4(vin.tangent.xyz, 0), bone_transforms[bi]);
    }

    float4 wp = mul(blended_pos, world);
    float3 wn = normalize(mul(blended_normal, world).xyz);

    // 距離による線幅補正
    float dist = distance(camera_position.xyz, wp.xyz);
    float width = outline_params.x + outline_params.y * dist * 0.01f;

    wp.xyz += wn * width;

    vout.position       = mul(wp, view_projection);
    vout.world_position = wp;
    vout.world_normal   = float4(wn, 0);
    vout.world_tangent  = blended_tangent;
    vout.texcoord       = vin.texcoord;
    vout.color          = outline_color;
    // 輪郭パスはモーションベクターを書かないので、動きゼロとして埋める。
    vout.current_clip   = vout.position;
    vout.previous_clip  = vout.position;
    return vout;
}
