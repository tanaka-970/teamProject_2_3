// Point / Spot Light の影マップへ、形状を必要なスライスぶん複製する。
//
// 頂点シェーダーは CSM のキャスター (csm_caster_static_vs /
// csm_caster_skinned_vs) をそのまま使い回す。どちらも仕事は
// 「ワールド座標へ変換して GS へ渡す」だけで同じなので、影の種類ごとに
// 同じ .hlsl を増やさない。差し替わるのはこの GS だけ。
//
// Spot は 1 スライス、Point は 6 スライス。1 回の Draw で 6 面ぶんを
// 出せるため、面ごとに Scene を描き直すより Draw Call が 1/6 で済む。
#include "local_shadow_common.hlsli"

cbuffer LOCAL_SHADOW_PASS : register(b11)
{
    // x=先頭スライス番号, y=このパスで描くスライス数, z/w=予約
    int4 local_shadow_pass_range;
};

struct GS_IN
{
    float4 world_position : POSITION;
};

struct GS_OUT
{
    float4 position : SV_POSITION;
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

        // この面の視錐台の外にある三角形は出さない。
        //
        // 【ここが Point Light の影の速さを決める】
        //   6 面へ無条件に複製すると、頂点処理もラスタライズも 6 倍になる。
        //   実際にはほとんどの三角形が 1〜2 面にしか映らないので、
        //   3 頂点が同じ面の外側に揃っているものを先に捨てる。
        //   保守的な判定なので、映るべき三角形を落とすことはない。
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
            output.slice = (uint) slice;
            stream.Append(output);
        }
        stream.RestartStrip();
    }
}
