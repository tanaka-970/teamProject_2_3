Texture2D source_texture : register(t0);
SamplerState source_sampler : register(s0);

cbuffer UIEffectConstants : register(b0)
{
    float4 effect_color;
    float4 effect_params0; // radius, intensity, threshold, amount
    float4 effect_params1; // hue, progress, softness, speed
    float4 effect_params2; // direction.xy, seed, time
    float4 target_size;    // width, height, 1 / width, 1 / height
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

float3 HueShift(float3 color, float angle)
{
    const float s = sin(angle);
    const float c = cos(angle);
    const float3 weights = float3(0.299, 0.587, 0.114);
    return color * c + cross(weights, color) * s + weights * dot(weights, color) * (1.0 - c);
}

float4 main(VSOutput input) : SV_TARGET
{
    float4 color = source_texture.Sample(source_sampler, input.uv);
    const float brightness = effect_params0.w;
    const float contrast = effect_params0.y;
    const float saturation = effect_params0.x;
    const float hue = radians(effect_params1.x);

    color.rgb += brightness;
    color.rgb = (color.rgb - 0.5) * max(contrast, 0.0) + 0.5;
    const float gray = dot(color.rgb, float3(0.299, 0.587, 0.114));
    color.rgb = lerp(float3(gray, gray, gray), color.rgb, max(saturation, 0.0));
    color.rgb = HueShift(color.rgb, hue) * effect_color.rgb;
    return saturate(color);
}
