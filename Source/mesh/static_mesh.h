#pragma once

// UNIT.13
#include <d3d11.h>
#include <wrl.h>
#include <directxmath.h>

//UNIT.14
#include <string>

// UNIT.15
#include <vector>

// UNIT.13
class static_mesh
{
public:
	struct vertex
	{
		DirectX::XMFLOAT3 position;
		DirectX::XMFLOAT3 normal;
		// UNIT.14
		DirectX::XMFLOAT2 texcoord;
	};
	struct constants
	{
		DirectX::XMFLOAT4X4 world;
		DirectX::XMFLOAT4 material_color;
	};

	// UNIT.15
	struct subset
	{
		std::wstring usemtl;
		uint32_t index_start{ 0 }; 	// start position of index buffer
		uint32_t index_count{ 0 }; 	// number of vertices (indices)
	};
	std::vector<subset> subsets;

	//UNIT.14
	//wstring texture_filename;
	//ComPtr<ID3D11ShaderResourceView> shader_resource_view;
	// UNIT.15
	struct material
	{
		std::wstring name;
		DirectX::XMFLOAT4 Ka{ 0.2f, 0.2f, 0.2f, 1.0f };
		DirectX::XMFLOAT4 Kd{ 0.8f, 0.8f, 0.8f, 1.0f };
		DirectX::XMFLOAT4 Ks{ 1.0f, 1.0f, 1.0f, 1.0f };
		// UNIT.16
		std::wstring texture_filenames[2];
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> shader_resource_views[2];
	};
	std::vector<material> materials;

	// UNIT.16
	DirectX::XMFLOAT3 bounding_box[2]{ { D3D11_FLOAT32_MAX, D3D11_FLOAT32_MAX, D3D11_FLOAT32_MAX }, { -D3D11_FLOAT32_MAX, -D3D11_FLOAT32_MAX, -D3D11_FLOAT32_MAX } };

private:
	Microsoft::WRL::ComPtr<ID3D11Buffer> vertex_buffer;
	Microsoft::WRL::ComPtr<ID3D11Buffer> index_buffer;

	Microsoft::WRL::ComPtr<ID3D11VertexShader> vertex_shader;
	Microsoft::WRL::ComPtr<ID3D11PixelShader> pixel_shader;
	Microsoft::WRL::ComPtr<ID3D11InputLayout> input_layout;
	Microsoft::WRL::ComPtr<ID3D11Buffer> constant_buffer;
	// TAAのモーションベクター用: b6=前フレームのワールド/ビュー射影。
	Microsoft::WRL::ComPtr<ID3D11Buffer> motion_object_constant_buffer;
	// 剛体なので前フレームのワールド行列だけ保持すればよい。
	DirectX::XMFLOAT4X4 previous_world{ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
	unsigned long long motion_frame_id{ 0 };
	bool motion_history_valid{ false };

	// 読み込み結果。コンストラクタが最後まで到達したときだけ true になる。
	bool loaded_{ false };
	std::wstring load_error_;

public:
	// 読み込み前にパスを検証する。
	//
	// 呼び出し側はこれで弾いてから構築すること。
	// 存在しない .obj を渡すと、以前は
	//   1) _ASSERT_EXPR(fin, "OBJ file not found.") が出て
	//   2) それを無視すると、空のまま subsets.rbegin()-> を実行して
	//      「can't decrement value-initialized vector iterator」が続けて出る
	// という二段構えの停止になっていた。
	//
	// out_reason には失敗理由が入る。Editor のログへそのまま出せる。
	static bool can_load(const wchar_t* obj_filename, std::wstring* out_reason = nullptr);

	// 構築が成功したか。false の場合、描画してはいけない。
	bool is_loaded() const noexcept { return loaded_; }
	const std::wstring& load_error() const noexcept { return load_error_; }

	static_mesh(ID3D11Device* device, const wchar_t* obj_filename, bool flipping_v_coordinates/*UNIT.14*/);
	// Fileを介さないprocedural mesh。Landscape / Built-in Primitive用。
	static_mesh(ID3D11Device* device, const std::vector<vertex>& vertices,
		const std::vector<uint32_t>& indices);

	// Procedural mesh の geometry だけを更新する。
	// Shader / InputLayout / Material / dummy texture は再生成しない。
	// Landscape の Sculpt 中に static_mesh 自体を毎フレーム作り直すと、
	// CSO 読み込みや texture 作成まで繰り返して極端に重くなるため、
	// GPU vertex/index buffer の差し替えだけを行う入口を分ける。
	bool update_procedural_geometry(ID3D11Device* device,
		const std::vector<vertex>& vertices,
		const std::vector<uint32_t>& indices);

	virtual ~static_mesh() = default;

	void render(ID3D11DeviceContext* immediate_context,
		const DirectX::XMFLOAT4X4& world,
		const DirectX::XMFLOAT4& material_color,
		ID3D11PixelShader* alternative_pixel_shader = nullptr,
		ID3D11VertexShader* alternative_vertex_shader = nullptr,
		ID3D11InputLayout* alternative_input_layout = nullptr,
		bool bind_pixel_shader = true,
		// trueのときだけ前フレーム姿勢をVSへ載せ、履歴を更新する。
		bool write_motion_vectors = false);

protected:
	void create_com_buffers(ID3D11Device* device, vertex* vertices, size_t vertex_count, uint32_t* indices, size_t index_count);
};
