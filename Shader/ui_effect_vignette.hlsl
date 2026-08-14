Texture2D source_texture : register(t0);
SamplerState source_sampler : register(s0);

cbuffer UIEffectConstants : register(b0)
{
    float4 effect_color;
    float4 effect_params0; // radius, strength, threshold, amount
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
    float4 source = source_texture.Sample(source_sampler, input.uv);
    const float2 aspect = float2(target_size.x / max(target_size.y, 1.0), 1.0);
    const float distance_from_center = length((input.uv - 0.5) * aspect);
    const float radius = max(effect_params0.x, 0.0);
    const float softness = max(effect_params1.z, 0.0001);
    const float edge = smoothstep(radius, radius + softness, distance_from_center);
    const float blend = edge * saturate(effect_params0.y) * effect_color.a;
    source.rgb = lerp(source.rgb, effect_color.rgb, blend);
    return source;
}
