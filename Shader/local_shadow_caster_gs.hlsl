// Point / Spot の影マップへ形状を必要なスライスぶん複製する GS。VS は CSM のものを使い回す。
#include "local_shadow_common.hlsli"

cbuffer LOCAL_SHADOW_PASS : register(b11)
{
    // x=先頭スライス番号, y=このパスで描くスライス数, z/w=予約
    int4 local_shadow_pass_range;
};

struct GS_IN
{
    float4 world_position : POSITION;
    float2 texcoord : TEXCOORD;
};

struct GS_OUT
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD;
    // Model Effect Stack の面消しを影でも評価するためワールド座標を運ぶ。
    float3 world_position : TEXCOORD1;
    uint   slice    : SV_RenderTargetArrayIndex;
};

[maxvertexcount(LOCAL_SHADOW_POINT_FACE_COUNT * 3)]
void main(triangle GS_IN input[3], inout TriangleStream<GS_OUT> stream)
{
    const int base = local_shadow_pass_range.x;
    const int count = min(local_shadow_pass_range.y, LOCAL_SHADOW_POINT_FACE_COUNT);

    for (int face = 0; face < count; ++face)
    {
        const int slice = base + face;

        float4 clip[3];
        [unroll] for (int v = 0; v < 3; ++v)
        {
            clip[v] = mul(input[v].world_position,
                local_shadow_slices[slice].view_projection);
        }

        // この面の外にある三角形は出さない。6 面複製の頂点処理を減らす本体。
        bool outside =
            (clip[0].x >  clip[0].w && clip[1].x >  clip[1].w && clip[2].x >  clip[2].w) ||
            (clip[0].x < -clip[0].w && clip[1].x < -clip[1].w && clip[2].x < -clip[2].w) ||
            (clip[0].y >  clip[0].w && clip[1].y >  clip[1].w && clip[2].y >  clip[2].w) ||
            (clip[0].y < -clip[0].w && clip[1].y < -clip[1].w && clip[2].y < -clip[2].w) ||
            (clip[0].z >  clip[0].w && clip[1].z >  clip[1].w && clip[2].z >  clip[2].w) ||
            (clip[0].z < 0.0f       && clip[1].z < 0.0f       && clip[2].z < 0.0f);
        if (outside) continue;

        [unroll] for (int i = 0; i < 3; ++i)
        {
            GS_OUT output;
            output.position = clip[i];
            output.texcoord = input[i].texcoord;
            output.world_position = input[i].world_position.xyz;
            output.slice = (uint) slice;
            stream.Append(output);
        }
        stream.RestartStrip();
    }
}
