struct RenderItemData
{
    row_major float4x4 world;
    float4 tint;
    uint owner_low;
    uint owner_high;
    uint flags;
    uint reserved;
};
cbuffer FrameConstants : register(b0)
{
    row_major float4x4 view_projection;
    float4 camera_position;
    float4 time_parameters;
};
StructuredBuffer<RenderItemData> render_items : register(t0);
struct VertexInput { float3 position : POSITION; float4 color : COLOR; };
struct VertexOutput { float4 position : SV_POSITION; float4 color : COLOR; };
VertexOutput main(VertexInput input, uint instance_id : SV_InstanceID)
{
    VertexOutput output;
    const RenderItemData item = render_items[instance_id];
    output.position = mul(mul(float4(input.position, 1.0f), item.world), view_projection);
    output.color = input.color * item.tint;
    return output;
}
