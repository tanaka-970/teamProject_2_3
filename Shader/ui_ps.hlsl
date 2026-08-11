Texture2D ui_texture : register(t0);
SamplerState ui_sampler : register(s0);

cbuffer ui_visual_constants : register(b0)
{
    float4 fill_color_2;
    float4 fill_params;
    float4 stroke_color_2;
    float4 stroke_params;
    float4 outline_color;
    float4 shadow_offset;
    float4 shadow_color;
    float4 atlas_size;
};

struct PS_INPUT
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    float2 gradient_uv : TEXCOORD1;
    float4 color : COLOR0;
    float4 uv_bounds : TEXCOORD2;
};

float GradientAmount(float2 uv)
{
    float amount = 0.0f;
    if (fill_params.x >= 0.5f)
    {
        if (fill_params.x < 1.5f)
        {
            float2 direction = float2(cos(fill_params.y), sin(fill_params.y));
            amount = saturate(dot(uv - fill_params.zw, direction) + 0.5f);
        }
        else
        {
            amount = saturate(length(uv - fill_params.zw) / 0.70710678f);
        }
    }
    return amount;
}

float SdfSpread()
{
    return max(atlas_size.z, 0.0001f);
}

bool InGlyphSdfRegion(float2 uv, float4 uv_bounds)
{
    const float2 texel = 1.0f / max(atlas_size.xy, float2(1.0f, 1.0f));
    const float2 padding = texel * SdfSpread();
    return uv.x >= uv_bounds.x - padding.x &&
        uv.y >= uv_bounds.y - padding.y &&
        uv.x <= uv_bounds.z + padding.x &&
        uv.y <= uv_bounds.w + padding.y;
}

float SampleSdfDistance(float2 uv, float4 uv_bounds)
{
    float distance = -SdfSpread();
    if (InGlyphSdfRegion(uv, uv_bounds))
    {
        const float encoded = ui_texture.Sample(ui_sampler, uv).a;
        distance = (encoded * 2.0f - 1.0f) * SdfSpread();
    }
    return distance;
}

float SdfCoverage(float distance)
{
    const float aa = max(fwidth(distance), 0.0001f);
    return smoothstep(-aa, aa, distance);
}

float SdfOutlineCoverage(float distance, float width)
{
    const float aa = max(fwidth(distance), 0.0001f);
    const float outer = smoothstep(-width - aa, -width + aa, distance);
    const float inner = smoothstep(-aa, aa, distance);
    return saturate(outer - inner);
}

float4 main(PS_INPUT input) : SV_TARGET
{
    float4 result = input.color;
    if (stroke_params.z > 0.5f)
    {
        const float outline_width = max(stroke_params.y, 0.0f);
        if (outline_width <= 0.0f && shadow_color.a <= 0.0f)
        {
            const float distance = SampleSdfDistance(input.uv, input.uv_bounds);
            result.a *= SdfCoverage(distance);
        }
        else
        {
            const float distance = SampleSdfDistance(input.uv, input.uv_bounds);
            const float distance_per_screen_pixel = max(fwidth(distance), 0.0001f);
            const float sdf_outline_width = min(
                outline_width * distance_per_screen_pixel, SdfSpread());
            const float fill = SdfCoverage(distance);
            const float outline = outline_width > 0.0f
                ? SdfOutlineCoverage(distance, sdf_outline_width) : 0.0f;

            float shadow = 0.0f;
            if (shadow_color.a > 0.0f)
            {
                const float2 shadow_uv_offset = ddx(input.uv) * shadow_offset.x +
                    ddy(input.uv) * shadow_offset.y;
                const float shadow_distance = SampleSdfDistance(
                    input.uv - shadow_uv_offset, input.uv_bounds);
                shadow = SdfCoverage(shadow_distance);
                if (outline_width > 0.0f)
                {
                    const float shadow_derivative = max(
                        fwidth(shadow_distance), 0.0001f);
                    const float shadow_outline_width = min(
                        outline_width * shadow_derivative, SdfSpread());
                    shadow = max(shadow, SdfOutlineCoverage(
                        shadow_distance, shadow_outline_width));
                }
            }

            result = shadow_color * shadow;
            result = lerp(result, outline_color, outline);
            result = lerp(result, input.color, fill);
            result.a = max(result.a, input.color.a * fill);
        }
    }
    else
    {
        float4 color = input.color;
        const float amount = GradientAmount(input.gradient_uv);
        color = lerp(color, fill_color_2, amount);
        if (stroke_params.x > 0.5f)
        {
            color = lerp(color, stroke_color_2, saturate(input.gradient_uv.x));
        }
        result = ui_texture.Sample(ui_sampler, input.uv) * color;
    }
    return result;
}
