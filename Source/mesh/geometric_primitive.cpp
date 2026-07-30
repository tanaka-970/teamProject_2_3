// UNIT.11
#include "shader.h"
#include "misc.h"
#include "geometric_primitive.h"

// UNIT.11
geometric_primitive::geometric_primitive(ID3D11Device* device)
{
#if 0 // UNIT.12
	// create a mesh for a cube
	vertex vertices[24]{};
	uint32_t indices[36]{};

	uint32_t face{ 0 };

	// top-side
	// 0---------1
	// |         |
	// |   -Y    |
	// |         |
	// 2---------3
	face = 0;
	vertices[face * 4 + 0].position = { -0.5f, +0.5f, +0.5f };
	vertices[face * 4 + 1].position = { +0.5f, +0.5f, +0.5f };
	vertices[face * 4 + 2].position = { -0.5f, +0.5f, -0.5f };
	vertices[face * 4 + 3].position = { +0.5f, +0.5f, -0.5f };
	vertices[face * 4 + 0].normal = { +0.0f, +1.0f, +0.0f };
	vertices[face * 4 + 1].normal = { +0.0f, +1.0f, +0.0f };
	vertices[face * 4 + 2].normal = { +0.0f, +1.0f, +0.0f };
	vertices[face * 4 + 3].normal = { +0.0f, +1.0f, +0.0f };
	indices[face * 6 + 0] = face * 4 + 0;
	indices[face * 6 + 1] = face * 4 + 1;
	indices[face * 6 + 2] = face * 4 + 2;
	indices[face * 6 + 3] = face * 4 + 1;
	indices[face * 6 + 4] = face * 4 + 3;
	indices[face * 6 + 5] = face * 4 + 2;

	// bottom-side
	// 0---------1
	// |         |
	// |   -Y    |
	// |         |
	// 2---------3
	face += 1;
	vertices[face * 4 + 0].position = { -0.5f, -0.5f, +0.5f };
	vertices[face * 4 + 1].position = { +0.5f, -0.5f, +0.5f };
	vertices[face * 4 + 2].position = { -0.5f, -0.5f, -0.5f };
	vertices[face * 4 + 3].position = { +0.5f, -0.5f, -0.5f };
	vertices[face * 4 + 0].normal = { +0.0f, -1.0f, +0.0f };
	vertices[face * 4 + 1].normal = { +0.0f, -1.0f, +0.0f };
	vertices[face * 4 + 2].normal = { +0.0f, -1.0f, +0.0f };
	vertices[face * 4 + 3].normal = { +0.0f, -1.0f, +0.0f };
	indices[face * 6 + 0] = face * 4 + 0;
	indices[face * 6 + 1] = face * 4 + 2;
	indices[face * 6 + 2] = face * 4 + 1;
	indices[face * 6 + 3] = face * 4 + 1;
	indices[face * 6 + 4] = face * 4 + 2;
	indices[face * 6 + 5] = face * 4 + 3;

	// front-side
	// 0---------1
	// |         |
	// |   +Z    |
	// |         |
	// 2---------3
	face += 1;
	vertices[face * 4 + 0].position = { -0.5f, +0.5f, -0.5f };
	vertices[face * 4 + 1].position = { +0.5f, +0.5f, -0.5f };
	vertices[face * 4 + 2].position = { -0.5f, -0.5f, -0.5f };
	vertices[face * 4 + 3].position = { +0.5f, -0.5f, -0.5f };
	vertices[face * 4 + 0].normal = { +0.0f, +0.0f, -1.0f };
	vertices[face * 4 + 1].normal = { +0.0f, +0.0f, -1.0f };
	vertices[face * 4 + 2].normal = { +0.0f, +0.0f, -1.0f };
	vertices[face * 4 + 3].normal = { +0.0f, +0.0f, -1.0f };
	indices[face * 6 + 0] = face * 4 + 0;
	indices[face * 6 + 1] = face * 4 + 1;
	indices[face * 6 + 2] = face * 4 + 2;
	indices[face * 6 + 3] = face * 4 + 1;
	indices[face * 6 + 4] = face * 4 + 3;
	indices[face * 6 + 5] = face * 4 + 2;

	// back-side
	// 0---------1
	// |         |
	// |   +Z    |
	// |         |
	// 2---------3
	face += 1;
	vertices[face * 4 + 0].position = { -0.5f, +0.5f, +0.5f };
	vertices[face * 4 + 1].position = { +0.5f, +0.5f, +0.5f };
	vertices[face * 4 + 2].position = { -0.5f, -0.5f, +0.5f };
	vertices[face * 4 + 3].position = { +0.5f, -0.5f, +0.5f };
	vertices[face * 4 + 0].normal = { +0.0f, +0.0f, +1.0f };
	vertices[face * 4 + 1].normal = { +0.0f, +0.0f, +1.0f };
	vertices[face * 4 + 2].normal = { +0.0f, +0.0f, +1.0f };
	vertices[face * 4 + 3].normal = { +0.0f, +0.0f, +1.0f };
	indices[face * 6 + 0] = face * 4 + 0;
	indices[face * 6 + 1] = face * 4 + 2;
	indices[face * 6 + 2] = face * 4 + 1;
	indices[face * 6 + 3] = face * 4 + 1;
	indices[face * 6 + 4] = face * 4 + 2;
	indices[face * 6 + 5] = face * 4 + 3;

	// right-side
	// 0---------1
	// |         |      
	// |   -X    |
	// |         |
	// 2---------3
	face += 1;
	vertices[face * 4 + 0].position = { +0.5f, +0.5f, -0.5f };
	vertices[face * 4 + 1].position = { +0.5f, +0.5f, +0.5f };
	vertices[face * 4 + 2].position = { +0.5f, -0.5f, -0.5f };
	vertices[face * 4 + 3].position = { +0.5f, -0.5f, +0.5f };
	vertices[face * 4 + 0].normal = { +1.0f, +0.0f, +0.0f };
	vertices[face * 4 + 1].normal = { +1.0f, +0.0f, +0.0f };
	vertices[face * 4 + 2].normal = { +1.0f, +0.0f, +0.0f };
	vertices[face * 4 + 3].normal = { +1.0f, +0.0f, +0.0f };
	indices[face * 6 + 0] = face * 4 + 0;
	indices[face * 6 + 1] = face * 4 + 1;
	indices[face * 6 + 2] = face * 4 + 2;
	indices[face * 6 + 3] = face * 4 + 1;
	indices[face * 6 + 4] = face * 4 + 3;
	indices[face * 6 + 5] = face * 4 + 2;

	// left-side
	// 0---------1
	// |         |      
	// |   -X    |
	// |         |
	// 2---------3
	face += 1;
	vertices[face * 4 + 0].position = { -0.5f, +0.5f, -0.5f };
	vertices[face * 4 + 1].position = { -0.5f, +0.5f, +0.5f };
	vertices[face * 4 + 2].position = { -0.5f, -0.5f, -0.5f };
	vertices[face * 4 + 3].position = { -0.5f, -0.5f, +0.5f };
	vertices[face * 4 + 0].normal = { -1.0f, +0.0f, +0.0f };
	vertices[face * 4 + 1].normal = { -1.0f, +0.0f, +0.0f };
	vertices[face * 4 + 2].normal = { -1.0f, +0.0f, +0.0f };
	vertices[face * 4 + 3].normal = { -1.0f, +0.0f, +0.0f };
	indices[face * 6 + 0] = face * 4 + 0;
	indices[face * 6 + 1] = face * 4 + 2;
	indices[face * 6 + 2] = face * 4 + 1;
	indices[face * 6 + 3] = face * 4 + 1;
	indices[face * 6 + 4] = face * 4 + 2;
	indices[face * 6 + 5] = face * 4 + 3;

	create_com_buffers(device, vertices, 24, indices, 36);
#endif

	HRESULT hr{ S_OK };

	D3D11_INPUT_ELEMENT_DESC input_element_desc[]
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	};
	create_vs_from_cso(device, "geometric_primitive_vs.cso", vertex_shader.GetAddressOf(), input_layout.GetAddressOf(), input_element_desc, ARRAYSIZE(input_element_desc));
	create_ps_from_cso(device, "geometric_primitive_ps.cso", pixel_shader.GetAddressOf());

	D3D11_BUFFER_DESC buffer_desc{};
	buffer_desc.ByteWidth = sizeof(constants);
	buffer_desc.Usage = D3D11_USAGE_DEFAULT;
	buffer_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	hr = device->CreateBuffer(&buffer_desc, nullptr, constant_buffer.GetAddressOf());
	_ASSERT_EXPR(SUCCEEDED(hr), hr_trace(hr));
}

// UNIT.11
void geometric_primitive::render(ID3D11DeviceContext* immediate_context, const DirectX::XMFLOAT4X4& world, const DirectX::XMFLOAT4& material_color)
{
	uint32_t stride{ sizeof(vertex) };
	uint32_t offset{ 0 };
	immediate_context->IASetVertexBuffers(0, 1, vertex_buffer.GetAddressOf(), &stride, &offset);
	immediate_context->IASetIndexBuffer(index_buffer.Get(), DXGI_FORMAT_R32_UINT, 0);
	immediate_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	immediate_context->IASetInputLayout(input_layout.Get());

	immediate_context->VSSetShader(vertex_shader.Get(), nullptr, 0);
	immediate_context->PSSetShader(pixel_shader.Get(), nullptr, 0);

	constants data{ world, material_color };
	immediate_context->UpdateSubresource(constant_buffer.Get(), 0, 0, &data, 0, 0);
	immediate_context->VSSetConstantBuffers(0, 1, constant_buffer.GetAddressOf());

	D3D11_BUFFER_DESC buffer_desc{};
	index_buffer->GetDesc(&buffer_desc);
	immediate_context->DrawIndexed(buffer_desc.ByteWidth / sizeof(uint32_t), 0, 0);
}

// UNIT.11
void geometric_primitive::create_com_buffers(ID3D11Device* device, vertex* vertices, size_t vertex_count, uint32_t* indices, size_t index_count)
{
	HRESULT hr{ S_OK };

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

// UNIT.12
geometric_cube::geometric_cube(ID3D11Device* device) : geometric_primitive(device)
{
	// create a mesh for a cube
	vertex vertices[24]{};
	uint32_t indices[36]{};

	uint32_t face{ 0 };

	// top-side
	// 0---------1
	// |         |
	// |   -Y    |
	// |         |
	// 2---------3
	face = 0;
	vertices[face * 4 + 0].position = { -0.5f, +0.5f, +0.5f };
	vertices[face * 4 + 1].position = { +0.5f, +0.5f, +0.5f };
	vertices[face * 4 + 2].position = { -0.5f, +0.5f, -0.5f };
	vertices[face * 4 + 3].position = { +0.5f, +0.5f, -0.5f };
	vertices[face * 4 + 0].normal = { +0.0f, +1.0f, +0.0f };
	vertices[face * 4 + 1].normal = { +0.0f, +1.0f, +0.0f };
	vertices[face * 4 + 2].normal = { +0.0f, +1.0f, +0.0f };
	vertices[face * 4 + 3].normal = { +0.0f, +1.0f, +0.0f };
	indices[face * 6 + 0] = face * 4 + 0;
	indices[face * 6 + 1] = face * 4 + 1;
	indices[face * 6 + 2] = face * 4 + 2;
	indices[face * 6 + 3] = face * 4 + 1;
	indices[face * 6 + 4] = face * 4 + 3;
	indices[face * 6 + 5] = face * 4 + 2;

	// bottom-side
	// 0---------1
	// |         |
	// |   -Y    |
	// |         |
	// 2---------3
	face += 1;
	vertices[face * 4 + 0].position = { -0.5f, -0.5f, +0.5f };
	vertices[face * 4 + 1].position = { +0.5f, -0.5f, +0.5f };
	vertices[face * 4 + 2].position = { -0.5f, -0.5f, -0.5f };
	vertices[face * 4 + 3].position = { +0.5f, -0.5f, -0.5f };
	vertices[face * 4 + 0].normal = { +0.0f, -1.0f, +0.0f };
	vertices[face * 4 + 1].normal = { +0.0f, -1.0f, +0.0f };
	vertices[face * 4 + 2].normal = { +0.0f, -1.0f, +0.0f };
	vertices[face * 4 + 3].normal = { +0.0f, -1.0f, +0.0f };
	indices[face * 6 + 0] = face * 4 + 0;
	indices[face * 6 + 1] = face * 4 + 2;
	indices[face * 6 + 2] = face * 4 + 1;
	indices[face * 6 + 3] = face * 4 + 1;
	indices[face * 6 + 4] = face * 4 + 2;
	indices[face * 6 + 5] = face * 4 + 3;

	// front-side
	// 0---------1
	// |         |
	// |   +Z    |
	// |         |
	// 2---------3
	face += 1;
	vertices[face * 4 + 0].position = { -0.5f, +0.5f, -0.5f };
	vertices[face * 4 + 1].position = { +0.5f, +0.5f, -0.5f };
	vertices[face * 4 + 2].position = { -0.5f, -0.5f, -0.5f };
	vertices[face * 4 + 3].position = { +0.5f, -0.5f, -0.5f };
	vertices[face * 4 + 0].normal = { +0.0f, +0.0f, -1.0f };
	vertices[face * 4 + 1].normal = { +0.0f, +0.0f, -1.0f };
	vertices[face * 4 + 2].normal = { +0.0f, +0.0f, -1.0f };
	vertices[face * 4 + 3].normal = { +0.0f, +0.0f, -1.0f };
	indices[face * 6 + 0] = face * 4 + 0;
	indices[face * 6 + 1] = face * 4 + 1;
	indices[face * 6 + 2] = face * 4 + 2;
	indices[face * 6 + 3] = face * 4 + 1;
	indices[face * 6 + 4] = face * 4 + 3;
	indices[face * 6 + 5] = face * 4 + 2;

	// back-side
	// 0---------1
	// |         |
	// |   +Z    |
	// |         |
	// 2---------3
	face += 1;
	vertices[face * 4 + 0].position = { -0.5f, +0.5f, +0.5f };
	vertices[face * 4 + 1].position = { +0.5f, +0.5f, +0.5f };
	vertices[face * 4 + 2].position = { -0.5f, -0.5f, +0.5f };
	vertices[face * 4 + 3].position = { +0.5f, -0.5f, +0.5f };
	vertices[face * 4 + 0].normal = { +0.0f, +0.0f, +1.0f };
	vertices[face * 4 + 1].normal = { +0.0f, +0.0f, +1.0f };
	vertices[face * 4 + 2].normal = { +0.0f, +0.0f, +1.0f };
	vertices[face * 4 + 3].normal = { +0.0f, +0.0f, +1.0f };
	indices[face * 6 + 0] = face * 4 + 0;
	indices[face * 6 + 1] = face * 4 + 2;
	indices[face * 6 + 2] = face * 4 + 1;
	indices[face * 6 + 3] = face * 4 + 1;
	indices[face * 6 + 4] = face * 4 + 2;
	indices[face * 6 + 5] = face * 4 + 3;

	// right-side
	// 0---------1
	// |         |      
	// |   -X    |
	// |         |
	// 2---------3
	face += 1;
	vertices[face * 4 + 0].position = { +0.5f, +0.5f, -0.5f };
	vertices[face * 4 + 1].position = { +0.5f, +0.5f, +0.5f };
	vertices[face * 4 + 2].position = { +0.5f, -0.5f, -0.5f };
	vertices[face * 4 + 3].position = { +0.5f, -0.5f, +0.5f };
	vertices[face * 4 + 0].normal = { +1.0f, +0.0f, +0.0f };
	vertices[face * 4 + 1].normal = { +1.0f, +0.0f, +0.0f };
	vertices[face * 4 + 2].normal = { +1.0f, +0.0f, +0.0f };
	vertices[face * 4 + 3].normal = { +1.0f, +0.0f, +0.0f };
	indices[face * 6 + 0] = face * 4 + 0;
	indices[face * 6 + 1] = face * 4 + 1;
	indices[face * 6 + 2] = face * 4 + 2;
	indices[face * 6 + 3] = face * 4 + 1;
	indices[face * 6 + 4] = face * 4 + 3;
	indices[face * 6 + 5] = face * 4 + 2;

	// left-side
	// 0---------1
	// |         |      
	// |   -X    |
	// |         |
	// 2---------3
	face += 1;
	vertices[face * 4 + 0].position = { -0.5f, +0.5f, -0.5f };
	vertices[face * 4 + 1].position = { -0.5f, +0.5f, +0.5f };
	vertices[face * 4 + 2].position = { -0.5f, -0.5f, -0.5f };
	vertices[face * 4 + 3].position = { -0.5f, -0.5f, +0.5f };
	vertices[face * 4 + 0].normal = { -1.0f, +0.0f, +0.0f };
	vertices[face * 4 + 1].normal = { -1.0f, +0.0f, +0.0f };
	vertices[face * 4 + 2].normal = { -1.0f, +0.0f, +0.0f };
	vertices[face * 4 + 3].normal = { -1.0f, +0.0f, +0.0f };
	indices[face * 6 + 0] = face * 4 + 0;
	indices[face * 6 + 1] = face * 4 + 2;
	indices[face * 6 + 2] = face * 4 + 1;
	indices[face * 6 + 3] = face * 4 + 1;
	indices[face * 6 + 4] = face * 4 + 2;
	indices[face * 6 + 5] = face * 4 + 3;

	create_com_buffers(device, vertices, 24, indices, 36);
}

// UNIT.12
#include <vector>
geometric_cylinder::geometric_cylinder(ID3D11Device* device, uint32_t slices) : geometric_primitive(device)
{
	std::vector<vertex> vertices;
	std::vector<uint32_t> indices;

	float d{ 2.0f * DirectX::XM_PI / slices };
	float r{ 0.5f };

	vertex vertex{};
	uint32_t base_index{ 0 };

	// top cap centre
	vertex.position = { 0.0f, +0.5f, 0.0f };
	vertex.normal = { 0.0f, +1.0f, 0.0f };
	vertices.emplace_back(vertex);
	// top cap ring
	for (uint32_t i = 0; i < slices; ++i)
	{
		float x{ r * cosf(i * d) };
		float z{ r * sinf(i * d) };
		vertex.position = { x, +0.5f, z };
		vertex.normal = { 0.0f, +1.0f, 0.0f };
		vertices.emplace_back(vertex);
	}
	base_index = 0;
	for (uint32_t i = 0; i < slices - 1; ++i)
	{
		indices.emplace_back(base_index + 0);
		indices.emplace_back(base_index + i + 2);
		indices.emplace_back(base_index + i + 1);
	}
	indices.emplace_back(base_index + 0);
	indices.emplace_back(base_index + 1);
	indices.emplace_back(base_index + slices);

	// bottom cap centre
	vertex.position = { 0.0f, -0.5f, 0.0f };
	vertex.normal = { 0.0f, -1.0f, 0.0f };
	vertices.emplace_back(vertex);
	// bottom cap ring
	for (uint32_t i = 0; i < slices; ++i)
	{
		float x = r * cosf(i * d);
		float z = r * sinf(i * d);
		vertex.position = { x, -0.5f, z };
		vertex.normal = { 0.0f, -1.0f, 0.0f };
		vertices.emplace_back(vertex);
	}
	base_index = slices + 1;
	for (uint32_t i = 0; i < slices - 1; ++i)
	{
		indices.emplace_back(base_index + 0);
		indices.emplace_back(base_index + i + 1);
		indices.emplace_back(base_index + i + 2);
	}
	indices.emplace_back(base_index + 0);
	indices.emplace_back(base_index + (slices - 1) + 1);
	indices.emplace_back(base_index + (0) + 1);

	// side rectangle
	for (uint32_t i = 0; i < slices; ++i)
	{
		float x = r * cosf(i * d);
		float z = r * sinf(i * d);

		vertex.position = { x, +0.5f, z };
		vertex.normal = { x, 0.0f, z };
		vertices.emplace_back(vertex);

		vertex.position = { x, -0.5f, z };
		vertex.normal = { x, 0.0f, z };
		vertices.emplace_back(vertex);
	}
	base_index = slices * 2 + 2;
	for (uint32_t i = 0; i < slices - 1; ++i)
	{
		indices.emplace_back(base_index + i * 2 + 0);
		indices.emplace_back(base_index + i * 2 + 2);
		indices.emplace_back(base_index + i * 2 + 1);

		indices.emplace_back(base_index + i * 2 + 1);
		indices.emplace_back(base_index + i * 2 + 2);
		indices.emplace_back(base_index + i * 2 + 3);
	}
	indices.emplace_back(base_index + (slices - 1) * 2 + 0);
	indices.emplace_back(base_index + (0) * 2 + 0);
	indices.emplace_back(base_index + (slices - 1) * 2 + 1);

	indices.emplace_back(base_index + (slices - 1) * 2 + 1);
	indices.emplace_back(base_index + (0) * 2 + 0);
	indices.emplace_back(base_index + (0) * 2 + 1);

	create_com_buffers(device, vertices.data(), vertices.size(), indices.data(), indices.size());
}

// UNIT.12
geometric_sphere::geometric_sphere(ID3D11Device* device, uint32_t slices, uint32_t stacks) : geometric_primitive(device)
{
	std::vector<vertex> vertices;
	std::vector<uint32_t> indices;

	float r{ 0.5f };

	//
	// Compute the vertices stating at the top pole and moving down the stacks.
	//

	// Poles: note that there will be texture coordinate distortion as there is
	// not a unique point on the texture map to assign to the pole when mapping
	// a rectangular texture onto a sphere.
	vertex top_vertex{};
	top_vertex.position = { 0.0f, +r, 0.0f };
	top_vertex.normal = { 0.0f, +1.0f, 0.0f };

	vertex bottom_vertex{};
	bottom_vertex.position = { 0.0f, -r, 0.0f };
	bottom_vertex.normal = { 0.0f, -1.0f, 0.0f };

	vertices.emplace_back(top_vertex);

	float phi_step{ DirectX::XM_PI / stacks };
	float theta_step{ 2.0f * DirectX::XM_PI / slices };

	// Compute vertices for each stack ring (do not count the poles as rings).
	for (uint32_t i = 1; i <= stacks - 1; ++i)
	{
		float phi{ i * phi_step };

		// Vertices of ring.
		for (uint32_t j = 0; j <= slices; ++j)
		{
			float theta{ j * theta_step };

			vertex v{};

			// spherical to cartesian
			v.position.x = r * sinf(phi) * cosf(theta);
			v.position.y = r * cosf(phi);
			v.position.z = r * sinf(phi) * sinf(theta);

			DirectX::XMVECTOR p{ XMLoadFloat3(&v.position) };
			DirectX::XMStoreFloat3(&v.normal, DirectX::XMVector3Normalize(p));

			vertices.emplace_back(v);
		}
	}

	vertices.emplace_back(bottom_vertex);

	//
	// Compute indices for top stack.  The top stack was written first to the vertex buffer
	// and connects the top pole to the first ring.
	//
	for (uint32_t i = 1; i <= slices; ++i)
	{
		indices.emplace_back(0);
		indices.emplace_back(i + 1);
		indices.emplace_back(i);
	}

	//
	// Compute indices for inner stacks (not connected to poles).
	//

	// Offset the indices to the index of the first vertex in the first ring.
	// This is just skipping the top pole vertex.
	uint32_t base_index{ 1 };
	uint32_t ring_vertex_count{ slices + 1 };
	for (uint32_t i = 0; i < stacks - 2; ++i)
	{
		for (uint32_t j = 0; j < slices; ++j)
		{
			indices.emplace_back(base_index + i * ring_vertex_count + j);
			indices.emplace_back(base_index + i * ring_vertex_count + j + 1);
			indices.emplace_back(base_index + (i + 1) * ring_vertex_count + j);

			indices.emplace_back(base_index + (i + 1) * ring_vertex_count + j);
			indices.emplace_back(base_index + i * ring_vertex_count + j + 1);
			indices.emplace_back(base_index + (i + 1) * ring_vertex_count + j + 1);
		}
	}

	//
	// Compute indices for bottom stack.  The bottom stack was written last to the vertex buffer
	// and connects the bottom pole to the bottom ring.
	//

	// South pole vertex was added last.
	uint32_t south_pole_index{ static_cast<uint32_t>(vertices.size() - 1) };

	// Offset the indices to the index of the first vertex in the last ring.
	base_index = south_pole_index - ring_vertex_count;

	for (uint32_t i = 0; i < slices; ++i)
	{
		indices.emplace_back(south_pole_index);
		indices.emplace_back(base_index + i);
		indices.emplace_back(base_index + i + 1);
	}
	create_com_buffers(device, vertices.data(), vertices.size(), indices.data(), indices.size());
}

// UNIT.12
geometric_capsule::geometric_capsule(ID3D11Device* device, float mantle_height, const DirectX::XMFLOAT3& radius, uint32_t slices, uint32_t ellipsoid_stacks, uint32_t mantle_stacks) : geometric_primitive(device)
{
	std::vector<vertex> vertices;
	std::vector<uint32_t> indices;
	const int base_offset = 0;

	slices = std::max<uint32_t>(3u, slices);
	mantle_stacks = std::max<uint32_t>(1u, mantle_stacks);
	ellipsoid_stacks = std::max<uint32_t>(2u, ellipsoid_stacks);

	const float inv_slices = 1.0f / static_cast<float>(slices);
	const float inv_mantle_stacks = 1.0f / static_cast<float>(mantle_stacks);
	const float inv_ellipsoid_stacks = 1.0f / static_cast<float>(ellipsoid_stacks);

	const float pi_2{ 3.14159265358979f * 2.0f };
	const float pi_0_5{ 3.14159265358979f * 0.5f };
	const float angle_steps = inv_slices * pi_2;
	const float half_height = mantle_height * 0.5f;

	/* Generate mantle vertices */
	struct spherical {
		float radius, theta, phi;
	} point{ 1, 0, 0 };
	DirectX::XMFLOAT3 position, normal;
	DirectX::XMFLOAT2 texcoord;

	float angle = 0.0f;
	for (uint32_t u = 0; u <= slices; ++u)
	{
		/* Compute X- and Z coordinates */
		texcoord.x = sinf(angle);
		texcoord.y = cosf(angle);

		position.x = texcoord.x * radius.x;
		position.z = texcoord.y * radius.z;

		/* Compute normal vector */
		normal.x = texcoord.x;
		normal.y = 0;
		normal.z = texcoord.y;

		float magnitude = sqrtf(normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
		normal.x = normal.x / magnitude;
		normal.y = normal.y / magnitude;
		normal.z = normal.z / magnitude;

		/* Add top and bottom vertex */
		texcoord.x = static_cast<float>(slices - u) * inv_slices;

		for (uint32_t v = 0; v <= mantle_stacks; ++v)
		{
			texcoord.y = static_cast<float>(v) * inv_mantle_stacks;
#if _HAS_CXX20
			position.y = lerp(half_height, -half_height, texcoord.y);
#else
			position.y = half_height * (1 - texcoord.y) + -half_height * texcoord.y;
#endif
			vertices.push_back({ position, normal });
		}

		/* Increase angle for the next iteration */
		angle += angle_steps;
	}

	/* Generate bottom and top cover vertices */
	const float cover_side[2] = { 1, -1 };
	uint32_t base_offset_ellipsoid[2] = { 0 };
	for (size_t i = 0; i < 2; ++i)
	{
		base_offset_ellipsoid[i] = static_cast<uint32_t>(vertices.size());

		for (uint32_t v = 0; v <= ellipsoid_stacks; ++v)
		{
			/* Compute theta of spherical coordinate */
			texcoord.y = static_cast<float>(v) * inv_ellipsoid_stacks;
			point.theta = texcoord.y * pi_0_5;

			for (uint32_t u = 0; u <= slices; ++u)
			{
				/* Compute phi of spherical coordinate */
				texcoord.x = static_cast<float>(u) * inv_slices;
				point.phi = texcoord.x * pi_2 * cover_side[i] + pi_0_5;

				/* Convert spherical coordinate into cartesian coordinate and set normal by coordinate */
				const float sin_theta = sinf(point.theta);
				position.x = point.radius * cosf(point.phi) * sin_theta;
				position.y = point.radius * sinf(point.phi) * sin_theta;
				position.z = point.radius * cosf(point.theta);

				std::swap(position.y, position.z);
				position.y *= cover_side[i];

				/* Get normal and move half-sphere */
				float magnitude = sqrtf(position.x * position.x + position.y * position.y + position.z * position.z);
				normal.x = position.x / magnitude;
				normal.y = position.y / magnitude;
				normal.z = position.z / magnitude;

				/* Transform coordiante with radius and height */
				position.x *= radius.x;
				position.y *= radius.y;
				position.z *= radius.z;
				position.y += half_height * cover_side[i];

				//TODO: texCoord wrong for bottom half-sphere!!!
				/* Add new vertex */
				vertices.push_back({ position, normal });
			}
		}
	}

	/* Generate indices for the mantle */
	int offset = base_offset;
	for (uint32_t u = 0; u < slices; ++u)
	{
		for (uint32_t v = 0; v < mantle_stacks; ++v)
		{
			auto i0 = v + 1 + mantle_stacks;
			auto i1 = v;
			auto i2 = v + 1;
			auto i3 = v + 2 + mantle_stacks;

			indices.emplace_back(i0 + offset);
			indices.emplace_back(i1 + offset);
			indices.emplace_back(i3 + offset);
			indices.emplace_back(i1 + offset);
			indices.emplace_back(i2 + offset);
			indices.emplace_back(i3 + offset);
		}
		offset += (1 + mantle_stacks);
	}

	/* Generate indices for the top and bottom */
	for (size_t i = 0; i < 2; ++i)
	{
		for (uint32_t v = 0; v < ellipsoid_stacks; ++v)
		{
			for (uint32_t u = 0; u < slices; ++u)
			{
				/* Compute indices for current face */
				auto i0 = v * (slices + 1) + u;
				auto i1 = v * (slices + 1) + (u + 1);

				auto i2 = (v + 1) * (slices + 1) + (u + 1);
				auto i3 = (v + 1) * (slices + 1) + u;

				/* Add new indices */
				indices.emplace_back(i0 + base_offset_ellipsoid[i]);
				indices.emplace_back(i1 + base_offset_ellipsoid[i]);
				indices.emplace_back(i3 + base_offset_ellipsoid[i]);
				indices.emplace_back(i1 + base_offset_ellipsoid[i]);
				indices.emplace_back(i2 + base_offset_ellipsoid[i]);
				indices.emplace_back(i3 + base_offset_ellipsoid[i]);
			}
		}
	}
	create_com_buffers(device, vertices.data(), vertices.size(), indices.data(), indices.size());
}
