#pragma once

#include <d3d11.h>
#include <cstddef>
#include <cstdint>

HRESULT load_texture_from_file(ID3D11Device* device, const wchar_t* filename, ID3D11ShaderResourceView** shader_resource_view, D3D11_TEXTURE2D_DESC* texture2d_desc);
void release_all_textures();
std::uint64_t estimate_texture2d_bytes(const D3D11_TEXTURE2D_DESC& desc);
std::uint64_t texture_cache_resident_bytes();
std::size_t texture_cache_resident_count();
// UNIT.16
HRESULT make_dummy_texture(ID3D11Device* device, ID3D11ShaderResourceView** shader_resource_view, DWORD value/*0xAABBGGRR*/, UINT dimension);

