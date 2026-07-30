// デバッグ用プリミティブの頂点情報とシーン定数を定義する。
struct VS_OUT
{
	float4 position : SV_POSITION;
	float4 color : COLOR;
};
cbuffer OBJECT_CONSTANT_BUFFER : register(b0)
{
	row_major float4x4 world;
	float4 material_color;
};
cbuffer SCENE_CONSTANT_BUFFER : register(b1)
{
	row_major float4x4 view_projection;
	float4 light_direction;
};

