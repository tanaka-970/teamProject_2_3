// 画像、頂点色、透明度を合成してスプライトを描くピクセルシェーダー。
// UNIT.02
#include "sprite.hlsli"
Texture2D color_map : register(t0);



// UNIT.05
Texture2D color_texture : register(t0);
SamplerState point_sampler_state : register(s0);
SamplerState linear_sampler_state : register(s1);
SamplerState anisotropic_sampler_state : register(s2);

float4 main(VS_OUT pin) : SV_TARGET
{
	float4 color = color_map.Sample(anisotropic_sampler_state, pin.texcoord);
	float alpha = color.a;
#if 1 
     // Inverse gamma process 
	const float GAMMA = 2.2;
	color.rgb = pow(color.rgb, GAMMA);
#endif 
	return float4(color.rgb, alpha) * pin.color;
	
}


