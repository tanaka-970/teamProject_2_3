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
    const float decay = saturate(effect_params0.w);
    const float density = max(effect_params1.z, 0.0001);
    const float2 light = effect_params2.xy;

    // 光源へ向かって少しずつ寄りながら明部だけを積む。
    const float2 step_uv = (input.uv - light) * (density / 32.0);
    float2 sample_uv = input.uv;
    float weight = 1.0;
    float3 accumulated = 0.0;

    [unroll]
    for (int i = 0; i < 32; ++i)
    {
        sample_uv -= step_uv;
        const float3 sampled = source_texture.Sample(source_sampler, sample_uv).rgb;
        // しきい値より明るいぶんだけを光芒にする。
        const float bright = max(fx_luma(sampled) - threshold, 0.0);
        accumulated += sampled * bright * weight;
        weight *= decay;
    }

    accumulated *= intensity / 32.0;
    // HDR なので saturate しない。
    color.rgb += accumulated * effect_color.rgb * effect_color.a;
    return color;
}
