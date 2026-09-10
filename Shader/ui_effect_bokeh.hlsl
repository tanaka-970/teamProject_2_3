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
    const float threshold = effect_params0.z;
    const float sides = effect_params0.w;
    const float2 texel = target_size.zw;

    // 黄金角で散らすと少ないサンプルでも円板が均される。
    const float golden = 2.39996323;
    float3 accumulated = 0.0;
    float total = 0.0;

    [unroll]
    for (int i = 0; i < 24; ++i)
    {
        const float t = (float(i) + 0.5) / 24.0;
        const float a = float(i) * golden;
        float2 offset = float2(cos(a), sin(a)) * sqrt(t);
        // 多角形ボケ。3 未満なら円のまま。
        if (sides > 2.5)
        {
            const float corners = floor(sides);
            const float wedge = 3.14159265 / corners;
            const float shaped = cos(wedge) /
                max(cos(fmod(a, 2.0 * wedge) - wedge), 0.0001);
            offset *= shaped;
        }
        const float2 sample_uv = input.uv + offset * radius * texel;
        const float3 sampled = source_texture.Sample(source_sampler, sample_uv).rgb;
        // 明るい画素ほど強く玉になる。
        const float weight = 1.0 + max(fx_luma(sampled) - threshold, 0.0) * 8.0;
        accumulated += sampled * weight;
        total += weight;
    }

    const float3 bokeh = accumulated / max(total, 0.0001);
    color.rgb = lerp(color.rgb, bokeh, intensity);
    return color;
}
