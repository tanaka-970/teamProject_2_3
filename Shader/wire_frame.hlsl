// メッシュの頂点色を使ってワイヤーフレームを描くピクセルシェーダー。
#include "static_mesh.hlsli"

float4 main(VS_OUT pin) : SV_TARGET
{
	return pin.color;
}
