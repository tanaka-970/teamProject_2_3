cbuffer UIEffectConstants : register(b0)
{
    float4 screen_size;
    float4 fill_color_2;
    float4 mode;
    float4 outline_color;
    float4 shadow_offset;
    float4 shadow_color;
    float4 atlas_size;
    float4 fill_parameters;
    float4 clip_parameters;
    float4 clip_bounds;
    float4 mask_parameters;
    float4 mask_uv;
};

Texture2D ui_source : register(t0);
Texture2D ui_backdrop : register(t1);
SamplerState ui_sampler : register(s0);

struct PSInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
    float4 uv_bounds : TEXCOORD1;
};

float4 sample_blur(float2 uv, float2 texel, float radius)
{
    const float weights[5] = { 0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216 };
    float4 result = ui_source.Sample(ui_sampler, uv) * weights[0];
    const float safe_radius = max(radius, 1.0);
    [unroll]
    for (int index = 1; index < 5; ++index)
    {
        const float2 offset = texel * safe_radius * (float)index;
        result += ui_source.Sample(ui_sampler, uv + offset) * weights[index];
        result += ui_source.Sample(ui_sampler, uv - offset) * weights[index];
    }
    return result;
}

float4 sample_blur_backdrop(float2 uv, float2 texel, float radius)
{
    const float weights[5] = { 0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216 };
    float4 result = ui_backdrop.Sample(ui_sampler, uv) * weights[0];
    const float safe_radius = max(radius, 1.0);
    [unroll]
    for (int index = 1; index < 5; ++index)
    {
        const float2 offset = texel * safe_radius * (float)index;
        result += ui_backdrop.Sample(ui_sampler, uv + offset) * weights[index];
        result += ui_backdrop.Sample(ui_sampler, uv - offset) * weights[index];
    }
    return result;
}

float4 main(PSInput input) : SV_TARGET
{
    const float2 texel = 1.0 / max(screen_size.xy, float2(1.0, 1.0));
    const float4 source = ui_source.Sample(ui_sampler, input.uv);
    const uint kind = (uint)mode.x;
    if (kind == 1)
    {
        if (fill_parameters.w > 0.5)
        {
            const float4 backdrop = sample_blur_backdrop(input.uv, texel,
                fill_parameters.x);
            return float4(lerp(backdrop.rgb, source.rgb, source.a), source.a);
        }
        return sample_blur(input.uv, texel, fill_parameters.x);
    }

    if (kind == 2)
    {
        const float4 blurred = sample_blur(input.uv, texel, fill_parameters.x);
        const float glow_alpha = max(0.0, blurred.a - source.a) * fill_parameters.y;
        return source + float4(fill_color_2.rgb, 1.0) * glow_alpha;
    }

    if (kind == 3)
    {
        const float4 blurred = sample_blur(input.uv, texel, fill_parameters.x);
        const float outline_alpha = saturate(blurred.a - source.a) * fill_parameters.y;
        return float4(outline_color.rgb, outline_alpha * outline_color.a) + source;
    }

    if (kind == 4)
    {
        const float2 shadow_uv = input.uv - shadow_offset.xy * texel;
        const float shadow_alpha = ui_source.Sample(ui_sampler, shadow_uv).a * shadow_color.a;
        return float4(shadow_color.rgb, shadow_alpha * fill_parameters.y) + source;
    }

    if (kind == 5)
    {
        float3 adjusted = max(source.rgb + fill_color_2.rgb, 0.0);
        adjusted *= max(outline_color.rgb, 0.0);
        return float4(adjusted, source.a);
    }

    return source;
}
