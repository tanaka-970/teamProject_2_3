#include "misc.h"
#include "skinned_mesh.h"
#include<fstream>
#include <sstream>
#include <functional>
#include <algorithm>
#include"shader.h"
#include"static_mesh.h"
#include"sprite_batch.h"

#include"texture.h"
#include"../render/motion_vector_context.h"
#include"../../RePlayEngine/Rendering/RenderStats.h"
#include <cstring>
#include <filesystem>
#include <stdexcept>
using namespace DirectX;

void skinned_mesh::create_com_objects(ID3D11Device* device, const char* fbx_filename)
{
    // DX12のCPU先読みや検証経路ではD3D11デバイスを持たないため、
    // GPU資源生成を要求された場合でもnullデバイスを参照しない。
    if (device == nullptr) return;

    for (mesh& mesh : meshes)
    {
        HRESULT hr{ S_OK };
        D3D11_BUFFER_DESC buffer_desc{};
        D3D11_SUBRESOURCE_DATA subresource_data{};

        buffer_desc.ByteWidth = static_cast<UINT>(sizeof(vertex) * mesh.vertices.size());
        buffer_desc.Usage = D3D11_USAGE_DEFAULT;
        buffer_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        buffer_desc.CPUAccessFlags = 0;
        buffer_desc.MiscFlags = 0;
        buffer_desc.StructureByteStride = 0;
        subresource_data.pSysMem = mesh.vertices.data();
        subresource_data.SysMemPitch = 0;
        subresource_data.SysMemSlicePitch = 0;
        hr = device->CreateBuffer(&buffer_desc, &subresource_data, mesh.vertex_buffer.ReleaseAndGetAddressOf());
        _ASSERT_EXPR(SUCCEEDED(hr), hr_trace(hr));

        buffer_desc.ByteWidth = static_cast<UINT>(sizeof(uint32_t) * mesh.indices.size());
        buffer_desc.Usage = D3D11_USAGE_DEFAULT;
        buffer_desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
        subresource_data.pSysMem = mesh.indices.data();
        hr = device->CreateBuffer(&buffer_desc, &subresource_data, mesh.index_buffer.ReleaseAndGetAddressOf());
        _ASSERT_EXPR(SUCCEEDED(hr), hr_trace(hr));

        // 衝突Cookを後から実行できるよう、CPU側の頂点とインデックスを保持する。
    }

    HRESULT hr{ S_OK };
    D3D11_INPUT_ELEMENT_DESC input_element_desc[]
    {
         { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
     { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },

     // ★順番を TANGENT → TEXCOORD に入れ替える！
     { "TANGENT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
     { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },

     { "WEIGHTS", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
     { "BONES", 0, DXGI_FORMAT_R32G32B32A32_UINT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
     { "MORPHPOS", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
     { "MORPHNORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
     };

    create_vs_from_cso(device, "skinned_mesh_vs.cso", vertex_shader.ReleaseAndGetAddressOf(),
        input_layout.ReleaseAndGetAddressOf(), input_element_desc, ARRAYSIZE(input_element_desc));

    create_ps_from_cso(device, "skinned_mesh_ps.cso", pixel_shader.ReleaseAndGetAddressOf());

    D3D11_BUFFER_DESC buffer_desc{};
    buffer_desc.Usage = D3D11_USAGE_DEFAULT;
    buffer_desc.ByteWidth = sizeof(constants);
    buffer_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    hr = device->CreateBuffer(&buffer_desc, nullptr, constant_buffer.ReleaseAndGetAddressOf());
    _ASSERT_EXPR(SUCCEEDED(hr), hr_trace(hr));

    // TAAのモーションベクター用の定数バッファ(b6/b8)。
    buffer_desc.ByteWidth = sizeof(motion_vectors::ObjectConstants);
    hr = device->CreateBuffer(&buffer_desc, nullptr,
        motion_object_constant_buffer.ReleaseAndGetAddressOf());
    _ASSERT_EXPR(SUCCEEDED(hr), hr_trace(hr));
    buffer_desc.ByteWidth = sizeof(motion_bone_constants);
    hr = device->CreateBuffer(&buffer_desc, nullptr,
        motion_bone_constant_buffer.ReleaseAndGetAddressOf());
    _ASSERT_EXPR(SUCCEEDED(hr), hr_trace(hr));

    for (std::unordered_map<uint64_t, material>::iterator iterator = materials.begin();
        iterator != materials.end(); ++iterator)
    {
        // 0=BaseColor / 1=Normal / 2=ORM / 3=Emissive。
        // FBX は従来どおり先頭2枚だけを持ち、後半は中立Dummyになる。
        for (size_t texture_index = 0; texture_index < 4; ++texture_index)
        {
            // ファイル名が格納されているかチェック [cite: 67]
            if (iterator->second.texture_filenames[texture_index].size() > 0)
            {
                std::filesystem::path texture_path(iterator->second.texture_filenames[texture_index]);
                std::filesystem::path path;
                if (texture_path.is_absolute())
                    path = texture_path;
                else
                {
                    path = std::filesystem::path(fbx_filename).parent_path() / texture_path;
                }

                D3D11_TEXTURE2D_DESC texture2d_desc;
                load_texture_from_file(device, path.c_str(),
                    iterator->second.shader_resource_views[texture_index].ReleaseAndGetAddressOf(), &texture2d_desc);
            }
            else
            {
                // ファイル名がない場合はダミーを生成 [cite: 87]
                DWORD dummy = 0xFFFFFFFF;
                if (texture_index == 1) dummy = 0xFFFF7F7F;
                else if (texture_index == 2) dummy = 0xFF00FFFF; // AO=1 Roughness=1 Metal=0
                else if (texture_index == 3) dummy = 0xFF000000; // Emissive=0
                make_dummy_texture(device,
                    iterator->second.shader_resource_views[texture_index].ReleaseAndGetAddressOf(),
                    dummy, 16);
            }
        }
    }
}

void skinned_mesh::release_device_resources()
{
	for (mesh& m : meshes)
	{
		m.vertex_buffer.Reset();
		m.index_buffer.Reset();
	}

	for (auto& kv : materials)
	{
		for (auto& srv : kv.second.shader_resource_views)
		{
			srv.Reset();
		}
	}
	materials.clear();

	vertex_shader.Reset();
	pixel_shader.Reset();
	input_layout.Reset();
	constant_buffer.Reset();
	motion_object_constant_buffer.Reset();
	motion_bone_constant_buffer.Reset();
}
