// 照明を使わず静的メッシュの色と画像を出力するピクセルシェーダー。
#include "static_mesh.hlsli"

Texture2D    diffuse_map : register(t0);
SamplerState sampler_lin : register(s1);

float4 main(VS_OUT pin) : SV_TARGET
{
    float4 base = diffuse_map.Sample(sampler_lin, pin.texcoord);
    return base * pin.color;
}
