Texture2D source_texture : register(t0);
Texture2D matte_texture : register(t1);
SamplerState source_sampler : register(s0);

cbuffer UIEffectConstants : register(b0)
{
    float4 effect_color;
    float4 effect_params0; // rim width, intensity, threshold, refraction amount
    float4 effect_params1; // light angle, dispersion, fresnel, speed
    float4 effect_params2; // center.xy, seed, time
    float4 target_size;
    float4 effect_color_2;
    float4 effect_color_3;
    float4 effect_color_4;
    float4 effect_color_stops;
    float4 effect_params3;
    float4 brush_pattern_settings;
    float4 brush_pattern_weights[4];
};

struct VSOutput { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };

float4 sample_safe(float2 uv)
{
    if (any(uv < 0.0) || any(uv > 1.0)) return 0.0;
    return source_texture.Sample(source_sampler, uv);
}

float SurfaceAt(float2 uv)
{
    uv = saturate(uv);
    const float matte = matte_texture.Sample(source_sampler, uv).a;
    const float4 value = source_texture.Sample(source_sampler, uv);
    const float fallback = value.a < 0.999 ? value.a :
        dot(value.rgb, float3(0.2126, 0.7152, 0.0722));
    return lerp(fallback, matte, step(0.5, effect_params3.y));
}

float4 main(VSOutput input) : SV_TARGET
{
    const float4 source = source_texture.Sample(source_sampler, input.uv);
    const float rim_width = max(effect_params0.x, 0.5);
    const float2 texel = target_size.zw * rim_width;
    const float surface_left = SurfaceAt(input.uv - float2(texel.x, 0.0));
    const float surface_right = SurfaceAt(input.uv + float2(texel.x, 0.0));
    const float surface_up = SurfaceAt(input.uv - float2(0.0, texel.y));
    const float surface_down = SurfaceAt(input.uv + float2(0.0, texel.y));
    const float surface_up_left = SurfaceAt(input.uv - texel);
    const float surface_up_right = SurfaceAt(input.uv + float2(texel.x, -texel.y));
    const float surface_down_left = SurfaceAt(input.uv + float2(-texel.x, texel.y));
    const float surface_down_right = SurfaceAt(input.uv + texel);
    const float2 gradient = float2(
        (surface_left - surface_right) * 2.0 + surface_up_left +
            surface_down_left - surface_up_right - surface_down_right,
        (surface_up - surface_down) * 2.0 + surface_up_left +
            surface_up_right - surface_down_left - surface_down_right) * 0.25;
    const float gradient_length = length(gradient);
    const float2 normal = gradient_length > 0.0001
        ? gradient / gradient_length : float2(0.0, 0.0);
    const float edge = saturate(gradient_length * 2.5);

    // A local refraction surface: the alpha edge supplies a stable 2D normal,
    // so the effect also works for transparent logos, text and sprites.
    const float refraction = max(effect_params0.w, 0.0) * edge;
    const float2 offset = normal * refraction * target_size.zw;
    const float dispersion = saturate(effect_params1.y) * 0.35;
    const float2 base_uv = input.uv + offset;
    const float red = sample_safe(base_uv + offset * dispersion).r;
    const float green = sample_safe(base_uv).g;
    const float blue = sample_safe(base_uv - offset * dispersion).b;
    float3 refracted = float3(red, green, blue);

    const float angle = radians(effect_params1.x);
    const float2 light = float2(cos(angle), sin(angle));
    const float directional = saturate(dot(normal, light));
    const float fresnel = pow(saturate(edge * 0.75 + effect_params1.z * 0.35),
        lerp(3.0, 0.65, saturate(effect_params1.z)));
    const float rim = edge * (0.20 + directional * 0.80) *
        (0.25 + fresnel * 0.75);
    const float glass_mix = saturate(effect_params0.y);
    refracted = lerp(refracted, refracted * effect_color.rgb,
        saturate(effect_color.a));

    float4 result = source;
    result.rgb = lerp(source.rgb, refracted, glass_mix);
    result.rgb += effect_color_2.rgb * rim * glass_mix * effect_color_2.a;
    result.a = source.a;
    return result;
}
