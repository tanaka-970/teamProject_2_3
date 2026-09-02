Texture2D source_texture : register(t0);
Texture2D mask_texture : register(t1);
SamplerState source_sampler : register(s0);

cbuffer UIEffectConstants : register(b0)
{
    float4 effect_color;
    float4 effect_params0; // radius, intensity, threshold, grain_size
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
    value = frac(value * float2(123.34, 456.21));
    value += dot(value, value + 45.32);
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
    const float grain = max(effect_params0.w, 1.0);
    float n;
    if (effect_params3.y > 0.5)
    {
        const float texture_noise = dot(mask_texture.Sample(source_sampler,
            PatternUV(input.uv)).rgb, float3(0.2126, 0.7152, 0.0722));
        n = texture_noise * 2.0 - 1.0;
    }
    else
    {
        const float2 cell = floor(input.uv * target_size.xy / grain);
        n = Hash(cell + effect_params2.z + effect_params1.w * effect_params2.w) *
            2.0 - 1.0;
    }
    color.rgb += n * effect_params0.y * effect_color.rgb;
    // Scene Effect の RT は HDR なので 1.0 で丸めず、負値だけ止める。
    return float4(max(color.rgb, 0.0f), saturate(color.a));
}
