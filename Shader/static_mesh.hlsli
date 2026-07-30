// 静的メッシュ描画の頂点構造、シーン定数、材質定数を共通定義する。
#if 0
struct VS_OUT
{
	float4 position : SV_POSITION;
	float4 color : COLOR;
	// UNIT.14
	float2 texcoord : TEXCOORD;
};
#else
// UNIT.16
struct VS_OUT
{
	float4 position : SV_POSITION;
	float4 world_position : POSITION;
	float4 world_normal : NORMAL;
	float4 color : COLOR;
	float2 texcoord : TEXCOORD;
	// TAAのモーションベクター用。現/前フレームのクリップ座標をそのまま渡す。
	float4 current_clip : TEXCOORD1;
	float4 previous_clip : TEXCOORD2;
};
#endif

cbuffer OBJECT_CONSTANT_BUFFER : register(b0)
{
	row_major float4x4 world;
	float4 material_color;
};
cbuffer SCENE_CONSTANT_BUFFER : register(b1)
{
	row_major float4x4 view_projection;
	float4 light_direction;
	// UNIT.16
	float4 camera_position;
};
