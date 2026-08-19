Texture2D source_texture : register(t0);
SamplerState source_sampler : register(s0);

cbuffer UIEffectConstants : register(b0)
{
    float4 effect_color;
    float4 effect_params0; // target aspect, opacity, threshold, amount
    float4 effect_params1; // angle, progress, softness, speed
    float4 effect_params2;
    float4 target_size;
    float4 effect_color_2;
    float4 effect_color_3;
    float4 effect_color_4;
    float4 effect_color_stops;
};

struct VSOutput { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };

float4 main(VSOutput input) : SV_TARGET
{
    const float current_aspect = target_size.x / max(target_size.y, 1.0);
    const float desired_aspect = max(effect_params0.x, 0.1);
    float2 content_half_size = float2(0.5, 0.5);
    if (current_aspect > desired_aspect)
        content_half_size.x = 0.5 * desired_aspect / current_aspect;
    else
        content_half_size.y = 0.5 * current_aspect / desired_aspect;
    const float2 outside = abs(input.uv - 0.5) - content_half_size;
    const float boundary = max(outside.x, outside.y);
    const float bar = smoothstep(-max(effect_params1.z, 0.00001), 0.0, boundary);
    const float4 source = source_texture.Sample(source_sampler, input.uv);
    return lerp(source, effect_color,
        bar * saturate(effect_params0.y) * effect_color.a);
}
