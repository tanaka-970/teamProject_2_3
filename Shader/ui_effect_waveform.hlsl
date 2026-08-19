Texture2D source_texture : register(t0);
SamplerState source_sampler : register(s0);

cbuffer UIEffectConstants : register(b0)
{
    float4 effect_color;
    float4 effect_params0; // wavelength, thickness, feather, amplitude
    float4 effect_params1; // angle, wobble, sharpness, speed
    float4 effect_params2; // direction.xy, seed, time
    float4 target_size;    // width, height, 1 / width, 1 / height
    float4 effect_color_2;
    float4 effect_color_3;
    float4 effect_color_4;
    float4 effect_color_stops;
    // 宣言順どおり c9 に来るため packoffset は付けない。
    // 1 つだけ付けると X3530（packoffset の混在）でコンパイルが止まる。
    float4 effect_params3;  // x = 波形の種類

};

struct VSOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

static const float two_pi = 6.28318530718;

float4 main(VSOutput input) : SV_TARGET
{
    const float angle = effect_params1.x * 0.01745329252;
    const float2 axis = float2(cos(angle), sin(angle));
    const float2 perp = float2(-axis.y, axis.x);

    const float2 px = input.uv * target_size.xy;
    const float wavelength = max(effect_params0.x, 1.0);
    const float speed = effect_params1.w;
    const float phase = dot(px, axis) / wavelength * two_pi -
        effect_params2.w * speed + effect_params2.z;

    const int waveform = (int)floor(clamp(effect_params3.x, 0.0, 4.0) + 0.5);
    float wave = sin(phase);
    if (waveform == 1)
    {
        wave = (2.0 / 3.14159265359) * asin(sin(phase));
    }
    else if (waveform == 2)
    {
        wave = 2.0 * frac(phase / two_pi) - 1.0;
    }
    else if (waveform == 3)
    {
        const float cycle = frac(phase / two_pi);
        const float valley_direct = abs(cycle - 0.42);
        const float peak_direct = abs(cycle - 0.50);
        const float tail_direct = abs(cycle - 0.61);
        const float valley_distance = min(valley_direct, 1.0 - valley_direct);
        const float peak_distance = min(peak_direct, 1.0 - peak_direct);
        const float tail_distance = min(tail_direct, 1.0 - tail_direct);
        const float valley_normalized = valley_distance / 0.045;
        const float peak_normalized = peak_distance / 0.022;
        const float tail_normalized = tail_distance / 0.070;
        const float valley = exp(-valley_normalized * valley_normalized) * 0.35;
        const float peak = exp(-peak_normalized * peak_normalized) * 1.25;
        const float tail = exp(-tail_normalized * tail_normalized) * 0.18;
        wave = (peak + tail - valley) / 1.25;
    }
    else if (waveform == 4)
    {
        const float position = phase / two_pi;
        const float cell = floor(position);
        const float local = frac(position);
        const float blend = smoothstep(0.0, 1.0, local);
        const float seed_offset = effect_params2.z * 17.0;
        const float a = frac(sin((cell + seed_offset) * 127.1) * 43758.5453123);
        const float b = frac(sin((cell + 1.0 + seed_offset) * 127.1) * 43758.5453123);
        wave = lerp(a, b, blend) * 2.0 - 1.0;
    }
    wave = clamp(wave, -1.0, 1.0);
    const float exponent = 1.0 + 3.0 * saturate(effect_params1.z);
    wave = sign(wave) * pow(abs(wave), exponent);

    const float wobble = saturate(effect_params1.y);
    const float low_phase = dot(px, axis) / (wavelength * 6.0) * two_pi -
        effect_params2.w * speed * 0.35 + effect_params2.z;
    wave += sin(low_phase) * wobble;

    float range_a = saturate(min(effect_params2.x, effect_params2.y));
    float range_b = saturate(max(effect_params2.x, effect_params2.y));
    if (range_b <= range_a)
    {
        range_a = 0.0;
        range_b = 1.0;
    }
    const float t = saturate(dot(input.uv - 0.5, axis) + 0.5);
    const float feather = max(effect_params0.z, 0.0001);
    const float envelope = smoothstep(range_a, range_a + feather, t) *
        (1.0 - smoothstep(range_b - feather, range_b, t));
    const float displacement_wave = wave * envelope;

    const float2 center_px = target_size.xy * 0.5;
    const float2 from_center = px - center_px;
    const float axis_distance = dot(from_center, axis);
    const float perp_distance = dot(from_center, perp);
    const float modulation = saturate(0.5 + 0.5 * wave);
    const float thickness_scale = max(0.05,
        1.0 - saturate(effect_params0.y) * envelope * (1.0 - modulation));
    const float2 thickened_px = center_px + axis * axis_distance +
        perp * (perp_distance / thickness_scale);
    const float2 thickened_uv = thickened_px * target_size.zw;

    const float amplitude = max(effect_params0.w, 0.0);
    const float2 src_uv = thickened_uv - perp * displacement_wave * amplitude * target_size.zw;

    float4 result = float4(0.0, 0.0, 0.0, 0.0);
    const bool inside = src_uv.x >= 0.0 && src_uv.x <= 1.0 &&
        src_uv.y >= 0.0 && src_uv.y <= 1.0;
    if (inside)
    {
        result = source_texture.Sample(source_sampler, src_uv);
    }
    return result;
}
