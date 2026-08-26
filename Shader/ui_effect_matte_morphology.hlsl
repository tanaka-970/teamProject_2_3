Texture2D source_texture : register(t0);
SamplerState source_sampler : register(s0);

cbuffer UIEffectConstants : register(b0)
{
    float4 effect_color;
    float4 effect_params0; // radius, intensity, threshold, amount
    float4 effect_params1;
    float4 effect_params2;
    float4 target_size;    // width, height, 1 / width, 1 / height
    float4 effect_color_2;
    float4 effect_color_3;
    float4 effect_color_4;
    float4 effect_color_stops;
    float4 effect_params3; // x = morphology mode
    float4 brush_pattern_settings;
    float4 brush_pattern_weights[4];
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

static const float2 morphology_directions[8] = {
    float2(1.0, 0.0), float2(-1.0, 0.0),
    float2(0.0, 1.0), float2(0.0, -1.0),
    float2(0.70710678, 0.70710678), float2(-0.70710678, 0.70710678),
    float2(0.70710678, -0.70710678), float2(-0.70710678, -0.70710678)
};

float4 sample_at(float2 uv)
{
    return source_texture.Sample(source_sampler, saturate(uv));
}

float4 main(VSOutput input) : SV_TARGET
{
    const float4 source = sample_at(input.uv);
    const float center = source.a;
    const float radius = max(effect_params0.x, 0.0);
    float maximum = center;
    float minimum = center;
    float4 dilation_sample = source;

    // A single far-away 8 tap ring leaves gaps as the radius grows. Four
    // concentric rings keep the operation stable while remaining practical in
    // the shared one-pass EffectChain.
    [unroll]
    for (int ring = 1; ring <= 4; ++ring)
    {
        const float2 step_uv = target_size.zw * radius * (ring / 4.0);
        [unroll]
        for (int index = 0; index < 8; ++index)
        {
            const float4 sample_value = sample_at(input.uv +
                morphology_directions[index] * step_uv);
            if (sample_value.a > maximum)
            {
                maximum = sample_value.a;
                dilation_sample = sample_value;
            }
            minimum = min(minimum, sample_value.a);
        }
    }

    // Fill only when opposite sides support the pixel. Unlike max(center,
    // minimum), this actually closes a local hole and avoids growing an open
    // silhouette as ordinary dilation would.
    const float2 outer_step = target_size.zw * radius;
    const float horizontal = min(
        sample_at(input.uv + float2(outer_step.x, 0.0)).a,
        sample_at(input.uv - float2(outer_step.x, 0.0)).a);
    const float vertical = min(
        sample_at(input.uv + float2(0.0, outer_step.y)).a,
        sample_at(input.uv - float2(0.0, outer_step.y)).a);
    const float diagonal_a = min(
        sample_at(input.uv + outer_step).a,
        sample_at(input.uv - outer_step).a);
    const float diagonal_b = min(
        sample_at(input.uv + float2(outer_step.x, -outer_step.y)).a,
        sample_at(input.uv + float2(-outer_step.x, outer_step.y)).a);
    const float hole_fill = max(center,
        max(max(horizontal, vertical), max(diagonal_a, diagonal_b)));

    const int mode = (int)round(effect_params3.x);
    float morphological = maximum; // 0: dilation
    if (mode == 1) morphological = minimum; // 1: erosion
    else if (mode == 2) morphological = hole_fill; // 2: local hole fill
    else if (mode == 3) morphological = maximum - minimum; // 3: matte edge

    const float alpha = lerp(center, saturate(morphological),
        saturate(effect_params0.y));
    float4 result = source;
    if ((mode == 0 || mode == 2 || mode == 3) && morphological > center)
        result.rgb = lerp(source.rgb, dilation_sample.rgb,
            saturate((morphological - center) * effect_params0.y));
    result.a = alpha;
    return result;
}
