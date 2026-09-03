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
    const float intensity = max(effect_params0.y, 0.0);
    const float threshold = effect_params0.z;
    const float spacing = effect_params0.w;
    const float ghosts = max(floor(effect_params0.x), 1.0);
    const float halo_width = max(effect_params1.z, 0.0001);
    const float2 light = effect_params2.xy;

    // 光源から見た反対側へゴーストを等間隔に並べる。
    const float2 to_light = (light - input.uv) * spacing;
    float3 accumulated = 0.0;

    for (float g = 1.0; g <= ghosts; g += 1.0)
    {
        const float2 sample_uv = input.uv + to_light * g;
        if (any(sample_uv < 0.0) || any(sample_uv > 1.0)) continue;
        const float3 sampled = source_texture.Sample(source_sampler, sample_uv).rgb;
        const float bright = max(fx_luma(sampled) - threshold, 0.0);
        // 遠いゴーストほど淡く、色を少しずつずらす。
        const float fade = 1.0 - g / (ghosts + 1.0);
        const float3 tint = lerp(effect_color.rgb, effect_color.gbr, g / ghosts);
        accumulated += sampled * bright * fade * tint;
    }

    // 光源を囲むハロー。
    const float2 halo_dir = normalize(light - input.uv + 0.00001);
    const float3 halo_sample = source_texture.Sample(source_sampler,
        saturate(input.uv + halo_dir * spacing)).rgb;
    const float halo_bright = max(fx_luma(halo_sample) - threshold, 0.0);
    const float ring = 1.0 - smoothstep(0.0, halo_width,
        abs(length(light - input.uv) - spacing));
    accumulated += halo_sample * halo_bright * ring * effect_color.rgb;

    color.rgb += accumulated * intensity * effect_color.a;
    return color;
}
