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
    const float radius = max(effect_params0.x, 0.0);
    const float intensity = saturate(effect_params0.y);
    const float width = saturate(effect_params0.w);
    const float falloff = max(effect_params1.z, 0.0001);
    const float angle = radians(effect_params1.x);
    const float2 center = effect_params2.xy;
    const float2 texel = target_size.zw;

    // 帯の法線方向の距離だけでぼかし量を決める。
    const float2 normal = float2(-sin(angle), cos(angle));
    const float band = abs(dot(input.uv - center, normal));
    const float blur = smoothstep(width, width + falloff, band);

    const float golden = 2.39996323;
    float3 accumulated = 0.0;

    [unroll]
    for (int i = 0; i < 16; ++i)
    {
        const float t = (float(i) + 0.5) / 16.0;
        const float a = float(i) * golden;
        const float2 offset = float2(cos(a), sin(a)) * sqrt(t);
        accumulated += source_texture.Sample(source_sampler,
            input.uv + offset * radius * blur * texel).rgb;
    }

    const float3 blurred = accumulated / 16.0;
    color.rgb = lerp(color.rgb, blurred, blur * intensity);
    return color;
}
