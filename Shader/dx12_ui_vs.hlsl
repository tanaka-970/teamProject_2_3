cbuffer UIConstants : register(b0)
{
    float4 screen_size : packoffset(c0);
    row_major float4x4 world_canvas_matrix : packoffset(c44);
    row_major float4x4 world_view_projection : packoffset(c48);
    float4 world_canvas_parameters : packoffset(c52);
    float4 world_viewport : packoffset(c53);
};

struct VSInput
{
    float2 position : POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
    float4 uv_bounds : TEXCOORD1;
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
    float4 uv_bounds : TEXCOORD1;
};

VSOutput main(VSInput input)
{
    VSOutput output;
    if (world_canvas_parameters.x > 0.5)
    {
        const float2 viewport_size = max(world_viewport.zw, float2(1.0, 1.0));
        const float2 normalized = (input.position - world_viewport.xy) /
            viewport_size;
        const float3 canvas_position = float3(
            (normalized.x - 0.5) * world_canvas_parameters.y,
            (0.5 - normalized.y) * world_canvas_parameters.z, 0.0);
        const float4 world_position = mul(float4(canvas_position, 1.0),
            world_canvas_matrix);
        output.position = mul(world_position, world_view_projection);
    }
    else
    {
        const float2 safe_size = max(screen_size.xy, float2(1.0, 1.0));
        const float2 ndc = float2(input.position.x / safe_size.x * 2.0 - 1.0,
            1.0 - input.position.y / safe_size.y * 2.0);
        output.position = float4(ndc, 1.0, 1.0);
    }
    output.uv = input.uv;
    output.color = input.color;
    output.uv_bounds = input.uv_bounds;
    return output;
}
