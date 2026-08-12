#include "shader.h"
#include "misc.h"
#include "geometric_primitive.h"

#include <vector>

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
