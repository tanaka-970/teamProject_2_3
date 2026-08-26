Texture2D source_texture : register(t0);
SamplerState source_sampler : register(s0);

cbuffer UIEffectConstants : register(b0)
{
    float4 effect_color;
    float4 effect_params0; // radius, intensity, threshold, amount
    float4 effect_params1; // angle, progress, softness, speed
    float4 effect_params2;
    float4 target_size;
    float4 effect_color_2;
    float4 effect_color_3;
    float4 effect_color_4;
    float4 effect_color_stops;
    float4 effect_params3;
    float4 brush_pattern_settings;
    float4 brush_pattern_weights[4];
};

struct VSOutput { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };

static const float pi = 3.14159265359;

float4 sample_transparent(float2 uv)
{
    if (any(uv < 0.0) || any(uv > 1.0)) return 0.0;
    return source_texture.Sample(source_sampler, uv);
}

float4 sample_from_origin(float2 origin_uv, float2 origin_to_pixel,
    float2 axis, float projected, float sample_projected)
{
    const float2 sample_offset = origin_to_pixel + axis *
        (sample_projected - projected);
    const float2 uv = origin_uv + sample_offset / target_size.xy;
    return sample_transparent(uv);
}

float4 main(VSOutput input) : SV_TARGET
{
    const float angle = radians(effect_params1.x);
    const float2 axis = normalize(float2(cos(angle), sin(angle)));
    const float2 centered_pixels = (input.uv - 0.5) * target_size.xy;
    const float progress = saturate(effect_params1.y);
    const float radius = max(effect_params0.x, 1.0);
    const float4 source = source_texture.Sample(source_sampler, input.uv);
    float4 curled = source;

    const int pattern = (int)round(effect_params3.x);
    float distance_from_fold = -1.0;
    float shadow_distance = 0.0;

    if (pattern == 1)
    {
        // Corner curl: the fold line is a shrinking circle around the selected corner.
        const float2 corner_uv = saturate(effect_params2.xy);
        const float2 to_pixel = (input.uv - corner_uv) * target_size.xy;
        const float distance_from_corner = length(to_pixel);
        const float2 corner_axis = distance_from_corner > 0.001
            ? to_pixel / distance_from_corner : float2(1.0, 0.0);
        const float2 corner_pixels[4] = {
            float2(0.0, 0.0), float2(target_size.x, 0.0),
            float2(0.0, target_size.y), float2(target_size.x, target_size.y)
        };
        float extent = 0.0;
        [unroll]
        for (int corner_index = 0; corner_index < 4; ++corner_index)
            extent = max(extent, length(corner_pixels[corner_index] -
                corner_uv * target_size.xy));
        const float fold = extent * (1.0 - progress);
        distance_from_fold = distance_from_corner - fold;
        shadow_distance = distance_from_fold;
        if (distance_from_fold > 0.0)
        {
            const float theta = distance_from_fold / radius;
            if (theta <= pi)
            {
                const float sample_distance = fold + sin(theta) * radius;
                curled = sample_from_origin(corner_uv, to_pixel, corner_axis,
                    distance_from_corner, sample_distance);
                curled.rgb *= 1.0 - 0.3 * sin(theta);
            }
            else
            {
                const float backside_distance = distance_from_fold - pi * radius;
                const float sample_distance = fold - backside_distance;
                if (sample_distance < -1.0)
                    curled = float4(0.0, 0.0, 0.0, 0.0);
                else
                {
                    curled = sample_from_origin(corner_uv, to_pixel, corner_axis,
                        distance_from_corner, sample_distance);
                    curled.rgb = lerp(curled.rgb, effect_color.rgb,
                        saturate(effect_color.a));
                    curled.rgb *= 0.75;
                    curled.a *= saturate(effect_params0.w);
                }
            }
        }
    }
    else if (pattern == 2)
    {
        // Center fold: both sides curl toward a fold line at the center.
        const float projected = dot(centered_pixels, axis);
        const float extent = dot(0.5 * target_size.xy, abs(axis));
        const float fold = extent * (1.0 - progress);
        const float side = projected < 0.0 ? -1.0 : 1.0;
        const float local_projected = abs(projected);
        distance_from_fold = local_projected - fold;
        shadow_distance = distance_from_fold;
        if (distance_from_fold > 0.0)
        {
            const float theta = distance_from_fold / radius;
            const float2 side_axis = axis * side;
            if (theta <= pi)
            {
                const float sample_distance = fold + sin(theta) * radius;
                curled = sample_from_origin(0.5, centered_pixels, side_axis,
                    local_projected, sample_distance);
                curled.rgb *= 1.0 - 0.3 * sin(theta);
            }
            else
            {
                const float backside_distance = distance_from_fold - pi * radius;
                const float sample_distance = fold - backside_distance;
                if (sample_distance < -extent - 1.0)
                    curled = float4(0.0, 0.0, 0.0, 0.0);
                else
                {
                    curled = sample_from_origin(0.5, centered_pixels, side_axis,
                        local_projected, sample_distance);
                    curled.rgb = lerp(curled.rgb, effect_color.rgb,
                        saturate(effect_color.a));
                    curled.rgb *= 0.75;
                    curled.a *= saturate(effect_params0.w);
                }
            }
        }
    }
    else if (pattern == 3)
    {
        // Accordion: repeat a small cylindrical bend along the selected axis.
        const float projected = dot(centered_pixels, axis);
        const float extent = dot(0.5 * target_size.xy, abs(axis));
        const float period = max(radius * 2.0, 8.0);
        const float phase = (projected + extent) / period * pi + progress * pi;
        const float bend = sin(phase) * radius * progress;
        const float2 sample_offset = centered_pixels - axis * bend;
        curled = sample_transparent(0.5 + sample_offset / target_size.xy);
        curled.rgb *= 0.82 + 0.18 * abs(cos(phase));
        shadow_distance = abs(bend);
    }
    else
    {
        // Straight curl: the original single cylindrical page edge.
        const float projected = dot(centered_pixels, axis);
        const float extent = dot(0.5 * target_size.xy, abs(axis));
        const float fold = extent - progress * extent * 2.0;
        distance_from_fold = projected - fold;
        shadow_distance = distance_from_fold;
        if (distance_from_fold > 0.0)
        {
            const float theta = distance_from_fold / radius;
            if (theta <= pi)
            {
                const float sample_projected = fold + sin(theta) * radius;
                curled = sample_from_origin(0.5, centered_pixels, axis,
                    projected, sample_projected);
                curled.rgb *= 1.0 - 0.3 * sin(theta);
            }
            else
            {
                const float backside_distance = distance_from_fold - pi * radius;
                const float sample_projected = fold - backside_distance;
                if (sample_projected < -extent - 1.0)
                    curled = float4(0.0, 0.0, 0.0, 0.0);
                else
                {
                    curled = sample_from_origin(0.5, centered_pixels, axis,
                        projected, sample_projected);
                    curled.rgb = lerp(curled.rgb, effect_color.rgb,
                        saturate(effect_color.a));
                    curled.rgb *= 0.75;
                    curled.a *= saturate(effect_params0.w);
                }
            }
        }
    }

    const float shadow_width = max(radius * 1.5, 1.0);
    const float fold_shadow = (1.0 - smoothstep(0.0, shadow_width,
        abs(shadow_distance))) * saturate(effect_params1.z) *
        saturate(effect_params1.y) * 0.45;
    curled.rgb *= 1.0 - fold_shadow;
    return lerp(source, curled, saturate(effect_params0.y));
}
