// 静的メッシュを法線方向へ膨らませて輪郭を作る頂点シェーダー。
#include "static_mesh.hlsli"

cbuffer OUTLINE_CONSTANT_BUFFER : register(b7)
{
    float4 outline_color;
    float4 outline_params;
};

struct VS_IN
{
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float2 texcoord : TEXCOORD;
};

VS_OUT main(VS_IN vin)
{
    VS_OUT vout = (VS_OUT) 0;
    float4 wp = mul(float4(vin.position, 1.0f), world);
    float3 wn = normalize(mul(float4(vin.normal, 0.0f), world).xyz);

    float dist = distance(camera_position.xyz, wp.xyz);
    float width = outline_params.x + outline_params.y * dist * 0.01f;
    wp.xyz += wn * width;

    vout.position       = mul(wp, view_projection);
    vout.world_position = wp;
    vout.world_normal   = float4(wn, 0.0f);
    vout.texcoord       = vin.texcoord;
    vout.color          = outline_color;
    // 輪郭パスはモーションベクターを書かないので、動きゼロとして埋める。
    vout.current_clip   = vout.position;
    vout.previous_clip  = vout.position;
    return vout;
}
