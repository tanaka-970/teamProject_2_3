Texture2D source_texture : register(t0);
SamplerState source_sampler : register(s0);

cbuffer UIEffectConstants : register(b0)
{
    float4 effect_color;
    float4 effect_params0; // span radius, amount, lower threshold, unused
    float4 effect_params1; // angle, offset, softness, speed
    float4 effect_params2; // direction.xy, seed, time
    float4 target_size;
    float4 effect_color_2;
    float4 effect_color_3;
    float4 effect_color_4;
    float4 effect_color_stops; // upper threshold, unused, unused, unused
    float4 effect_params3; // waveform = sort mode
    float4 brush_pattern_settings;
    float4 brush_pattern_weights[4];
};

struct VSOutput { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };
static const int sample_count = 16;

float Luma(float3 color)
{
    return dot(color, float3(0.2126, 0.7152, 0.0722));
}

float Saturation(float3 color)
{
    const float maximum = max(color.r, max(color.g, color.b));
    const float minimum = min(color.r, min(color.g, color.b));
    return maximum > 0.0001 ? (maximum - minimum) / maximum : 0.0;
}

float Hue(float3 color)
{
    const float chroma = max(color.r, max(color.g, color.b)) -
        min(color.r, min(color.g, color.b));
    const float angle = atan2(1.73205080757 * (color.g - color.b),
        2.0 * color.r - color.g - color.b);
    return chroma < 0.0001 ? 0.0 : frac(angle / 6.28318530718 + 1.0);
}

float SortKey(float4 value, int mode)
{
    float key = Luma(value.rgb);
    if (mode == 2) key = Saturation(value.rgb);
    else if (mode == 3) key = Hue(value.rgb);
    return key;
}

float4 main(VSOutput input) : SV_TARGET
{
    const float4 source = source_texture.Sample(source_sampler, input.uv);
    const float lower = saturate(effect_params0.z);
    const float upper = max(lower + 0.001, saturate(effect_color_stops.x));
    const float softness = max(effect_params1.z, 0.0001);
    const float source_key = Luma(source.rgb);
    const float eligible = smoothstep(lower, min(lower + softness, upper), source_key) *
        (1.0 - smoothstep(max(upper - softness, lower), upper, source_key));
    if (eligible <= 0.001) return source;

    const float angle = radians(effect_params1.x);
    const float2 axis = normalize(float2(cos(angle), sin(angle)));
    const float2 centered_pixels = (input.uv - 0.5) * target_size.xy;
    const float line_coordinate = dot(centered_pixels, axis);
    const float extent = dot(0.5 * target_size.xy, abs(axis));
    const float span = max(effect_params0.x * 2.0, 16.0);
    const float perpendicular = dot(centered_pixels, float2(-axis.y, axis.x));
    const float phase = frac(effect_params1.y + effect_params2.w * effect_params1.w +
        effect_params2.z * 0.001);
    const float window_shift = (phase - 0.5) * min(span * 0.35, 32.0);
    const float shifted_coordinate = line_coordinate - window_shift;
    const float animated_start = floor((shifted_coordinate + extent) / span) *
        span - extent + window_shift;
    const float local_position = line_coordinate - animated_start;
    const float2 base_pixels = float2(axis.x * animated_start - axis.y * perpendicular,
        axis.y * animated_start + axis.x * perpendicular);

    float4 values[sample_count];
    float keys[sample_count];
    float eligible_samples[sample_count];
    const int sort_mode = (int)round(effect_params3.x);
    [unroll]
    for (int index = 0; index < sample_count; ++index)
    {
        const float sample_position = (index + 0.5) / (float)sample_count * span;
        const float2 sample_pixels = base_pixels + axis * sample_position;
        const float2 sample_uv = saturate(0.5 + sample_pixels * target_size.zw);
        values[index] = source_texture.SampleLevel(source_sampler, sample_uv, 0.0);
        const float sample_luma = Luma(values[index].rgb);
        eligible_samples[index] = sample_luma >= lower && sample_luma <= upper
            ? 1.0 : 0.0;
        keys[index] = eligible_samples[index] > 0.5
            ? SortKey(values[index], sort_mode)
            : (sort_mode == 1 ? -1000000.0 : 1000000.0);
    }

    // Fixed-size bitonic network: deterministic on SM5 and avoids a second GPU pass.
    [unroll]
    for (int size = 2; size <= sample_count; size <<= 1)
    {
        [unroll]
        for (int stride = size >> 1; stride > 0; stride >>= 1)
        {
            [unroll]
            for (int index = 0; index < sample_count; ++index)
            {
                const int partner = index ^ stride;
                if (partner <= index) continue;
                const bool ascending = ((index & size) == 0) ^ (sort_mode == 1);
                const bool swap_values = ascending
                    ? keys[index] > keys[partner] : keys[index] < keys[partner];
                if (swap_values)
                {
                    const float key = keys[index];
                    keys[index] = keys[partner];
                    keys[partner] = key;
                    const float4 value = values[index];
                    values[index] = values[partner];
                    values[partner] = value;
                    const float sample_eligible = eligible_samples[index];
                    eligible_samples[index] = eligible_samples[partner];
                    eligible_samples[partner] = sample_eligible;
                }
            }
        }
    }

    const int rank = clamp((int)floor(saturate(local_position / span) * sample_count),
        0, sample_count - 1);
    int eligible_count = 0;
    [unroll]
    for (int count_index = 0; count_index < sample_count; ++count_index)
        eligible_count += eligible_samples[count_index] > 0.5 ? 1 : 0;
    if (eligible_count == 0) return source;
    const int eligible_rank = clamp((int)floor((rank + 0.5) /
        sample_count * eligible_count), 0, eligible_count - 1);
    const float4 sorted = values[eligible_rank];
    return lerp(source, sorted, saturate(effect_params0.w) * eligible);
}
