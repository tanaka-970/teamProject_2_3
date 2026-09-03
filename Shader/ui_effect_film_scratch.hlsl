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

float fx_hash(float2 v, float seed)
{
    return frac(sin(dot(v, float2(127.1, 311.7)) + seed) * 43758.5453);
}

float fx_luma(float3 c)
{
    return dot(c, float3(0.2126, 0.7152, 0.0722));
}

float4 main(VSOutput input) : SV_TARGET
{
    float4 color = source_texture.Sample(source_sampler, input.uv);
    const float scratches = max(floor(effect_params0.x), 0.0);
    const float intensity = saturate(effect_params0.y);
    const float dust = saturate(effect_params0.z);
    const float thickness = max(effect_params0.w, 0.0001);
    const float softness = saturate(effect_params1.z);
    const float speed = max(effect_params1.w, 0.0);
    const float seed = effect_params2.z;
    const float time = effect_params2.w;

    // コマ送り。速度 0 なら 1 枚に固定される。
    const float frame = floor(time * speed * 24.0);
    float mark = 0.0;

    // 縦の傷。コマごとに位置と長さが変わる。
    for (float s = 0.0; s < scratches; s += 1.0)
    {
        const float2 key = float2(s, frame);
        const float x = fx_hash(key, seed);
        const float top = fx_hash(key, seed + 7.0);
        const float span = 0.2 + fx_hash(key, seed + 13.0) * 0.8;
        if (input.uv.y < top || input.uv.y > top + span) continue;
        const float d = abs(input.uv.x - x);
        mark = max(mark, 1.0 - smoothstep(thickness * (1.0 - softness),
            thickness, d));
    }

    // ホコリ。細かい格子へまばらに置く。
    const float2 grain = floor(input.uv * 160.0);
    const float speck = fx_hash(grain, seed + frame * 0.37);
    if (speck > 1.0 - dust * 0.06)
    {
        const float2 local = frac(input.uv * 160.0) - 0.5;
        mark = max(mark, 1.0 - smoothstep(0.15, 0.45, length(local)));
    }

    const float coverage = saturate(mark) * intensity;
    color.rgb = lerp(color.rgb, effect_color.rgb, coverage * effect_color.a);
    return color;
}
