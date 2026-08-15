// スキンメッシュ描画の頂点構造、定数、ボーン情報を共通定義する。
 struct VS_IN
{
	float4 position : POSITION;
	float4 normal : NORMAL;
	float4 tangent : TANGENT;
	float2 texcoord : TEXCOORD;
	float4 bone_weights : WEIGHTS;
	uint4 bone_indices : BONES;
	float3 morph_position : MORPHPOS;
	float3 morph_normal : MORPHNORMAL;

};
struct VS_OUT
{
	float4 position : SV_POSITION;
	float4 world_position : POSITION;
	float4 world_normal : NORMAL;
	float4 world_tangent : TANGENT;
	float2 texcoord : TEXCOORD;
	float4 color : COLOR;
	// TAAのモーションベクター用。現/前フレームのクリップ座標をそのまま渡す。
	// SV_POSITIONはラスタライズ後の値になるため、別に持つ必要がある。
	float4 current_clip : TEXCOORD1;
	float4 previous_clip : TEXCOORD2;
};

static const int MAX_BONES = 256;
cbuffer OBJECT_CONSTANT_BUFFER : register(b0)
{
	row_major float4x4 world;
	float4 material_color;
	row_major float4x4 bone_transforms[MAX_BONES];
	// w=1 のときだけ GLB 内蔵 Material を使用する。
	float4 gltf_pbr;      // x=metallic y=roughness z=occlusion w=enabled
	float4 gltf_emissive; // rgb=factor w=strength
	float4 gltf_alpha;    // x=mode y=cutoff z=unlit w=normal scale
	float4 gltf_morph;    // x=target0 weight
};

cbuffer SCENE_CONSTANT_BUFFER : register(b1)
{
	row_major float4x4 view_projection;
	float4 light_direction;
	float4 camera_position;
};
