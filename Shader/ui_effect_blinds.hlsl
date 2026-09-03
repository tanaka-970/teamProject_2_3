Texture2D source_texture : register(t0);
SamplerState source_sampler : register(s0);

cbuffer UIEffectConstants : register(b0)
{
    float4 effect_color;
    float4 effect_params0; // radius, intensity, threshold, amount
    float4 effect_params1; // angle, progress, softness, speed
    float4 effect_params2; // direction.xy, seed, time
    float4 target_size;    // width, height, 1 / width, 1 / height
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

float4 main(VSOutput input) : SV_TARGET
{
    float4 color = source_texture.Sample(source_sampler, input.uv);
    const float progress = saturate(effect_params1.y);
    const float softness = max(effect_params1.z, 0.0001);
    const float angle = radians(effect_params1.x);
    const float intensity = saturate(effect_params0.y);
    const float slats = max(floor(effect_params0.x), 1.0);
    // 0 で片側から、1 で各スリットの中央から開く。
    const float from_center = saturate(effect_params0.w);

    const float2 axis = float2(cos(angle), sin(angle));
    const float projected = dot(input.uv - 0.5, axis) + 0.5;
    // 1 枚ぶんの中での位置。
    const float cell = frac(projected * slats);
    const float edge = lerp(cell, abs(cell - 0.5) * 2.0, from_center);

    // progress 0 で edge > 0 の全画素が残り、1 で全画素が覆われる。
    const float coverage =
        1.0 - smoothstep(progress - softness, progress + softness, edge);
    color.a *= saturate(1.0 - saturate(coverage) * intensity);
    return color;
}
