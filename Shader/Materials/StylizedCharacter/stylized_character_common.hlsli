// スタイライズドキャラクターの肌・顔影・髪・リム・結晶表現を合成する共通処理。

cbuffer CHARACTER_MATERIAL_CONSTANTS : register(b11)
{
    float4 skin_tint;
    float4 skin_shadow_tint;
    float4 face_shadow_tint;
    float4 hair_highlight_color;
    float4 rim_color;
    float4 crystal_tint;
    float4 general_params;
    float4 skin_params;
    float4 face_params;
    float4 hair_params;
    float4 rim_params;
    float4 crystal_params;
    float4 artistic_top_color;
    float4 artistic_bottom_color;
    float4 artistic_params;
    float4 gradient_params;
    float4 specular_color;
    float4 specular_params;
};

float3 shift_hue(float3 color, float shift)
{
    const float3x3 rgb_to_yiq = float3x3(
        0.299f, 0.587f, 0.114f,
        0.596f, -0.275f, -0.321f,
        0.212f, -0.523f, 0.311f);
    const float3x3 yiq_to_rgb = float3x3(
        1.0f, 0.956f, 0.621f,
        1.0f, -0.272f, -0.647f,
        1.0f, -1.106f, 1.703f);
    float3 yiq = mul(rgb_to_yiq, color);
    float angle = shift * 6.2831853f;
    float hue = atan2(yiq.z, yiq.y) + angle;
    float chroma = length(yiq.yz);
    yiq.yz = float2(cos(hue), sin(hue)) * chroma;
    return max(mul(yiq_to_rgb, yiq), 0.0f);
}

float3 stylized_character_shade(float3 base, float3 normal, float3 tangent,
    float3 world_position)
{
    float3 N = normalize(normal);
    float3 T = normalize(tangent - N * dot(N, tangent));
    float3 L = normalize(-light_direction.xyz);
    float3 V = normalize(camera_position.xyz - world_position);
    float3 H = normalize(L + V);

    float threshold = general_params.x;
    float softness = max(general_params.y + skin_params.z * skin_params.w +
        face_params.y * face_params.w, 0.001f);
    float shadow_strength = general_params.z;
    float saturation = general_params.w;

    float wrap = skin_params.x * skin_params.w;
    float wrapped_light = saturate((dot(N, L) + wrap) / (1.0f + wrap));
    float face_bias = face_params.x * face_params.w;
    float toon_light = smoothstep(threshold - softness, threshold + softness,
        wrapped_light + face_bias);
    float bands = max(artistic_params.x, 1.0f);
    toon_light = bands > 1.0f ? round(toon_light * (bands - 1.0f)) / (bands - 1.0f)
        : toon_light;
    float3 shadow_color = lerp(skin_shadow_tint.rgb, face_shadow_tint.rgb, face_params.w);
    float3 color = base * lerp(shadow_color, skin_tint.rgb,
        lerp(1.0f - shadow_strength, 1.0f, toon_light));

    float back_scatter = pow(saturate(dot(-N, L)), 3.0f) * skin_params.y * skin_params.w;
    color += skin_tint.rgb * back_scatter;
    color += base * face_params.z * face_params.w;

    float tangent_half = saturate(1.0f - abs(dot(T, H)));
    float hair_specular = pow(tangent_half, max(hair_params.x, 1.0f));
    hair_specular = smoothstep(1.0f - max(hair_params.z, 0.01f), 1.0f, hair_specular);
    color += hair_highlight_color.rgb * hair_specular * hair_params.y * hair_params.w;

    float ordinary_specular = pow(saturate(dot(N, H)), max(specular_params.x, 1.0f));
    ordinary_specular = smoothstep(specular_params.y,
        min(specular_params.y + 0.12f, 1.0f), ordinary_specular);
    color += specular_color.rgb * ordinary_specular * specular_params.z * specular_params.w;

    float fresnel = pow(1.0f - saturate(dot(N, V)), max(rim_params.x, 0.1f));
    float rim = smoothstep(rim_params.y, min(rim_params.y + 0.25f, 1.0f), fresnel);
    color += rim_color.rgb * rim * rim_params.z * rim_params.w;

    float crystal_enabled = crystal_tint.a;
    float crystal_fresnel = pow(1.0f - saturate(dot(N, V)), max(crystal_params.y, 0.1f));
    float crystal_amount = crystal_params.x * crystal_enabled;
    color = lerp(color, color * crystal_tint.rgb + crystal_tint.rgb * crystal_fresnel,
        crystal_amount);
    color += crystal_tint.rgb * crystal_params.w * crystal_enabled;

    float gradient = saturate(world_position.y * gradient_params.x + gradient_params.y);
    float3 gradient_color = lerp(artistic_bottom_color.rgb, artistic_top_color.rgb, gradient);
    color = lerp(color, color * gradient_color, artistic_params.w);
    color = (color - 0.5f) * artistic_params.y + 0.5f;
    color = shift_hue(color, artistic_params.z);

    float luminance = dot(color, float3(0.299f, 0.587f, 0.114f));
    return max(lerp(luminance.xxx, color, saturation), 0.0f);
}
