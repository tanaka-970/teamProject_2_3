// Deferred照明用の全画面三角形を生成する頂点シェーダー。
// フルスクリーン頂点シェーダー (画面全体に1枚の紙を貼る係)
// 頂点バッファを使わず、SV_VertexIDだけで画面いっぱいの四角形を作る。
// ライティングパスはモデルではなく「画面全体」に対して描くため、その土台。
struct VS_OUT
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

VS_OUT main(uint vid : SV_VERTEXID)
{
    const float2 positions[4] = { { -1, 1 }, { 1, 1 }, { -1, -1 }, { 1, -1 } };
    const float2 uvs[4] = { { 0, 0 }, { 1, 0 }, { 0, 1 }, { 1, 1 } };
    VS_OUT vout;
    vout.position = float4(positions[vid], 0, 1);
    vout.uv = uvs[vid];
    return vout;
}
