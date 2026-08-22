Texture2D effected_texture : register(t0);
Texture2D region_mask_texture : register(t1);
Texture2D original_texture : register(t2);
SamplerState source_sampler : register(s0);

cbuffer UIEffectConstants : register(b0)
{
    float4 effect_color;
    float4 effect_params0;
    float4 effect_params1;
    float4 effect_params2;
    float4 target_size;
    float4 effect_color_2;
    float4 effect_color_3;
    float4 effect_color_4;
    float4 effect_color_stops;
    float4 effect_params3;
    float4 brush_pattern_settings;
    float4 brush_pattern_weights[4];
    float4 effect_region_params;   // center.xy, size.xy
    float4 effect_region_settings; // rotation, feather, strength, shape + invert*4
    float4 effect_region_extra_params[7];
    float4 effect_region_extra_settings[7];
    float4 effect_region_count;    // x = enabled region count
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

float single_region_weight(float2 uv, float4 region_params, float4 region_settings)
{
    const float shape_flags = region_settings.w;
    float mask = 1.0;
    if (shape_flags >= -0.5)
    {
        const bool invert = shape_flags >= 4.0;
        const int shape = (int)floor(shape_flags - (invert ? 4.0 : 0.0) + 0.5);
        const float2 center = region_params.xy;
        const float2 size = max(region_params.zw, float2(0.0001, 0.0001));
        const float angle = region_settings.x * 0.0174532925199433;
        const float s = sin(angle);
        const float c = cos(angle);
        const float2 delta = uv - center;
        const float2 local = float2(
            delta.x * c + delta.y * s,
            -delta.x * s + delta.y * c);
        const float feather = saturate(region_settings.y);
        mask = 0.0;
        if (shape == 2)
        {
            // 投げ縄画像も矩形/楕円と同じ center・size・rotation で配置する。
            // 枠外は黒として扱うため、画像を範囲外へサンプルして端が伸びない。
            const float2 mask_uv = local / (size * 2.0) + 0.5;
            const bool inside = all(mask_uv >= 0.0) && all(mask_uv <= 1.0);
            const float3 sample = inside
                ? region_mask_texture.Sample(source_sampler, mask_uv).rgb
                : float3(0.0, 0.0, 0.0);
            const float mask_value = saturate(dot(sample,
                float3(0.2126, 0.7152, 0.0722)));
            mask = feather <= 0.00001
                ? step(0.5, mask_value)
                : smoothstep(0.5 - feather * 0.5,
                    0.5 + feather * 0.5, mask_value);
        }
        else if (shape == 1)
        {
            const float distance_to_edge = length(local / size);
            mask = feather <= 0.00001
                ? step(distance_to_edge, 1.0)
                : 1.0 - smoothstep(1.0 - feather,
                    1.0 + feather, distance_to_edge);
        }
        else
        {
            const float distance_to_edge = max(abs(local.x / size.x),
                abs(local.y / size.y));
            mask = feather <= 0.00001
                ? step(distance_to_edge, 1.0)
                : 1.0 - smoothstep(1.0 - feather,
                    1.0 + feather, distance_to_edge);
        }
        if (invert) mask = 1.0 - mask;
        mask *= region_settings.z;
    }
    return saturate(mask);
}

float region_weight(float2 uv)
{
    const int region_count = (int)floor(effect_region_count.x + 0.5);
    if (region_count <= 0) return 0.0;

    float mask = single_region_weight(uv,
        effect_region_params, effect_region_settings);
    [unroll]
    for (int index = 0; index < 7; ++index)
    {
        if (index >= region_count - 1) break;
        mask = max(mask, single_region_weight(uv,
            effect_region_extra_params[index],
            effect_region_extra_settings[index]));
    }
    return saturate(mask);
}

float4 main(VSOutput input) : SV_TARGET
{
    const float4 original = original_texture.Sample(source_sampler, input.uv);
    const float4 effected = effected_texture.Sample(source_sampler, input.uv);
    return lerp(original, effected, region_weight(input.uv));
}
