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
    float4 effect_color_2;
    float4 effect_color_3;
    float4 effect_color_4;
    float4 effect_color_stops;
    float4 effect_params3; // x = 形状, y = 形状画像の有無
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

static const float two_pi = 6.28318530718;

// 単位形状の境界半径。theta は y を上向きに測る。
float shape_boundary(int shape, float theta, float points, float inner)
{
    if (shape == 1)
    {
        // 星。半径を三角波にすると辺が外へ膨らんで花びらになるので、
        // 頂点と谷を結ぶ直線との交点を解く。
        const float half_tooth = 3.14159265 / points;
        float folded = fmod(theta, 2.0 * half_tooth);
        if (folded < 0.0) folded += 2.0 * half_tooth;
        // 0 が頂点、half_tooth が谷。
        const float phi = half_tooth - abs(folded - half_tooth);
        const float sa = sin(half_tooth);
        const float depth = max(inner, 0.05);
        const float denom = cos(phi) * depth * sa
            + sin(phi) * (1.0 - depth * cos(half_tooth));
        return depth * sa / max(denom, 0.0001);
    }
    if (shape == 2)
    {
        // ハート。極形式の定番式を最大 1 へ正規化する。
        const float s = sin(theta);
        const float c = cos(theta);
        return (2.0 - 2.0 * s + s * sqrt(abs(c)) / (s + 1.4)) * 0.25;
    }
    if (shape == 3)
    {
        // 菱形。|x| + |y| = 1 の境界。
        return 1.0 / max(abs(cos(theta)) + abs(sin(theta)), 0.0001);
    }
    return 1.0;
}

float4 main(VSOutput input) : SV_TARGET
{
    float4 color = source_texture.Sample(source_sampler, input.uv);
    const float progress = saturate(effect_params1.y);
    const float softness = max(effect_params1.z, 0.0001);
    const float rotation = radians(effect_params1.x);
    const float intensity = saturate(effect_params0.y);
    const float inner = saturate(effect_params0.z);
    const float points = max(floor(effect_params0.x), 2.0);
    const int shape = (int)(effect_params3.x + 0.5);
    const float2 center = effect_params2.xy;

    float coverage;
    if (effect_params3.y > 0.5)
    {
        // 形状画像があるときは明るい所から開く。
        const float3 sampled = mask_texture.Sample(source_sampler, input.uv).rgb;
        const float luma = dot(sampled, float3(0.2126, 0.7152, 0.0722));
        coverage = smoothstep(1.0 - progress - softness,
            1.0 - progress + softness, luma);
    }
    else
    {
        // 縦横比を補正しないと形が潰れる。
        const float aspect = max(target_size.x, 1.0) / max(target_size.y, 1.0);
        float2 delta = input.uv - center;
        delta.x *= aspect;
        const float2 rotated = float2(
            delta.x * cos(rotation) - delta.y * sin(rotation),
            delta.x * sin(rotation) + delta.y * cos(rotation));
        const float radius = length(rotated) * 2.0;
        // uv の y は下向きなので、形状の上下を合わせるため反転する。
        const float theta = atan2(-rotated.y, rotated.x);
        const float boundary = max(shape_boundary(shape, theta, points, inner), 0.0001);
        // 中心がずれても角まで届く大きさを採る。
        const float corner = 2.0 * length(float2(max(center.x, 1.0 - center.x) * aspect,
            max(center.y, 1.0 - center.y)));
        const float scale = progress * corner * 1.1;
        coverage = 1.0 - smoothstep(scale * boundary - softness,
            scale * boundary + softness, radius);
    }

    // progress が 1 のとき必ず覆いきる。星やハートは谷が深く隙間が残るので、
    // 泡マスクと同じ 0.85 から寄せて最後の切り替わりを目立たせない。
    coverage = max(coverage, smoothstep(0.85, 1.0, progress));
    color.a *= saturate(1.0 - saturate(coverage) * intensity);
    return color;
}
