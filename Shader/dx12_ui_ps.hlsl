cbuffer UIConstants : register(b0)
{
    float4 screen_size;
    float4 fill_color_2;
    // x = shape kind, y = text/SDF flag, z = stroke width, w = reserved.
    float4 mode;
    float4 outline_color;
    float4 shadow_offset;
    float4 shadow_color;
    float4 atlas_size;
    float4 fill_parameters;
    float4 clip_parameters;
    float4 clip_bounds;
    float4 mask_parameters;
    float4 mask_uv;
    float4 mask_uvs[4];
    float4 mask_operations;
    float4 mask_luma;
};

Texture2D ui_texture : register(t0);
Texture2D ui_mask_0 : register(t1);
Texture2D ui_mask_1 : register(t2);
Texture2D ui_mask_2 : register(t3);
Texture2D ui_mask_3 : register(t4);
SamplerState ui_sampler : register(s0);

struct PSInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
    float4 uv_bounds : TEXCOORD1;
};

float4 SampleUiMask(int index, float2 uv)
{
    if (index == 0) return ui_mask_0.Sample(ui_sampler, uv);
    if (index == 1) return ui_mask_1.Sample(ui_sampler, uv);
    if (index == 2) return ui_mask_2.Sample(ui_sampler, uv);
    return ui_mask_3.Sample(ui_sampler, uv);
}

float4 main(PSInput input) : SV_TARGET
{
    float clip_alpha = 1.0;
    if (clip_parameters.x > 0.5)
    {
        float2 clip_size = max(clip_bounds.zw - clip_bounds.xy,
            float2(0.0001, 0.0001));
        float2 clip_local = (input.position.xy - clip_bounds.xy) / clip_size;
        float signed_distance = 1.0;
        if (clip_parameters.x < 1.5)
        {
            signed_distance = 1.0 - length((clip_local - 0.5) / 0.5);
        }
        else
        {
            float radius = saturate(clip_parameters.w);
            float2 q = abs(clip_local - 0.5) - (0.5 - radius);
            signed_distance = radius - length(max(q, 0.0)) - max(q.x, q.y);
        }
        float feather = max(clip_parameters.z / max(clip_size.x, clip_size.y),
            0.0001);
        clip_alpha = smoothstep(0.0, feather, signed_distance);
        if (clip_parameters.y > 0.5) clip_alpha = 1.0 - clip_alpha;
        if (clip_alpha <= 0.0001) discard;
    }

    float2 sample_uv = input.uv;
    float4 sampled = ui_texture.Sample(ui_sampler, sample_uv);

    // Shape batches use the reserved white texture and keep their geometry in
    // the same command path as images. Circle is the first non-rectangular
    // variant; polygon/path geometry is already tessellated on the CPU.
    if (mode.x == 1.0)
    {
        float2 centered = input.uv * 2.0 - 1.0;
        float distance_to_edge = 1.0 - length(centered);
        float aa = max(fwidth(distance_to_edge), 0.0001);
        sampled.a *= smoothstep(0.0, aa, distance_to_edge);
    }

    float4 shaded = sampled * input.color;
    float2 mask_local_uv = saturate((input.uv - input.uv_bounds.xy) /
        max(input.uv_bounds.zw - input.uv_bounds.xy, float2(0.0001, 0.0001)));
    float mask_alpha = 1.0;
    const int mask_count = min((int)mask_parameters.x, 4);
    [unroll]
    for (int mask_index = 0; mask_index < 4; ++mask_index)
    {
        if (mask_index >= mask_count) continue;
        const float2 matte_uv = mask_uvs[mask_index].xy + mask_local_uv *
            mask_uvs[mask_index].zw;
        const float4 mask_sample = SampleUiMask(mask_index, matte_uv);
        const float matte_value = mask_luma[mask_index] > 0.5
            ? dot(mask_sample.rgb, float3(0.2126, 0.7152, 0.0722)) : mask_sample.a;
        if (mask_index == 0)
        {
            mask_alpha = matte_value;
        }
        else if (mask_operations[mask_index] < 0.5)
        {
            mask_alpha = saturate(mask_alpha + matte_value);
        }
        else if (mask_operations[mask_index] < 1.5)
        {
            mask_alpha = saturate(mask_alpha - matte_value);
        }
        else
        {
            mask_alpha = min(mask_alpha, matte_value);
        }
    }
    if (mask_parameters.y > 0.5) mask_alpha = 1.0 - mask_alpha;
    shaded.a *= mask_alpha;
    shaded.a *= clip_alpha;

    if (mode.y > 0.5)
    {
        float distance_value = sampled.a;
        float width = max(fwidth(distance_value), 0.0005);
        float glyph_alpha = smoothstep(0.5 - width, 0.5 + width, distance_value);
        float outline_alpha = 0.0;
        if (mode.z > 0.0)
        {
            outline_alpha = smoothstep(0.5 - width - mode.z * 0.02,
                0.5 - width, distance_value);
        }
        float4 text_color = lerp(outline_color, input.color, glyph_alpha);
        text_color.a = max(glyph_alpha * input.color.a,
            outline_alpha * outline_color.a);
        text_color.a *= mask_alpha * clip_alpha;
        return text_color;
    }

    if (fill_parameters.w > 0.5)
    {
        float2 bounds_size = max(input.uv_bounds.zw - input.uv_bounds.xy,
            float2(0.0001, 0.0001));
        float2 local = saturate((input.uv - input.uv_bounds.xy) / bounds_size);
        float gradient = 0.0;
        if (fill_parameters.w > 1.5)
        {
            gradient = saturate(distance(local, fill_parameters.yz) * 1.41421356);
        }
        else
        {
            float2 direction = float2(cos(fill_parameters.x), sin(fill_parameters.x));
            gradient = saturate(dot(local - fill_parameters.yz, direction) + 0.5);
        }
        shaded = sampled * lerp(input.color, fill_color_2, gradient);
    }

    return shaded;
}
