cbuffer ui_constants : register(b0)
{
    float4 screen_size;
    row_major float4x4 world_canvas_matrix;
    row_major float4x4 world_view_projection;
    float4 world_canvas_params;
};

struct VS_INPUT
{
    float2 position : POSITION;
    float2 uv       : TEXCOORD0;
    float2 gradient_uv : TEXCOORD1;
    float4 color    : COLOR0;
};

struct VS_OUTPUT
{
    float4 position : SV_POSITION;
    float2 uv       : TEXCOORD0;
    float2 gradient_uv : TEXCOORD1;
    float4 color    : COLOR0;
};

VS_OUTPUT main(VS_INPUT input)
{
    VS_OUTPUT output;
    if (world_canvas_params.x > 0.5)
    {
        const float2 normalized = input.position.xy /
            max(screen_size.xy, float2(1.0, 1.0));
        const float3 canvas_position = float3(
            (normalized.x - 0.5) * world_canvas_params.y,
            (0.5 - normalized.y) * world_canvas_params.z,
            0.0);
        const float4 world_position = mul(
            float4(canvas_position, 1.0), world_canvas_matrix);
        output.position = mul(world_position, world_view_projection);
    }
    else
    {
        const float2 inv_screen = float2(1.0, 1.0) /
            max(screen_size.xy, float2(1.0, 1.0));
        output.position = float4(
            input.position.x * inv_screen.x * 2.0 - 1.0,
            1.0 - input.position.y * inv_screen.y * 2.0,
            0.0,
            1.0);
    }
    output.uv = input.uv;
    output.gradient_uv = input.gradient_uv;
    output.color = input.color;
    return output;
}
