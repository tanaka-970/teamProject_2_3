cbuffer OBJECT_CONSTANT_BUFFER : register(b0)
{
    row_major float4x4 world;
    float4 material_color;
};
cbuffer SCENE_CONSTANT_BUFFER : register(b1)
{
    row_major float4x4 view_projection;
    float4 light_direction;
    float4 camera_position;
};
struct VertexInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 texcoord : TEXCOORD;
};
struct VS_OUT
{
    float4 position : SV_POSITION;
    float4 world_position : POSITION;
    float4 world_normal : NORMAL;
    float4 color : COLOR;
    float2 texcoord : TEXCOORD;
    float4 current_clip : TEXCOORD1;
    float4 previous_clip : TEXCOORD2;
};
VS_OUT main(VertexInput input)
{
    VS_OUT output;
    const float4 local_position = float4(input.position, 1.0f);
    output.world_position = mul(local_position, world);
    output.position = mul(output.world_position, view_projection);
    output.world_normal = float4(normalize(mul(float4(input.normal, 0.0f), world).xyz), 0.0f);
    output.color = material_color;
    output.texcoord = input.texcoord;
    output.current_clip = output.position;
    output.previous_clip = output.position;
    return output;
}
