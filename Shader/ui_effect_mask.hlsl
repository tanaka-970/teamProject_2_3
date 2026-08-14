Texture2D source_texture : register(t0);
Texture2D mask_texture : register(t1);
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

float4 main(VSOutput input) : SV_TARGET
{
    float4 color = source_texture.Sample(source_sampler, input.uv);
    const float2 centered = input.uv - effect_params2.xy;
    const float angle = radians(effect_params1.x);
    const float2 axis_x = float2(cos(angle), sin(angle));
    const float2 axis_y = float2(-axis_x.y, axis_x.x);
    const float2 local = float2(dot(centered, axis_x), dot(centered, axis_y)) /
        max(effect_params2.zw, float2(0.0001f, 0.0001f));
    const float rectangle = max(abs(local.x), abs(local.y));
    const float circle = length(local);
    const float shape = lerp(rectangle, circle, saturate(effect_params0.w));
    const float edge = saturate((1.0 - shape) / max(effect_params1.z, 0.0001));
    color.a *= edge;
    if (effect_params1.w > 0.5f)
    {
        const float mask_alpha = mask_texture.Sample(source_sampler, input.uv).a;
        color.a *= smoothstep(0.0f, max(effect_params1.z, 0.0001f), mask_alpha);
    }
    return color;
}
