// ボーン変形を適用してスキンメッシュ頂点を画面へ変換する頂点シェーダー。
#include "skinned_mesh.hlsli"
#include "motion_vector_skinning.hlsli"
VS_OUT main(VS_IN vin)
{
	VS_OUT vout;
	vin.position.xyz += vin.morph_position * gltf_morph.x;
	vin.normal.xyz += vin.morph_normal * gltf_morph.x;

	// 前フレームのボーン姿勢でも同じ頂点をスキニングしておく。
	// アニメーションによる動きをモーションベクターへ載せるために必要。
	const float4 previous_local = skin_previous_position(
		float4(vin.position.xyz, 1.0f), vin.bone_weights, vin.bone_indices);
	vout.previous_clip = mul(mul(previous_local, previous_world), previous_view_projection);

	vin.normal.w = 0;
	float sigma = vin.tangent.w;
	vin.tangent.w = 0;

	float4 blended_position = { 0, 0, 0, 1 };
	float4 blended_normal = { 0, 0, 0, 0 };
	float4 blended_tangent = { 0, 0, 0, 0 };
	for (int bone_index = 0; bone_index < 4; ++bone_index)
	{
		blended_position += vin.bone_weights[bone_index]
         * mul(vin.position, bone_transforms[vin.bone_indices[bone_index]]);
		blended_normal += vin.bone_weights[bone_index]
         * mul(vin.normal, bone_transforms[vin.bone_indices[bone_index]]);
		blended_tangent += vin.bone_weights[bone_index]
        * mul(vin.tangent, bone_transforms[vin.bone_indices[bone_index]]);

	}
	vin.position = float4(blended_position.xyz, 1.0f);
	vin.normal = float4(blended_normal.xyz, 0.0f);
	vin.tangent = float4(blended_tangent.xyz, 0.0f);
     
	vout.position = mul(vin.position, mul(world, view_projection));
 
	vout.world_position = mul(vin.position, world);
	vin.normal.w = 0;
	vout.world_normal = normalize(mul(vin.normal, world));
	vout.world_tangent = normalize(mul(vin.tangent, world));
	vout.world_tangent.w = sigma;

	vout.texcoord = vin.texcoord;
	vout.color = material_color;
	vout.current_clip = vout.position;

	return vout;
}
