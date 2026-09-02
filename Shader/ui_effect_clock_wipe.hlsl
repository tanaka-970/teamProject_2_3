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
    const float progress = saturate(effect_params1.y);
    const float softness = max(effect_params1.z, 0.0001);
    const float start = radians(effect_params1.x);
    const float intensity = saturate(effect_params0.y);
    const float inner_hole = saturate(effect_params0.z);
    // 掃引量。負の値で逆回りにする。
    const float span = clamp(effect_params0.w, -1.0, 1.0);
    const float2 center = effect_params2.xy;

    // 縦横比を補正しないと中心の空きが楕円になる。
    const float aspect = max(target_size.x, 1.0) / max(target_size.y, 1.0);
    float2 delta = input.uv - center;
    delta.x *= aspect;
    const float radius = length(delta) * 2.0;

    // 開始角度から測った 0..1 の角度。
    float turn = frac((atan2(delta.y, delta.x) - start) * 0.15915494);
    if (span < 0.0) turn = 1.0 - turn;

    const float swept = progress * abs(span);
    // swept が 0 なら境界が -softness..0 に来るので、どの画素も覆わない。
    float coverage = 1.0 - smoothstep(swept - softness, swept, turn);
    // 全周を掃いたら継ぎ目を残さない。
    coverage = max(coverage, smoothstep(0.97, 1.0, swept));
    // 中心の空きは最後まで残す。0 のときは中心まで覆う。
    coverage *= smoothstep(-softness, 0.0, radius - inner_hole);

    color.a *= saturate(1.0 - saturate(coverage) * intensity);
    return color;
}
