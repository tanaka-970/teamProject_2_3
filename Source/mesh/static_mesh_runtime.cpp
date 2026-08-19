#include "shader.h"
#include "misc.h"
#include "static_mesh.h"

#include <fstream>
#include <vector>
#include <cwctype>

#include <filesystem>
#include "texture.h"
#include "../render/motion_vector_context.h"
#include "../../RePlayEngine/Rendering/RenderStats.h"

using namespace DirectX;

static_mesh::static_mesh(ID3D11Device* device, const std::vector<vertex>& source_vertices,
    const std::vector<uint32_t>& source_indices)
{
    if (device == nullptr || source_vertices.empty() || source_indices.empty() ||
        source_indices.size() % 3 != 0)
    {
        load_error_ = L"Procedural mesh data が空か不正です。";
        return;
    }
    for (uint32_t index : source_indices)
    {
        if (index >= source_vertices.size())
        {
            load_error_ = L"Procedural mesh index が頂点範囲外です。";
            return;
        }
    }

    // create_com_buffers は入力を変更しないが旧APIが非const pointerなので、
    // GPU upload 用に一時コピーを作る。
    std::vector<vertex> vertices = source_vertices;
    std::vector<uint32_t> indices = source_indices;
    create_com_buffers(device, vertices.data(), vertices.size(), indices.data(), indices.size());

    HRESULT hr{ S_OK };
    D3D11_INPUT_ELEMENT_DESC input_element_desc[]
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    create_vs_from_cso(device, "static_mesh_vs.cso", vertex_shader.GetAddressOf(),
        input_layout.GetAddressOf(), input_element_desc, ARRAYSIZE(input_element_desc));
    create_ps_from_cso(device, "static_mesh_ps.cso", pixel_shader.GetAddressOf());

    D3D11_BUFFER_DESC buffer_desc{};
    buffer_desc.ByteWidth = sizeof(constants);
    buffer_desc.Usage = D3D11_USAGE_DEFAULT;
    buffer_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    hr = device->CreateBuffer(&buffer_desc, nullptr, constant_buffer.GetAddressOf());
    _ASSERT_EXPR(SUCCEEDED(hr), hr_trace(hr));

    buffer_desc.ByteWidth = sizeof(motion_vectors::ObjectConstants);
    hr = device->CreateBuffer(&buffer_desc, nullptr,
        motion_object_constant_buffer.GetAddressOf());
    _ASSERT_EXPR(SUCCEEDED(hr), hr_trace(hr));

    // procedural mesh は外部 material library を持たない。1 subset / 1 material にする。
    subsets.push_back({ L"Procedural", 0, static_cast<uint32_t>(indices.size()) });
    material procedural_material;
    procedural_material.name = L"Procedural";
    materials.push_back(std::move(procedural_material));

    make_dummy_texture(device, materials[0].shader_resource_views[0].GetAddressOf(), 0xFFFFFFFF, 16);
    make_dummy_texture(device, materials[0].shader_resource_views[1].GetAddressOf(), 0xFFFF7F7F, 16);

    for (const vertex& v : vertices)
    {
        bounding_box[0].x = std::min<float>(bounding_box[0].x, v.position.x);
        bounding_box[0].y = std::min<float>(bounding_box[0].y, v.position.y);
        bounding_box[0].z = std::min<float>(bounding_box[0].z, v.position.z);
        bounding_box[1].x = std::max<float>(bounding_box[1].x, v.position.x);
        bounding_box[1].y = std::max<float>(bounding_box[1].y, v.position.y);
        bounding_box[1].z = std::max<float>(bounding_box[1].z, v.position.z);
    }
    loaded_ = vertex_buffer != nullptr && index_buffer != nullptr &&
        vertex_shader != nullptr && constant_buffer != nullptr;
    if (!loaded_) load_error_ = L"Procedural mesh GPU resource の作成に失敗しました。";
}

bool static_mesh::update_procedural_geometry(ID3D11Device* device,
    const std::vector<vertex>& source_vertices,
    const std::vector<uint32_t>& source_indices)
{
    if (device == nullptr || source_vertices.empty() || source_indices.empty() ||
        source_indices.size() % 3 != 0)
        return false;

    for (uint32_t index : source_indices)
    {
        if (index >= source_vertices.size()) return false;
    }

    // create_com_buffers の旧APIが非const pointerなので upload 用コピーだけ作る。
    // Shader / Material / Texture は既存のものをそのまま再利用する。
    std::vector<vertex> vertices = source_vertices;
    std::vector<uint32_t> indices = source_indices;
    create_com_buffers(device, vertices.data(), vertices.size(), indices.data(), indices.size());

    if (subsets.empty())
        subsets.push_back({ L"Procedural", 0, static_cast<uint32_t>(indices.size()) });
    else
    {
        subsets[0].index_start = 0;
        subsets[0].index_count = static_cast<uint32_t>(indices.size());
    }

    bounding_box[0] = { D3D11_FLOAT32_MAX, D3D11_FLOAT32_MAX, D3D11_FLOAT32_MAX };
    bounding_box[1] = { -D3D11_FLOAT32_MAX, -D3D11_FLOAT32_MAX, -D3D11_FLOAT32_MAX };
    for (const vertex& v : vertices)
    {
        bounding_box[0].x = std::min<float>(bounding_box[0].x, v.position.x);
        bounding_box[0].y = std::min<float>(bounding_box[0].y, v.position.y);
        bounding_box[0].z = std::min<float>(bounding_box[0].z, v.position.z);
        bounding_box[1].x = std::max<float>(bounding_box[1].x, v.position.x);
        bounding_box[1].y = std::max<float>(bounding_box[1].y, v.position.y);
        bounding_box[1].z = std::max<float>(bounding_box[1].z, v.position.z);
    }

    loaded_ = vertex_buffer != nullptr && index_buffer != nullptr &&
        vertex_shader != nullptr && constant_buffer != nullptr;
    if (!loaded_) load_error_ = L"Procedural mesh GPU geometry の更新に失敗しました。";
    return loaded_;
}

void static_mesh::render(ID3D11DeviceContext* immediate_context,
	const XMFLOAT4X4& world,
	const XMFLOAT4& material_color,
	ID3D11PixelShader* alternative_pixel_shader,
	ID3D11VertexShader* alternative_vertex_shader,
	ID3D11InputLayout* alternative_input_layout,
	bool bind_pixel_shader,
	bool write_motion_vectors)
{
	if (write_motion_vectors && motion_object_constant_buffer)
	{
		// 剛体なので前フレームのワールド行列を渡すだけでモーションベクターが出る。
		const motion_vectors::FrameContext& motion_frame = motion_vectors::Frame();
		motion_vectors::ObjectConstants motion_object{};
		motion_object.previous_world = motion_history_valid ? previous_world : world;
		motion_object.previous_view_projection = motion_frame.previous_view_projection;
		motion_object.params = { motion_frame.enabled && motion_history_valid ? 1.0f : 0.0f,
			motion_frame.current_jitter.x, motion_frame.current_jitter.y, 0.0f };
		motion_object.params2 = { motion_frame.previous_jitter.x,
			motion_frame.previous_jitter.y, 0.0f, 0.0f };
		immediate_context->UpdateSubresource(
			motion_object_constant_buffer.Get(), 0, nullptr, &motion_object, 0, 0);
		immediate_context->VSSetConstantBuffers(
			6, 1, motion_object_constant_buffer.GetAddressOf());

		// PS へも同じ Buffer を渡す。
		//
		// G-Buffer の Pixel Shader が compute_motion_vector() を呼んでおり、
		// その中で b6 の motion_params / motion_params2 を読んでいる。
		// つまり PS 側も b6 に 160 バイトを期待している。
		// VS だけに Bind すると、PS の b6 にはトゥーン材質 (80 バイト) が
		// 残ったままになり、毎フレーム
		//   "80 bytes provided, 160 bytes expected" の警告が出る。
		immediate_context->PSSetConstantBuffers(
			6, 1, motion_object_constant_buffer.GetAddressOf());

		// 同一フレーム内で二度呼ばれても履歴は一度だけ進める。
		if (motion_frame_id != motion_frame.frame_id)
		{
			motion_frame_id = motion_frame.frame_id;
			previous_world = world;
			motion_history_valid = true;
		}
	}

	uint32_t stride{ sizeof(vertex) };
	uint32_t offset{ 0 };
	immediate_context->IASetVertexBuffers(0, 1, vertex_buffer.GetAddressOf(), &stride, &offset);
	immediate_context->IASetIndexBuffer(index_buffer.Get(), DXGI_FORMAT_R32_UINT, 0);
	immediate_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	ID3D11InputLayout* selected_input_layout =
		alternative_input_layout ? alternative_input_layout : input_layout.Get();
	ReplayEngine::Rendering::Stats().TrackStateSet(
		ReplayEngine::Rendering::RenderStats::StateKind::InputLayout,
		selected_input_layout);
	immediate_context->IASetInputLayout(selected_input_layout);

	ReplayEngine::Rendering::Stats().CountStateSet(ReplayEngine::Rendering::RenderStats::StateKind::Shader, false);
	immediate_context->VSSetShader(alternative_vertex_shader ? alternative_vertex_shader : vertex_shader.Get(), nullptr, 0);
	if (bind_pixel_shader)
	{
		ReplayEngine::Rendering::Stats().CountStateSet(ReplayEngine::Rendering::RenderStats::StateKind::Shader, false);
		alternative_pixel_shader
			? immediate_context->PSSetShader(alternative_pixel_shader, nullptr, 0)
			: immediate_context->PSSetShader(pixel_shader.Get(), nullptr, 0);
	}
	else
	{
		ReplayEngine::Rendering::Stats().CountStateSet(ReplayEngine::Rendering::RenderStats::StateKind::Shader, false);
		immediate_context->PSSetShader(nullptr, nullptr, 0);
	}

#if 0
	immediate_context->PSSetShaderResources(0, 1, shader_resource_view.GetAddressOf());

	D3D11_BUFFER_DESC buffer_desc{};
	index_buffer->GetDesc(&buffer_desc);
	immediate_context->DrawIndexed(buffer_desc.ByteWidth / sizeof(uint32_t), 0, 0);
#else
	for (const material& material : materials)
	{
		immediate_context->PSSetShaderResources(0, 1, material.shader_resource_views[0].GetAddressOf());
		immediate_context->PSSetShaderResources(1, 1, material.shader_resource_views[1].GetAddressOf());

		constants data{ world, material_color };
		XMStoreFloat4(&data.material_color, XMLoadFloat4(&material_color) * XMLoadFloat4(&material.Kd));
		immediate_context->UpdateSubresource(constant_buffer.Get(), 0, 0, &data, 0, 0);
		immediate_context->VSSetConstantBuffers(0, 1, constant_buffer.GetAddressOf());
		immediate_context->PSSetConstantBuffers(0, 1, constant_buffer.GetAddressOf());

		for (const subset& subset : subsets)
		{
			if (material.name == subset.usemtl)
			{
				ReplayEngine::Rendering::Stats().CountDrawIndexed(subset.index_count);
				immediate_context->DrawIndexed(subset.index_count, subset.index_start, 0);
			}
		}
	}
#endif
}

void static_mesh::create_com_buffers(ID3D11Device* device, vertex* vertices, size_t vertex_count, uint32_t* indices, size_t index_count)
{
	HRESULT hr = S_OK;

	D3D11_BUFFER_DESC buffer_desc{};
	D3D11_SUBRESOURCE_DATA subresource_data{};
	buffer_desc.ByteWidth = static_cast<UINT>(sizeof(vertex) * vertex_count);
	buffer_desc.Usage = D3D11_USAGE_DEFAULT;
	buffer_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	buffer_desc.CPUAccessFlags = 0;
	buffer_desc.MiscFlags = 0;
	buffer_desc.StructureByteStride = 0;
	subresource_data.pSysMem = vertices;
	subresource_data.SysMemPitch = 0;
	subresource_data.SysMemSlicePitch = 0;
	hr = device->CreateBuffer(&buffer_desc, &subresource_data, vertex_buffer.ReleaseAndGetAddressOf());
	_ASSERT_EXPR(SUCCEEDED(hr), hr_trace(hr));

	buffer_desc.ByteWidth = static_cast<UINT>(sizeof(uint32_t) * index_count);
	buffer_desc.Usage = D3D11_USAGE_DEFAULT;
	buffer_desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
	subresource_data.pSysMem = indices;
	hr = device->CreateBuffer(&buffer_desc, &subresource_data, index_buffer.ReleaseAndGetAddressOf());
	_ASSERT_EXPR(SUCCEEDED(hr), hr_trace(hr));
}
