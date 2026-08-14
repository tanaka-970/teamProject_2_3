Texture2D source_texture : register(t0);
Texture2D mask_texture : register(t1);
SamplerState source_sampler : register(s0);

cbuffer UIEffectConstants : register(b0)
{
    float4 effect_color;
    float4 effect_params0; // radius, intensity, edge_width, amount
    float4 effect_params1; // angle, progress, softness, speed
    float4 effect_params2; // direction.xy, seed, time
    float4 target_size;    // width, height, 1 / width, 1 / height
    float4 effect_color_2;
    float4 effect_color_3;
    float4 effect_color_4;
    float4 effect_color_stops;
    float4 effect_params3; // x = waveform, y = optional texture is bound
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

float Hash(float2 value)
{
    value = frac(value * float2(127.1, 311.7));
    value += dot(value, value + 19.19);
    return frac(value.x * value.y);
}

float2 PatternUV(float2 uv)
{
    const float rotation_radians = radians(effect_params1.x);
    float sine;
    float cosine;
    sincos(rotation_radians, sine, cosine);
    const float2 local = (uv - 0.5) * target_size.xy /
        max(effect_params0.x, 1.0);
    const float2 rotated = float2(
        local.x * cosine - local.y * sine,
        local.x * sine + local.y * cosine);
    return frac(rotated + 0.5);
}

float4 main(VSOutput input) : SV_TARGET
{
    float4 color = source_texture.Sample(source_sampler, input.uv);
    const float progress = saturate(effect_params1.y);
    const float edge = max(effect_params0.z, 0.0001);
    float keep;
    if (effect_params3.y > 0.5)
    {
        // テクスチャの明るい所から先に消える。未指定時の Hash 経路は下で
        // 既存式のまま残し、従来シーンの絵を変えない。
        const float pattern = dot(mask_texture.Sample(source_sampler,
            PatternUV(input.uv)).rgb, float3(0.2126, 0.7152, 0.0722));
        keep = 1.0 - smoothstep(1.0 - progress - edge,
            1.0 - progress + edge, pattern);
    }
    else
    {
        const float noise = Hash(floor(input.uv * target_size.xy * 0.25) +
            effect_params2.z);
        keep = smoothstep(progress - edge, progress + edge, noise);
    }
    const float border = 1.0 - abs(keep * 2.0 - 1.0);
    color.rgb = lerp(color.rgb, effect_color.rgb, border * effect_color.a);
    color.a *= keep;
    return color;
}
