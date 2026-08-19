Texture2D source_texture : register(t0);
SamplerState source_sampler : register(s0);

cbuffer UIEffectConstants : register(b0)
{
    float4 effect_color;
    float4 effect_params0; // spacing, darkness, threshold, amount
    float4 effect_params1; // angle, progress, softness, speed
    float4 effect_params2; // direction.xy, seed, time
    float4 target_size;
    float4 effect_color_2;
    float4 effect_color_3;
    float4 effect_color_4;
    float4 effect_color_stops;
};

struct VSOutput { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };
static const float two_pi = 6.28318530718;

float4 main(VSOutput input) : SV_TARGET
{
    float4 source = source_texture.Sample(source_sampler, input.uv);
    const float spacing = max(effect_params0.x, 1.0);
    const float row = input.uv.y * target_size.y + effect_params2.w * effect_params1.w;
    const float wave = 0.5 + 0.5 * cos(row / spacing * two_pi);
    // line は HLSL の予約語（ジオメトリシェーダーのプリミティブ型）なので使えない。
    const float scanline_mask = smoothstep(0.45, 0.75, wave) * saturate(effect_params0.y);
    source.rgb = lerp(source.rgb, effect_color.rgb, scanline_mask * effect_color.a);
    return source;
}
