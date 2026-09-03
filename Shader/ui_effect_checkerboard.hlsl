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

float checker_hash(float2 cell, float seed)
{
    return frac(sin(dot(cell, float2(127.1, 311.7)) + seed) * 43758.5453);
}

float4 main(VSOutput input) : SV_TARGET
{
    float4 color = source_texture.Sample(source_sampler, input.uv);
    const float progress = saturate(effect_params1.y);
    const float softness = max(effect_params1.z, 0.0001);
    const float angle = radians(effect_params1.x);
    const float intensity = saturate(effect_params0.y);
    const float cells = max(floor(effect_params0.x), 1.0);
    const float scatter = saturate(effect_params0.z);
    const float stagger = saturate(effect_params0.w);
    const float seed = effect_params2.z;

    // 縦横比を補正しないとマスが長方形になる。
    const float aspect = max(target_size.x, 1.0) / max(target_size.y, 1.0);
    float2 centered = input.uv - 0.5;
    centered.x *= aspect;
    const float2 rotated = float2(
        centered.x * cos(angle) - centered.y * sin(angle),
        centered.x * sin(angle) + centered.y * cos(angle));
    const float2 grid = (rotated + 0.5) * cells;
    const float2 index = floor(grid);
    const float2 local = frac(grid) - 0.5;

    // 市松の色。片方を先に開かせる。
    const float parity = fmod(abs(index.x + index.y), 2.0);
    const float delay = saturate(parity * stagger
        + checker_hash(index, seed) * scatter);
    const float grow = saturate((progress - delay) / max(1.0 - delay, 0.0001));

    // 正方形のまま広げるのでチェビシェフ距離を使う。
    const float distance = max(abs(local.x), abs(local.y)) * 2.0;
    float coverage = 1.0 - smoothstep(grow - softness, grow + softness, distance);
    // マスの継ぎ目を最後に埋める。
    coverage = max(coverage, smoothstep(0.95, 1.0, progress));
    color.a *= saturate(1.0 - saturate(coverage) * intensity);
    return color;
}
