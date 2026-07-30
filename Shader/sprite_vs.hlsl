// スプライト頂点を画面座標へ変換してUVを渡す頂点シェーダー。
// UNIT.02
#include "sprite.hlsli"

VS_OUT main(float4 position : POSITION, float4 color : COLOR, float2 texcoord : TEXCOORD /*UNIT.05*/)
{
	VS_OUT vout;
	vout.position = position;
	vout.color = color;

	// UNIT.05
	vout.texcoord = texcoord;

	return vout;
}
