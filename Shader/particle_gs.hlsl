// 各パーティクル点をカメラ向きの四角形へ展開するジオメトリシェーダー。
cbuffer SCENE_CONSTANT_BUFFER : register(b1)
{
    row_major float4x4 view_projection;
    float4 light_direction;
    float4 camera_position;
};

struct GS_IN
{
    float4 position : SV_POSITION;
    float4 color    : COLOR;
    float2 size_rot : TEXCOORD0;
};

struct GS_OUT
{
    float4 position : SV_POSITION;
    float4 color    : COLOR;
    float2 texcoord : TEXCOORD0;
};

[maxvertexcount(4)]
void main(point GS_IN ipt[1], inout TriangleStream<GS_OUT> stream)
{
    if (ipt[0].position.w < 0.5f) return; // 死

    float3 world = ipt[0].position.xyz;
    float3 look = normalize(camera_position.xyz - world);
    float3 up   = float3(0, 1, 0);
    float3 right = normalize(cross(up, look));
    up = cross(look, right);

    float s = ipt[0].size_rot.x;
    float r = ipt[0].size_rot.y;
    float cr = cos(r), sr = sin(r);

    float3 right_r = right * cr + up * sr;
    float3 up_r    = -right * sr + up * cr;

    float3 c0 = world - right_r * s + up_r * s;
    float3 c1 = world + right_r * s + up_r * s;
    float3 c2 = world - right_r * s - up_r * s;
    float3 c3 = world + right_r * s - up_r * s;

    GS_OUT o;
    o.color = ipt[0].color;

    o.position = mul(float4(c0, 1), view_projection); o.texcoord = float2(0,0); stream.Append(o);
    o.position = mul(float4(c1, 1), view_projection); o.texcoord = float2(1,0); stream.Append(o);
    o.position = mul(float4(c2, 1), view_projection); o.texcoord = float2(0,1); stream.Append(o);
    o.position = mul(float4(c3, 1), view_projection); o.texcoord = float2(1,1); stream.Append(o);
}
