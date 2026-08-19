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

// Live Object Report へ名前を出すために使う。
#include <d3d11sdklayers.h>

static map<wstring, ComPtr<ID3D11ShaderResourceView>> resources;

// Debug Build でだけ SRV へ名前を付ける。
//
// Live Object Report に名前が出ないと、残った SRV がどのテクスチャなのか、
// どのキャッシュが握っているのかを追えない。
// 名前は "TextureCache:<パス>" にして、キャッシュ由来だと一目で分かるようにする。
// Dummy Texture 用。エンジンが自分で生成した SRV にだけ、生成直後に 1 回だけ呼ぶ。
//
// 外部 Loader (DirectXTK) が返した SRV には呼ばないこと。
// あちらは生成時に自分で名前を付けており、後から別名を設定すると
// SETPRIVATEDATA_CHANGINGPARAMS の警告になる。
static void set_dummy_texture_debug_name(ID3D11ShaderResourceView* view, const wstring& key);

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
			if (SUCCEEDED(hr))
			{
				// ここでデバッグ名を付けない。
				//
				// この SRV を生成したのは DirectXTK の WIC / DDS Loader で、
				// 生成側が既に WKPDID_D3DDebugObjectName を設定している。
				// そこへ長さの違う名前を付け直すと D3D11 が
				//   SETPRIVATEDATA_CHANGINGPARAMS (STATE_SETTING WARNING #55)
				// を出す。実際にこの経路で 1 テクスチャにつき 1 件出ていた。
				//
				// 名前は「生成した所有者が 1 回だけ決める」方針にする。
				// 外部 Loader が返した SRV はエンジン側で再命名しない。
				// 自前で生成する Dummy Texture だけを命名する。
				resources[key] = loaded_view;
			}
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
static void set_dummy_texture_debug_name(ID3D11ShaderResourceView* view, const wstring& key)
{
#if defined(_DEBUG) || defined(DEBUG)
	if (view == nullptr) return;

	// 既存名の問い合わせによるガードは置かない。
	//
	// この SRV は直前の CreateShaderResourceView でエンジンが生成したもので、
	// 名前を付けるのはこの 1 箇所だけ。誰も先に付けていないことが
	// 呼び出し構造から保証されている。
	// 「既にあれば付けない」というガードは、外部 Loader が生成した SRV へ
	// 再命名しようとするから必要になるものであり、その設計自体をやめた。
	std::string name = "DummyTexture:";
	for (wchar_t character : key)
	{
		// キーは 16 進数と '.' だけなので、そのまま 1 バイトへ落とせる。
		name.push_back(static_cast<char>(character));
	}
	view->SetPrivateData(WKPDID_D3DDebugObjectName,
		static_cast<UINT>(name.size()), name.c_str());
#else
	(void)view;
	(void)key;
#endif
}

std::uint64_t estimate_texture2d_bytes(const D3D11_TEXTURE2D_DESC& desc)
{
	auto surface_bytes = [](DXGI_FORMAT format, std::uint32_t width,
		std::uint32_t height) noexcept -> std::uint64_t
	{
		const std::uint64_t w = (std::max)(std::uint64_t{ 1 },
			static_cast<std::uint64_t>(width));
		const std::uint64_t h = (std::max)(std::uint64_t{ 1 },
			static_cast<std::uint64_t>(height));
		switch (format)
		{
		case DXGI_FORMAT_BC1_UNORM:
		case DXGI_FORMAT_BC1_UNORM_SRGB:
		case DXGI_FORMAT_BC4_UNORM:
		case DXGI_FORMAT_BC4_SNORM:
			return ((w + 3u) / 4u) * ((h + 3u) / 4u) * 8u;
		case DXGI_FORMAT_BC2_UNORM:
		case DXGI_FORMAT_BC2_UNORM_SRGB:
		case DXGI_FORMAT_BC3_UNORM:
		case DXGI_FORMAT_BC3_UNORM_SRGB:
		case DXGI_FORMAT_BC5_UNORM:
		case DXGI_FORMAT_BC5_SNORM:
		case DXGI_FORMAT_BC6H_UF16:
		case DXGI_FORMAT_BC6H_SF16:
		case DXGI_FORMAT_BC7_UNORM:
		case DXGI_FORMAT_BC7_UNORM_SRGB:
			return ((w + 3u) / 4u) * ((h + 3u) / 4u) * 16u;
		case DXGI_FORMAT_R32G32B32A32_FLOAT:
			return w * h * 16u;
		case DXGI_FORMAT_R16G16B16A16_FLOAT:
		case DXGI_FORMAT_R16G16B16A16_UNORM:
			return w * h * 8u;
		case DXGI_FORMAT_R8_UNORM:
			return w * h;
		case DXGI_FORMAT_R8G8_UNORM:
		case DXGI_FORMAT_R16_FLOAT:
			return w * h * 2u;
		default:
			return w * h * 4u;
		}
	};

	const std::uint32_t mip_count = (std::max)(1u, desc.MipLevels);
	std::uint64_t total = 0;
	std::uint32_t width = desc.Width;
	std::uint32_t height = desc.Height;
	for (std::uint32_t mip = 0; mip < mip_count; ++mip)
	{
		total += surface_bytes(desc.Format, width, height) *
			static_cast<std::uint64_t>((std::max)(1u, desc.ArraySize));
		width = (std::max)(1u, width / 2u);
		height = (std::max)(1u, height / 2u);
	}
	return total;
}

std::uint64_t texture_cache_resident_bytes()
{
	lock_guard<mutex> lock(resources_mutex);
	std::uint64_t total = 0;
	for (const auto& entry : resources)
	{
		if (!entry.second) continue;
		ComPtr<ID3D11Resource> resource;
		entry.second->GetResource(resource.GetAddressOf());
		ComPtr<ID3D11Texture2D> texture;
		if (!resource || FAILED(resource.As(&texture)) || !texture) continue;
		D3D11_TEXTURE2D_DESC desc{};
		texture->GetDesc(&desc);
		total += estimate_texture2d_bytes(desc);
	}
	return total;
}

std::size_t texture_cache_resident_count()
{
	lock_guard<mutex> lock(resources_mutex);
	return resources.size();
}

void release_all_textures()
{
	// このキャッシュはファイルスコープの static なので、
	// 何もしなければ破棄されるのは main() が返ったあと。
	// ID3D11Debug::ReportLiveDeviceObjects はそれより前に走るため、
	// ここを呼ばないと SRV が必ず Live Object として残る。
	// 終了処理から Device より先に明示的に呼ぶこと。
	lock_guard<mutex> lock(resources_mutex);
	resources.clear();

	// 読み込み途中の状態も一緒に片付ける。
	// 完了済みの state が SRV を握ったまま残ると、同じ理由で解放されない。
	loading_resources.clear();
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

		// 生成直後に 1 回だけ名前を付ける。
		// この SRV は自前で作っているので他所の名前と衝突しない。
		set_dummy_texture_debug_name(*shader_resource_view, keyname.str());

		resources.insert(std::make_pair(keyname.str().c_str(), *shader_resource_view));
	}
	return hr;
}
