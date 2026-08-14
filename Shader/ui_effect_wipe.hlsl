Texture2D source_texture : register(t0);
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
    const float angle = radians(effect_params1.x);
    const float2 direction = normalize(float2(cos(angle), sin(angle)));
    const float projected = dot(input.uv - 0.5, direction) + 0.5;
    const float progress = saturate(effect_params1.y);
    const float softness = max(effect_params1.z, 0.0001);
    color.a *= 1.0 - smoothstep(progress, progress + softness, projected);
    return color;
}
