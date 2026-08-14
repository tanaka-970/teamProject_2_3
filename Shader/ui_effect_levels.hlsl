Texture2D source_texture : register(t0);
SamplerState source_sampler : register(s0);

cbuffer UIEffectConstants : register(b0)
{
    float4 effect_color;
    float4 effect_params0; // radius, intensity, input black, input white
    float4 effect_params1; // gamma exponent, progress, softness, speed
    float4 effect_params2; // output black/white, seed, time
    float4 target_size;
    float4 effect_color_2;
    float4 effect_color_3;
    float4 effect_color_4;
    float4 effect_color_stops;
};

struct VSOutput { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };

float4 main(VSOutput input) : SV_TARGET
{
    const float4 source = source_texture.Sample(source_sampler, input.uv);
    const float input_black = saturate(effect_params0.z);
    const float input_white = max(saturate(effect_params0.w), input_black + 0.0001);
    const bool output_valid = effect_params2.x >= 0.0 && effect_params2.y <= 1.0 &&
        effect_params2.y > effect_params2.x;
    const float output_black = output_valid ? effect_params2.x : 0.0;
    const float output_white = output_valid ? effect_params2.y : 1.0;
    const float gamma = exp2(effect_params1.x);
    float4 adjusted = source;
    adjusted.rgb = saturate((source.rgb - input_black) /
        (input_white - input_black));
    adjusted.rgb = pow(adjusted.rgb, 1.0 / max(gamma, 0.0001));
    adjusted.rgb = lerp(float3(output_black, output_black, output_black),
        float3(output_white, output_white, output_white), adjusted.rgb);
    return lerp(source, adjusted, saturate(effect_params0.y));
}
