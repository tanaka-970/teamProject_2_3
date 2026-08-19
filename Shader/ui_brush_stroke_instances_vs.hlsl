Texture2D source_texture : register(t0);

struct BrushInstance
{
    float2 center;
    float2 size;
    uint pattern;
    float padding;
};

StructuredBuffer<BrushInstance> brush_instances : register(t1);
SamplerState source_sampler : register(s0);

cbuffer UIEffectConstants : register(b0)
{
    float4 effect_color;
    float4 effect_params0;
    float4 effect_params1;
    float4 effect_params2;
    float4 target_size;
    float4 effect_color_2;
    float4 effect_color_3;
    float4 effect_color_4;
    float4 effect_color_stops;
    float4 effect_params3;
    float4 brush_pattern_settings;
    float4 brush_pattern_weights[4];
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float2 local_uv : TEXCOORD0;
    float2 center_uv : TEXCOORD1;
    float2 tangent : TEXCOORD2;
    float2 stroke_size : TEXCOORD3;
    nointerpolation uint pattern : TEXCOORD4;
};

float Luminance(float2 uv)
{
    return dot(source_texture.SampleLevel(source_sampler, uv, 0).rgb,
        float3(0.2126, 0.7152, 0.0722));
}

VSOutput main(uint vertex_id : SV_VertexID, uint instance_id : SV_InstanceID)
{
    static const float2 corners[4] =
    {
        float2(-0.5, -0.5), float2(-0.5, 0.5),
        float2(0.5, -0.5), float2(0.5, 0.5)
    };
    const BrushInstance instance = brush_instances[instance_id];
    const float2 center_uv = instance.center * target_size.zw;
    const float gx = Luminance(center_uv + float2(target_size.z, 0.0)) -
        Luminance(center_uv - float2(target_size.z, 0.0));
    const float gy = Luminance(center_uv + float2(0.0, target_size.w)) -
        Luminance(center_uv - float2(0.0, target_size.w));
    float2 tangent = float2(-gy, gx);
    const float tangent_length = length(tangent);
    tangent = tangent_length > 0.0001 ? tangent / tangent_length : float2(1.0, 0.0);
    const float2 normal = float2(-tangent.y, tangent.x);
    const float2 corner = corners[vertex_id];
    const float2 pixel = instance.center + tangent * (corner.x * instance.size.x) +
        normal * (corner.y * instance.size.y);

    VSOutput output;
    output.position = float4(pixel.x * target_size.z * 2.0 - 1.0,
        1.0 - pixel.y * target_size.w * 2.0, 0.0, 1.0);
    output.local_uv = corner + 0.5;
    output.center_uv = center_uv;
    output.tangent = tangent;
    output.stroke_size = instance.size;
    output.pattern = instance.pattern;
    return output;
}
