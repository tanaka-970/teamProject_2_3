// 照明を使わずスキンメッシュの色と画像を出力するピクセルシェーダー。
#include "skinned_mesh.hlsli"

Texture2D    diffuse_map : register(t0);
SamplerState sampler_lin : register(s1);

float4 main(VS_OUT pin) : SV_TARGET
{
    float4 base = diffuse_map.Sample(sampler_lin, pin.texcoord);
    base.rgb = max(base.rgb, float3(0.18f, 0.18f, 0.18f));
    float3 tint = max(pin.color.rgb, float3(0.75f, 0.75f, 0.75f));
    return float4(base.rgb * tint, base.a * pin.color.a);
}
