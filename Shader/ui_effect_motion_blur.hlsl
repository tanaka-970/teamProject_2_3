Texture2D source_texture : register(t0);
Texture2D aux_texture : register(t1);
SamplerState source_sampler : register(s0);
cbuffer UIEffectConstants : register(b0)
{
    float4 effect_color;
    float4 effect_params0; // radius, intensity, threshold, amount
    float4 effect_params1; // angle, progress, softness, speed
    float4 effect_params2; // direction.xy, seed, time
    float4 target_size;    // width,height,1/w,1/h
    float4 effect_color_2;
    float4 effect_color_3;
    float4 effect_color_4;
    float4 effect_color_stops;
    float4 effect_params3;
    float4 brush_pattern_settings;
    float4 brush_pattern_weights[4];
};
struct VSOutput { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };
float4 main(VSOutput i):SV_TARGET
{
    float angle = radians(effect_params1.x);
    float2 dir = float2(cos(angle), sin(angle)) *
        effect_params0.x * target_size.zw;
    const int N = 17;
    float4 spatial = 0.0f;
    float4 temporal = 0.0f;
    [unroll]
    for (int k = 0; k < N; ++k)
    {
        float t = (k / (float)(N - 1)) - 0.5f;
        spatial += source_texture.Sample(source_sampler, i.uv + dir * t);
        if (effect_params3.y > 0.5f)
            temporal += aux_texture.Sample(source_sampler, i.uv + dir * t);
    }
    spatial /= N;
    temporal = effect_params3.y > 0.5f ? temporal / N : spatial;
    float4 base = source_texture.Sample(source_sampler, i.uv);
    float temporalMix = saturate(effect_params1.w == 0.0f ? 0.5f : abs(effect_params1.w));
    float4 blurred = lerp(spatial, temporal, temporalMix);
    blurred.a = max(spatial.a, temporal.a * temporalMix);
    return lerp(base, blurred, saturate(effect_params0.y));
}
