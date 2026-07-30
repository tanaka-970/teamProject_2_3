// マスク値で不要部分を除外してスプライトを描くピクセルシェーダー。
#include "sprite.hlsli"

Texture2D color_map : register(t0);
SamplerState point_sampler_state : register(s0);
SamplerState linear_sampler_state : register(s1);

float4 main(VS_OUT input) : SV_TARGET
{
    const float4 sample_color = color_map.Sample(linear_sampler_state, input.texcoord);

    // ロゴPNG外周の極低アルファ画素を捨て、短いアンチエイリアス境界を作り直す。
    // 加算合成時に薄い残像が蓄積することを防ぐ。
    const float coverage = smoothstep(0.20f, 0.45f, sample_color.a);
    clip(coverage - 0.001f);

    return float4(sample_color.rgb * input.color.rgb, coverage * input.color.a);
}
