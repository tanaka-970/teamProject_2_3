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

float bubble_hash(float2 cell, float seed)
{
    return frac(sin(dot(cell, float2(127.1, 311.7)) + seed) * 43758.5453);
}

float4 main(VSOutput input) : SV_TARGET
{
    float4 color = source_texture.Sample(source_sampler, input.uv);
    const float progress = saturate(effect_params1.y);
    const float softness = max(effect_params1.z, 0.0001);
    const float seed = effect_params2.z;
    // 泡の細かさ。radius が小さいほど泡が大きくなる。
    const float density = max(effect_params0.x, 1.0);
    // 縦横比を保たないと泡が楕円になる。
    const float aspect = max(target_size.x, 1.0) / max(target_size.y, 1.0);
    const float2 grid = float2(density * aspect, density);
    const float2 scaled = input.uv * grid;
    const float2 baseCell = floor(scaled);

    // 泡は湧く時刻がばらつく。最大の被覆量を採る。
    float coverage = 0.0;
    [unroll] for (int y = -1; y <= 1; ++y)
    {
        [unroll] for (int x = -1; x <= 1; ++x)
        {
            const float2 cell = baseCell + float2(x, y);
            const float2 jitter = float2(bubble_hash(cell, seed),
                bubble_hash(cell, seed + 17.0));
            const float2 center = (cell + 0.15 + jitter * 0.7) / grid;
            // 湧き始めの遅れ。全部が同時に膨らむと面白くない。
            const float delay = bubble_hash(cell, seed + 41.0) * 0.65;
            const float grow = saturate((progress - delay) / max(1.0 - delay, 0.0001));
            if (grow <= 0.0) continue;
            // 泡ごとに最終半径を散らす。
            const float scaleVariation = 0.65 + bubble_hash(cell, seed + 73.0) * 0.7;
            const float radius = grow * scaleVariation * 0.9 / density;
            float2 delta = input.uv - center;
            delta.y /= max(aspect, 0.0001);
            const float distance = length(delta * float2(aspect, 1.0));
            coverage = max(coverage,
                1.0 - smoothstep(radius - softness * radius, radius, distance));
        }
    }

    // progress が 1 のとき必ず覆いきる。泡だけだと隙間が残る。
    coverage = max(coverage, smoothstep(0.85, 1.0, progress));
    color.a *= saturate(1.0 - coverage);
    return color;
}
