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

float speed_lines_hash(float line_index, float seed)
{
    return frac(sin(line_index * 127.1 + seed * 311.7) * 43758.5453);
}

float4 main(VSOutput input) : SV_TARGET
{
    float4 color = source_texture.Sample(source_sampler, input.uv);
    const float count = max(floor(effect_params0.x), 1.0);
    const float intensity = saturate(effect_params0.y);
    const float inner_hole = saturate(effect_params0.z);
    const float thickness = saturate(effect_params0.w);
    const float rotation = radians(effect_params1.x);
    const float progress = saturate(effect_params1.y);
    const float softness = max(effect_params1.z, 0.0001);
    const float speed = effect_params1.w;
    const float2 center = effect_params2.xy;
    const float seed = effect_params2.z;
    const float time = effect_params2.w;

    // 縦横比を補正しないと放射が楕円になる。
    const float aspect = max(target_size.x, 1.0) / max(target_size.y, 1.0);
    float2 delta = input.uv - center;
    delta.x *= aspect;
    // 画面の縦端で 1 になる正規化半径。角は 1 を超える。
    const float radius = length(delta) * 2.0;

    // 角度を 0..1 の帯にして、そこを線の本数で刻む。
    const float slot = frac((atan2(delta.y, delta.x) + rotation) * 0.15915494 + 0.5);
    const float scaled = slot * count;
    const float line_index = floor(scaled);

    const float length_variation = speed_lines_hash(line_index, seed);
    const float width_variation = speed_lines_hash(line_index, seed + 17.0);
    const float phase = speed_lines_hash(line_index, seed + 41.0);
    // speed が 0 なら時間項が消えて静止する。
    const float flicker = 0.5 + 0.5 * sin(time * speed + phase * 6.2831853);

    // 中心がずれても角までの実距離を採る。定数だと横長画面で角に線が残る。
    const float corner = 2.0 * length(float2(max(center.x, 1.0 - center.x) * aspect,
        max(center.y, 1.0 - center.y)));
    // progress 0 では最も長い線でも角の外、1 で中心の空きまで届く。
    const float reach = lerp(corner * 1.4, inner_hole, progress);
    const float line_start = reach * (0.85 + length_variation * 0.3)
        * (0.92 + 0.16 * flicker);
    const float radial = smoothstep(line_start, line_start + softness, radius);

    // 角度方向の太さは一定なので、外側ほど太く見えて集中線らしくなる。
    const float offset = abs(frac(scaled) - 0.5);
    const float half_width = max(0.5 * thickness * (0.6 + width_variation * 0.8),
        0.0001);
    const float angular = 1.0 - smoothstep(half_width * (1.0 - softness),
        half_width, offset);

    float coverage = radial * angular;
    // progress が 1 のとき必ず覆いきる。線だけでは隙間が残る。
    coverage = max(coverage, smoothstep(0.88, 1.0, progress));
    coverage = saturate(coverage) * intensity * effect_color.a;

    // HDR なので線の色は丸めない。
    color.rgb = lerp(color.rgb, effect_color.rgb, coverage);
    // 透明な所へ引いた線も見えるようにする。
    color.a = max(color.a, coverage);
    return color;
}
