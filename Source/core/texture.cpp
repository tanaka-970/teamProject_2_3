// UNIT.10
#include "texture.h"
#include "misc.h"

#include "WICTextureLoader.h"
using namespace DirectX;

#include <wrl.h>
using namespace Microsoft::WRL;

#include <string>
#include <map>
#include <condition_variable>
#include <memory>
#include <mutex>
using namespace std;
#include <filesystem> // 追加
#include "DDSTextureLoader.h" // 追加（プロジェクトに含まれている前提）
// UNIT.16
#include <sstream>
#include <iomanip>

static map<wstring, ComPtr<ID3D11ShaderResourceView>> resources;
struct texture_load_state
{
	condition_variable ready;
	bool completed{ false };
	HRESULT result{ E_FAIL };
	ComPtr<ID3D11ShaderResourceView> resource;
};
static map<wstring, shared_ptr<texture_load_state>> loading_resources;
static mutex resources_mutex;

HRESULT load_texture_from_file(ID3D11Device* device, const wchar_t* filename, ID3D11ShaderResourceView** shader_resource_view, D3D11_TEXTURE2D_DESC* texture2d_desc)
{
	if (!device || !filename || !shader_resource_view) return E_INVALIDARG;
	const wstring key = std::filesystem::path(filename).lexically_normal().wstring();
	shared_ptr<texture_load_state> loading;
	ComPtr<ID3D11ShaderResourceView> loaded_view;
	bool should_load = false;
	{
		unique_lock<mutex> lock(resources_mutex);
		if (const auto cached = resources.find(key); cached != resources.end())
		{
			loaded_view = cached->second;
		}
		else if (const auto active = loading_resources.find(key); active != loading_resources.end())
		{
			loading = active->second;
			loading->ready.wait(lock, [&loading] { return loading->completed; });
			if (FAILED(loading->result)) return loading->result;
			loaded_view = loading->resource;
		}
		else
		{
			loading = make_shared<texture_load_state>();
			loading_resources.emplace(key, loading);
			should_load = true;
		}
	}

	HRESULT hr = S_OK;
	ComPtr<ID3D11Resource> loaded_resource;
	if (should_load)
	{
		std::filesystem::path dds_filename(filename);
		dds_filename.replace_extension("dds");
		if (std::filesystem::exists(dds_filename))
			hr = CreateDDSTextureFromFile(device, dds_filename.c_str(),
				loaded_resource.GetAddressOf(), loaded_view.GetAddressOf());
		else
			hr = CreateWICTextureFromFile(device, filename,
				loaded_resource.GetAddressOf(), loaded_view.GetAddressOf());

		{
			lock_guard<mutex> lock(resources_mutex);
			loading->result = hr;
			loading->resource = loaded_view;
			loading->completed = true;
			if (SUCCEEDED(hr)) resources[key] = loaded_view;
			loading_resources.erase(key);
		}
		loading->ready.notify_all();
		if (FAILED(hr)) return hr;
	}
	else
	{
		loaded_view->GetResource(loaded_resource.GetAddressOf());
	}

	ComPtr<ID3D11Texture2D> texture2d;
	hr = loaded_resource.As(&texture2d);
	if (FAILED(hr)) return hr;
	if (texture2d_desc) texture2d->GetDesc(texture2d_desc);
	return loaded_view.CopyTo(shader_resource_view);
}
void release_all_textures()
{
	lock_guard<mutex> lock(resources_mutex);
	resources.clear();
}
// UNIT.16
HRESULT make_dummy_texture(ID3D11Device* device, ID3D11ShaderResourceView** shader_resource_view, DWORD value/*0xAABBGGRR*/, UINT dimension)
{
	lock_guard<mutex> lock(resources_mutex);
	HRESULT hr{ S_OK };

	wstringstream keyname;
	keyname << setw(8) << setfill(L'0') << hex << uppercase << value << L"." << dec << dimension;
	map<wstring, ComPtr<ID3D11ShaderResourceView>>::iterator it = resources.find(keyname.str().c_str());
	if (it != resources.end())
	{
		*shader_resource_view = it->second.Get();
		(*shader_resource_view)->AddRef();
	}
	else
	{
		D3D11_TEXTURE2D_DESC texture2d_desc{};
		texture2d_desc.Width = dimension;
		texture2d_desc.Height = dimension;
		texture2d_desc.MipLevels = 1;
		texture2d_desc.ArraySize = 1;
		texture2d_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		texture2d_desc.SampleDesc.Count = 1;
		texture2d_desc.SampleDesc.Quality = 0;
		texture2d_desc.Usage = D3D11_USAGE_DEFAULT;
		texture2d_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		texture2d_desc.CPUAccessFlags = 0;
		texture2d_desc.MiscFlags = 0;

		size_t texels = static_cast<size_t>(dimension) * dimension;
		unique_ptr<DWORD[]> sysmem{ make_unique< DWORD[]>(texels) };
		for (size_t i = 0; i < texels; ++i)
		{
			sysmem[i] = value;
		}

		D3D11_SUBRESOURCE_DATA subresource_data{};
		subresource_data.pSysMem = sysmem.get();
		subresource_data.SysMemPitch = sizeof(DWORD) * dimension;
		subresource_data.SysMemSlicePitch = 0;

		ComPtr<ID3D11Texture2D> texture2d;
		hr = device->CreateTexture2D(&texture2d_desc, &subresource_data, &texture2d);
		_ASSERT_EXPR(SUCCEEDED(hr), hr_trace(hr));

		D3D11_SHADER_RESOURCE_VIEW_DESC shader_resource_view_desc{};
		shader_resource_view_desc.Format = texture2d_desc.Format;
		shader_resource_view_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		shader_resource_view_desc.Texture2D.MipLevels = 1;

		hr = device->CreateShaderResourceView(texture2d.Get(), &shader_resource_view_desc, shader_resource_view);
		_ASSERT_EXPR(SUCCEEDED(hr), hr_trace(hr));
		resources.insert(std::make_pair(keyname.str().c_str(), *shader_resource_view));
	}
	return hr;
}
