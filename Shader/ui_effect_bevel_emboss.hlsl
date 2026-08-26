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

float alpha_at(float2 uv)
{
    return source_texture.Sample(source_sampler, saturate(uv)).a;
}

float4 main(VSOutput input) : SV_TARGET
{
    const float angle = radians(effect_params1.x);
    const float2 light_direction = float2(cos(angle), sin(angle));
    const float2 texel = max(effect_params0.x, 0.5) * target_size.zw;
    const float left = alpha_at(input.uv - float2(texel.x, 0.0));
    const float right = alpha_at(input.uv + float2(texel.x, 0.0));
    const float up = alpha_at(input.uv - float2(0.0, texel.y));
    const float down = alpha_at(input.uv + float2(0.0, texel.y));
    const float up_left = alpha_at(input.uv - texel);
    const float up_right = alpha_at(input.uv + float2(texel.x, -texel.y));
    const float down_left = alpha_at(input.uv + float2(-texel.x, texel.y));
    const float down_right = alpha_at(input.uv + texel);
    const float2 gradient = float2(
        (right - left) * 2.0 + up_right + down_right - up_left - down_left,
        (down - up) * 2.0 + down_left + down_right - up_left - up_right) * 0.25;
    const float gradient_length = length(gradient);
    const float2 normal = gradient_length > 0.0001
        ? gradient / gradient_length : float2(0.0, 0.0);
    const float lighting = dot(normal, light_direction);
    const float depth = max(effect_params0.w, 0.0);
    const float edge = saturate(gradient_length * depth * 2.0);
    const float highlight = max(lighting, 0.0) * edge * effect_color.a;
    const float shadow = max(-lighting, 0.0) * edge * effect_color_2.a;

    float4 result = source_texture.Sample(source_sampler, input.uv);
    const float coverage = saturate(result.a);
    const float application = saturate(effect_params0.y) * coverage;
    float3 beveled = lerp(result.rgb, effect_color.rgb, saturate(highlight));
    beveled = lerp(beveled, effect_color_2.rgb, saturate(shadow));
    result.rgb = lerp(result.rgb, beveled, application);
    return result;
}
