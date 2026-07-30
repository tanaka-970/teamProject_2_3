#pragma once
#include<d3d11.h>
#include<directxmath.h>
#include <WICTextureLoader.h>
#include <vector>
#include<wrl.h>
class sprite_batch
{


	////メンバ変数
	//ID3D11VertexShader* vertex_shader;
	//ID3D11PixelShader* pixel_shader;
	//ID3D11InputLayout* input_layout;
	//ID3D11Buffer* vertex_buffer;
	//ID3D11ShaderResourceView* shader_resource_view = nullptr;
	D3D11_TEXTURE2D_DESC texture2d_desc;
	ID3D11Resource* resource{  };
	const size_t max_vertices;
	

public:
	
	//メンバ関数
	void render(ID3D11DeviceContext* immediate_context,
		float dx, float dy, float dw, float dh,
		float r, float g, float b, float a,
		float angle/*degree*/
	);
	
	void render(ID3D11DeviceContext* immediate_context,
		float dx, float dy, float dw, float dh,
		float r, float g, float b, float a,
		float angle, /*degree*/
		float sx, float sy, float sw, float sh // 切り抜き範囲
	);
	//コンストラクタデストラクタ
	sprite_batch(ID3D11Device* device, const wchar_t* filename, size_t max_sprites);
	~sprite_batch();
	void begin(ID3D11DeviceContext* immediate_context);
	void end(ID3D11DeviceContext* immediate_context);
public:
	struct vertex
	{
		DirectX::XMFLOAT3 position;
		DirectX::XMFLOAT4 color;
		DirectX::XMFLOAT2 texcoord;
	};
	
	std::vector<struct vertex> vertices;
	//ユニット１０
	private:
		Microsoft::WRL::ComPtr<ID3D11VertexShader> vertex_shader;
		Microsoft::WRL::ComPtr<ID3D11PixelShader> pixel_shader;
		Microsoft::WRL::ComPtr<ID3D11InputLayout> input_layout;
		Microsoft::WRL::ComPtr<ID3D11Buffer> vertex_buffer;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> shader_resource_view;
};


