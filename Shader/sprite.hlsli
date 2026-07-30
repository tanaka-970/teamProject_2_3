// スプライト描画で共有する頂点情報と定数を定義する。
struct VS_OUT
{
	float4 position : SV_POSITION;
	float4 color : COLOR;
	float2 texcoord : TEXCOORD;
};
