#include "shader.h"
#include "misc.h"
#include "geometric_primitive.h"

#include <vector>

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
