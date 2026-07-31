#pragma once
#include <d3d11.h>
#include <directxmath.h>
#include <wrl.h>
#include <string>

class sprite
{
public:
	struct vertex
	{
		DirectX::XMFLOAT3 position;
		DirectX::XMFLOAT4 color;
		DirectX::XMFLOAT2 texcoord;
	};

	enum class AlphaMode
	{
		UseTextureAlpha = 0,	// 通常(テクスチャのアルファをそのまま使う)
		Black = 1,	// 黒背景を透過
		White = 2,	// 白背景を透過
	};
	// 【資料5】オーバーロードされた render メソッドの追加
	void render(ID3D11DeviceContext* immediate_context, float dx, float dy, float dw, float dh);
	//メンバ関数
	void render(ID3D11DeviceContext* immediate_context,
		float dx, float dy, float dw, float dh,
		float r, float g, float b, float a,
		float angle/*degree*/
		);
		//ユニット６オーバーロード
	void render(ID3D11DeviceContext* immediate_context,
		float dx, float dy, float dw, float dh,
		float r, float g, float b, float a,
		float angle, /*degree*/
		float sx, float sy, float sw, float sh // 切り抜き範囲
		);

		//文字描画
		// // sprite.h 内に追加
	void textout(ID3D11DeviceContext* immediate_context, std::string s,
		float x, float y, float w, float h,
		float r, float g, float b, float a);

    // ファイル名が空なら1x1の白テクスチャを生成する。
    // 単色UIを仮画像ファイルへ依存させないための経路となる。
	explicit sprite(ID3D11Device* device, const wchar_t* filename = nullptr,
		const char* pixel_shader_cso = "sprite_ps.cso");
	~sprite() = default;
	void SetAlphaMode(AlphaMode mode) { mAlphaMode = mode; }
	AlphaMode GetAlphaMode() const { return mAlphaMode; }
	float texture_width() const noexcept { return static_cast<float>(texture2d_desc.Width); }
	float texture_height() const noexcept { return static_cast<float>(texture2d_desc.Height); }
	bool valid() const noexcept { return shader_resource_view != nullptr; }
private:
	// メンバ変数（スマートポインタに統一）
	Microsoft::WRL::ComPtr<ID3D11VertexShader> vertex_shader;
	Microsoft::WRL::ComPtr<ID3D11PixelShader> pixel_shader;
	Microsoft::WRL::ComPtr<ID3D11InputLayout> input_layout;
	Microsoft::WRL::ComPtr<ID3D11Buffer> vertex_buffer;
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> shader_resource_view;
	Microsoft::WRL::ComPtr<ID3D11Buffer> alphaModeBuffer;
	AlphaMode mAlphaMode = AlphaMode::UseTextureAlpha;
	D3D11_TEXTURE2D_DESC texture2d_desc{};

};


