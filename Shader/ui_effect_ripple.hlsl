Texture2D source_texture : register(t0);
SamplerState source_sampler : register(s0);

cbuffer UIEffectConstants : register(b0)
{
    float4 effect_color;
    float4 effect_params0; // wavelength, intensity, threshold, amplitude
    float4 effect_params1; // angle, progress, softness, speed
    float4 effect_params2; // center.xy, seed, time
    float4 target_size;
    float4 effect_color_2;
    float4 effect_color_3;
    float4 effect_color_4;
    float4 effect_color_stops;
};

struct VSOutput { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };
static const float two_pi = 6.28318530718;

float2 SafeCenter()
{
    const bool valid = effect_params2.x >= 0.0 && effect_params2.x <= 1.0 &&
        effect_params2.y >= 0.0 && effect_params2.y <= 1.0;
    return valid ? effect_params2.xy : float2(0.5, 0.5);
}

float4 main(VSOutput input) : SV_TARGET
{
    const float2 center = SafeCenter();
    const float2 pixel_delta = (input.uv - center) * target_size.xy;
    const float distance_pixels = length(pixel_delta);
    const float2 direction = distance_pixels > 0.0001 ? pixel_delta / distance_pixels :
        float2(0.0, 0.0);
    const float wavelength = max(effect_params0.x, 1.0);
    const float phase = distance_pixels / wavelength * two_pi -
        effect_params2.w * effect_params1.w + effect_params2.z;
    const float displacement = sin(phase) * effect_params0.w;
    const float2 ripple_uv = input.uv + direction * displacement * target_size.zw;
    const float4 source = source_texture.Sample(source_sampler, input.uv);
    const float4 ripple = source_texture.Sample(source_sampler, ripple_uv);
    return lerp(source, ripple, saturate(effect_params0.y));
}
