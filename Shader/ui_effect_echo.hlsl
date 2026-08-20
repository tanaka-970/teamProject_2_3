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
    float4 current = source_texture.Sample(source_sampler, i.uv);
    if (effect_params3.y < 0.5f) return current;

    float angle = radians(effect_params1.x);
    float2 offset = float2(cos(angle), sin(angle)) *
        effect_params0.w * target_size.zw;
    float4 previous = aux_texture.Sample(source_sampler, i.uv - offset);
    float decay = saturate(effect_params0.y);
    // 前フレームの「既にEcho済みの結果」を戻すことで、履歴枚数ぶんのRTを
    // 持たずに時間方向へ尾を蓄積する。amount は現在像の優先度。
    float currentWeight = saturate(effect_params0.w > 0.0f ? 1.0f /
        (1.0f + effect_params0.w * 0.05f) : 1.0f);
    float4 result = lerp(current, previous, decay * (1.0f - currentWeight * 0.25f));
    result.a = max(current.a, previous.a * decay);
    return result;
}
