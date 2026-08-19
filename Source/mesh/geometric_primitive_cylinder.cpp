#include "shader.h"
#include "misc.h"
#include "geometric_primitive.h"

#include <vector>

// UNIT.12
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
