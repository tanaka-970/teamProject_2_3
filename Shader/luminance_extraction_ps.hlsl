// Bloom対象となる高輝度部分だけを抽出するピクセルシェーダー。
#include"fullscreen_quad.hlsli"
#define POINT 0
#define LINEAR 1
#define ANISOTROPIC 2
SamplerState sampler_states[3] : register(s0);
Texture2D texture_map : register(t0);

// 輝度抽出の閾値をCPUから調整するための定数バッファ
// threshold: 輝度がこの値以上のピクセルだけを残す
cbuffer LuminanceConstants : register(b0)
{
	float threshold;
	float3 padding; //16の倍数にするためのダミー変数
};

float4 main(VS_OUT pin) : SV_TARGET
{
	// テクスチャから色をサンプリング
	float4 color = texture_map.Sample(sampler_states[ANISOTROPIC], pin.texcoord);

	// アルファ値は保持
	float alpha = color.a;

	// 輝度(0.299, 0.587, 0.114)の加重平均を計算し、閾値以上なら色を残す
	// step(threshold, luminance) が 1 のときのみ元の色が残る
	color.rgb = step(threshold, dot(color.rgb, float3(0.299, 0.587, 0.114))) * color.rgb;

	// RGBは抽出結果、Aは元のアルファ
	return float4(color.rgb, alpha);
}
