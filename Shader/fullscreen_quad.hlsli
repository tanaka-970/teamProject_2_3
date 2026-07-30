// 全画面描画で受け渡す頂点位置とUVを定義する。
struct VS_OUT
{
	float4 position : SV_POSITION; // 画面上の位置
	float2 texcoord : TEXCOORD; // テクスチャの座標
};
