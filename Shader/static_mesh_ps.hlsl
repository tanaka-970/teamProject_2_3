// 静的メッシュを標準ライティングで描くピクセルシェーダー。
#include "skinned_mesh.hlsli"

#define POINT 0
#define LINEAR 1
#define ANISOTROPIC 2

SamplerState sampler_states[3] : register(s0);
Texture2D texture_maps[4] : register(t0);

float4 main(VS_OUT pin) : SV_TARGET
{
    // テクスチャから色をサンプリング [cite: 561]
	float4 color = texture_maps[0].Sample(sampler_states[ANISOTROPIC], pin.texcoord);

    // 法線とライト方向の計算 [cite: 563-567]
	float3 N = normalize(pin.world_normal.xyz);
	float3 L = normalize(-light_direction.xyz);

    // 拡散反射の計算 [cite: 569]
	float3 diffuse = color.rgb * max(0, dot(N, L));

    // テクスチャのアルファ値とマテリアルカラーを合成して出力 [cite: 571]
	return float4(diffuse, color.a) * pin.color;
}
