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
    float4 effect_params3; // x waveform, y mask exists, z luma matte, w invert matte
    float4 brush_pattern_settings;
    float4 brush_pattern_weights[4];
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

float4 main(VSOutput input) : SV_TARGET
{
    float4 color = source_texture.Sample(source_sampler, input.uv);
    const int shape_kind = (int)round(effect_params3.x) - 1;
    float edge = 1.0;
    if (shape_kind == -2)
    {
        // Runtime の自由図形マスク。輪郭は別RTの alpha に描かれているため、
        // ここでは矩形／円などの手続き形状を重ねず、mask_texture だけを使う。
        edge = 1.0;
    }
    else if (shape_kind >= 0)
    {
        const float2 centered = input.uv - effect_params2.xy;
        const float angle = radians(effect_params1.x);
        const float2 axis_x = float2(cos(angle), sin(angle));
        const float2 axis_y = float2(-axis_x.y, axis_x.x);
        const float2 local = float2(dot(centered, axis_x), dot(centered, axis_y)) /
            max(effect_params2.zw, float2(0.0001f, 0.0001f));
        const float radius = length(local);
        const float sides = max(round(effect_params0.z), 3.0);
        const float pi = 3.14159265359;
        float signed_distance = 0.0;

        if (shape_kind == 0)
        {
            signed_distance = 1.0 - max(abs(local.x), abs(local.y));
        }
        else if (shape_kind == 1)
        {
            signed_distance = 1.0 - radius;
        }
        else if (shape_kind == 2)
        {
            const float sector = 2.0 * pi / sides;
            const float local_angle =
                fmod(abs(atan2(local.y, local.x)) + pi / sides, sector) - pi / sides;
            const float boundary = cos(pi / sides) / max(cos(local_angle), 0.0001);
            signed_distance = boundary - radius;
        }
        else if (shape_kind == 3)
        {
            const float lobe_sector = pi / sides;
            const float local_angle = fmod(abs(atan2(local.y, local.x)),
                2.0 * lobe_sector);
            const float lobe = 1.0 - abs(local_angle - lobe_sector) / lobe_sector;
            const float inner_radius = saturate(effect_params0.x);
            const float boundary = lerp(inner_radius, 1.0, lobe);
            signed_distance = boundary - radius;
        }
        else
        {
            const float corner = saturate(effect_params0.y);
            const float2 q = abs(local) - float2(1.0 - corner, 1.0 - corner);
            signed_distance = corner - length(max(q, 0.0)) - min(max(q.x, q.y), 0.0);
        }

        const float softness = max(effect_params1.z, 0.0001);
        edge = smoothstep(-softness, softness, signed_distance);
    }
    else
    {
        // 既存の通常 Mask 用。矩形と円を amount で混ぜる挙動を維持する。
        const float2 centered = input.uv - effect_params2.xy;
        const float angle = radians(effect_params1.x);
        const float2 axis_x = float2(cos(angle), sin(angle));
        const float2 axis_y = float2(-axis_x.y, axis_x.x);
        const float2 local = float2(dot(centered, axis_x), dot(centered, axis_y)) /
            max(effect_params2.zw, float2(0.0001f, 0.0001f));
        const float rectangle = max(abs(local.x), abs(local.y));
        const float circle = length(local);
        const float shape = lerp(rectangle, circle, saturate(effect_params0.w));
        edge = saturate((1.0 - shape) / max(effect_params1.z, 0.0001));
    }
    color.a *= edge;
    if (effect_params1.w > 0.5f)
    {
        const float4 mask_sample = mask_texture.Sample(source_sampler, input.uv);
        float mask_value = mask_sample.a;
        if (effect_params3.z > 0.5f)
            mask_value = dot(mask_sample.rgb, float3(0.2126f, 0.7152f, 0.0722f));
        if (effect_params3.w > 0.5f)
            mask_value = 1.0f - mask_value;
        color.a *= smoothstep(0.0f, max(effect_params1.z, 0.0001f), mask_value);
    }
    return color;
}
