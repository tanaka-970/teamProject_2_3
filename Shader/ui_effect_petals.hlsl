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

float fx_hash(float2 v, float seed)
{
    return frac(sin(dot(v, float2(127.1, 311.7)) + seed) * 43758.5453);
}

float fx_luma(float3 c)
{
    return dot(c, float3(0.2126, 0.7152, 0.0722));
}

float4 main(VSOutput input) : SV_TARGET
{
    float4 color = source_texture.Sample(source_sampler, input.uv);
    const float density = max(floor(effect_params0.x), 1.0);
    const float intensity = saturate(effect_params0.y);
    const float size = max(effect_params0.w, 0.0001);
    const float amount = saturate(effect_params1.y);
    const float softness = max(effect_params1.z, 0.0001);
    const float speed = effect_params1.w;
    const float drift = effect_params1.x;
    const float seed = effect_params2.z;
    const float time = effect_params2.w;

    const float aspect = max(target_size.x, 1.0) / max(target_size.y, 1.0);
    float coverage = 0.0;

    // 2 層で奥行きを出す。奥ほど小さく遅い。
    [unroll]
    for (int layer = 0; layer < 2; ++layer)
    {
        const float depth = 1.0 - float(layer) * 0.45;
        const float cells = density * (1.0 + float(layer) * 0.6);
        float2 p = float2(input.uv.x * aspect, input.uv.y);
        // 落下と横流れ。速度 0 なら完全に静止する。
        p.y -= time * speed * 0.12 * depth;
        p.x -= sin(time * speed * 0.5 + float(layer) * 2.1) * drift * 0.1;
        const float2 grid = p * cells;
        const float2 index = floor(grid);
        const float2 local = frac(grid) - 0.5;

        const float present = fx_hash(index, seed + float(layer) * 37.0);
        if (present > amount) continue;

        const float2 jitter = float2(
            fx_hash(index, seed + 11.0 + float(layer)) - 0.5,
            fx_hash(index, seed + 23.0 + float(layer)) - 0.5) * 0.6;
        const float radius = size * depth * (0.6 + present * 0.8);
        // 花びらは楕円にして向きを乱数で変える。
        const float spin = fx_hash(index, seed + 47.0) * 6.2831853;
        float2 d = local - jitter;
        d = float2(d.x * cos(spin) - d.y * sin(spin),
                   d.x * sin(spin) + d.y * cos(spin));
        d.x *= 1.8;
        const float shape = length(d) / max(radius, 0.0001);
        coverage = max(coverage, 1.0 - smoothstep(1.0 - softness, 1.0, shape));
    }

    coverage = saturate(coverage) * intensity;
    color.rgb = lerp(color.rgb, effect_color.rgb, coverage * effect_color.a);
    color.a = max(color.a, coverage * effect_color.a);
    return color;
}
