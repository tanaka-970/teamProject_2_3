#include "ShaderTextureValidation.h"
#include "BuiltInShaders.h"
#include "ShaderConstantPacker.h"
#include "ShaderLibrary.h"
#include "../Materials/MaterialBinding.h"
#include "../../Assets/AssetDatabase.h"

#include "../DX12/D3D12ShaderCompiler.h"

#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3d12shader.h>
#include <dxcapi.h>
#include <wrl.h>

#include <array>
#include <cstdio>
#include <filesystem>
#include <set>
#include <string>

namespace ReplayEngine::Rendering::Validation
{
    namespace
    {
        class Check final
        {
        public:
            void Expect(bool value, const char* text)
            {
                ++total_; if (value) return; ++failed_;
                std::fprintf(stderr, "  [FAIL %d] %s\n", 1450 + total_ - 1, text);
            }
            int Report() const
            {
                std::fprintf(stderr, "shader-texture %s: %d checks, %d failed\n",
                    failed_ == 0 ? "OK" : "FAILED", total_, failed_);
                return failed_ == 0 ? 0 : 1450;
            }
        private: int total_ = 0; int failed_ = 0;
        };

        const ResolvedMaterialTexture* Find(const ResolvedMaterialBinding& binding,
            const char* name)
        {
            for (const auto& item : binding.textures)
                if (item.property_name == name) return &item;
            return nullptr;
        }
    }

    int RunShaderTextureValidation()
    {
        Check check;
        ShaderLibrary library;
        const auto report = library.ScanAll(std::filesystem::current_path());
        check.Expect(report.compile_failed == 0, "Shader Catalogを構築できる");

        MaterialAsset material;
        material.shader_guid = BuiltInShaders::Pbr.ToString();
        material.SyncLegacyFieldsToProperties();
        material.properties.Set("prop.BaseMap",
            Reflection::PropertyValue::MakeAssetReference("base-asset-guid"));
        material.properties.Set("prop.NormalMap",
            Reflection::PropertyValue::MakeAssetReference("normal-asset-guid"));
        material.properties.Set("prop.OcclusionMap",
            Reflection::PropertyValue::MakeAssetReference("occlusion-asset-guid"));

        ResolvedMaterialBinding binding;
        check.Expect(MaterialBindingResolver::Resolve(material, library.Catalog(),
            ShaderVariant::Static, binding), "PBR texture bindingを解決できる");
        check.Expect(binding.textures.size() == 6, "PBR texture propertyが6件ある");

        std::uint32_t previous = 0;
        std::set<std::uint32_t> unique;
        bool ordered = true;
        bool safe = true;
        for (const auto& texture : binding.textures)
        {
            if (previous != 0 && texture.slot <= previous) ordered = false;
            previous = texture.slot;
            safe = safe && texture.slot >=
                ShaderConstantPacker::material_texture_base_slot;
            unique.insert(texture.slot);
        }
        check.Expect(ordered, "textureをslot順へ並べる");
        check.Expect(safe, "全textureがt40以降にある");
        check.Expect(unique.size() == binding.textures.size(), "texture slotが重複しない");

        const auto* base = Find(binding, "BaseMap");
        const auto* normal = Find(binding, "NormalMap");
        const auto* emissive = Find(binding, "EmissiveMap");
        const auto* occlusion = Find(binding, "OcclusionMap");
        check.Expect(base && base->asset_guid == "base-asset-guid", "BaseMap GUIDを保持する");
        check.Expect(normal && normal->asset_guid == "normal-asset-guid", "NormalMap GUIDを保持する");
        check.Expect(normal && normal->default_texture == "bump", "NormalMap既定はbump");
        check.Expect(emissive && emissive->asset_guid.empty() &&
            emissive->default_texture == "black", "未設定Emissiveはblackへ落ちる");
        check.Expect(occlusion && occlusion->asset_guid == "occlusion-asset-guid",
            "OcclusionMapの新保存名を解決する");

        const std::uint32_t mask = binding.TextureSemanticMask();
        check.Expect((mask & ResolvedMaterialBinding::BaseMapSemantic) != 0,
            "GBuffer semantic maskにBaseMapが入る");
        check.Expect((mask & ResolvedMaterialBinding::NormalMapSemantic) != 0,
            "GBuffer semantic maskにNormalMapが入る");
        check.Expect((mask & ResolvedMaterialBinding::OcclusionMapSemantic) != 0,
            "GBuffer semantic maskにOcclusionMapが入る");
        std::uint32_t bridge_slot = 0;
        check.Expect(ResolvedMaterialBinding::TryGetGBufferBridgeSlot(
            "BaseMap", bridge_slot) && bridge_slot == 40,
            "BaseMapは宣言順に関係なくGBuffer t40へ再配置される");
        check.Expect(ResolvedMaterialBinding::TryGetGBufferBridgeSlot(
            "OcclusionMap", bridge_slot) && bridge_slot == 45,
            "OcclusionMapはGBuffer t45へ再配置される");
        check.Expect(ResolvedMaterialBinding::TryGetGBufferBridgeSlot(
            "RampMap", bridge_slot) && bridge_slot == 46,
            "RampMapはGBuffer t46へ再配置される");

        MaterialAsset toon;
        toon.shader_guid = BuiltInShaders::Toon.ToString();
        toon.SyncLegacyFieldsToProperties();
        ResolvedMaterialBinding toon_binding;
        check.Expect(MaterialBindingResolver::Resolve(toon, library.Catalog(),
            ShaderVariant::Static, toon_binding), "Toon texture bindingを解決できる");
        check.Expect((toon_binding.TextureSemanticMask() &
            ResolvedMaterialBinding::NormalMapSemantic) == 0,
            "Toonのt41 RampMapをNormalMapと誤認しない");

        MaterialAsset old_alias = material;
        old_alias.properties.Remove("prop.OcclusionMap");
        old_alias.properties.Set("prop.AmbientOcclusionMap",
            Reflection::PropertyValue::MakeAssetReference("old-ao-guid"));
        ResolvedMaterialBinding alias_binding;
        check.Expect(MaterialBindingResolver::Resolve(old_alias, library.Catalog(),
            ShaderVariant::Static, alias_binding), "旧AO保存名も解決できる");
        const auto* alias = Find(alias_binding, "OcclusionMap");
        check.Expect(alias && alias->asset_guid == "old-ao-guid",
            "AmbientOcclusionMap aliasを値ごと救済する");

        // GPU 側は DX12 の 1x1 fallback resource と DXC reflection を検査する。
        Microsoft::WRL::ComPtr<IDXGIFactory6> factory;
        Microsoft::WRL::ComPtr<ID3D12Device> device;
        HRESULT device_result = CreateDXGIFactory2(0, IID_PPV_ARGS(&factory));
        if (SUCCEEDED(device_result) && factory)
        {
            for (UINT index = 0; ; ++index)
            {
                Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
                if (factory->EnumAdapters1(index, &adapter) == DXGI_ERROR_NOT_FOUND) break;
                DXGI_ADAPTER_DESC1 desc{};
                adapter->GetDesc1(&desc);
                if ((desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0) continue;
                if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0,
                    IID_PPV_ARGS(&device)))) break;
            }
            if (!device)
            {
                Microsoft::WRL::ComPtr<IDXGIAdapter> warp;
                if (SUCCEEDED(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp))))
                    device_result = D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_11_0,
                        IID_PPV_ARGS(&device));
            }
        }
        check.Expect(SUCCEEDED(device_result) && device != nullptr,
            "DX12 HardwareまたはWARP deviceを作れる");

        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> heap;
        if (device)
        {
            D3D12_DESCRIPTOR_HEAP_DESC heap_desc{};
            heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
            heap_desc.NumDescriptors = 4;
            device->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&heap));
        }
        check.Expect(heap != nullptr, "fallback texture用SRV heapを作れる");

        std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, 4> fallback_resources{};
        bool fallback_ok = device != nullptr && heap != nullptr;
        if (fallback_ok)
        {
            const D3D12_HEAP_PROPERTIES properties{ D3D12_HEAP_TYPE_DEFAULT };
            D3D12_RESOURCE_DESC desc{};
            desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            desc.Width = 1;
            desc.Height = 1;
            desc.DepthOrArraySize = 1;
            desc.MipLevels = 1;
            desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            desc.SampleDesc.Count = 1;
            desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
            const UINT stride = device->GetDescriptorHandleIncrementSize(
                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            D3D12_CPU_DESCRIPTOR_HANDLE handle = heap->GetCPUDescriptorHandleForHeapStart();
            for (std::size_t index = 0; index < fallback_resources.size(); ++index)
            {
                if (FAILED(device->CreateCommittedResource(&properties, D3D12_HEAP_FLAG_NONE,
                    &desc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr,
                    IID_PPV_ARGS(&fallback_resources[index]))))
                {
                    fallback_ok = false;
                    break;
                }
                D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
                srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                srv.Format = desc.Format;
                srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                srv.Texture2D.MipLevels = 1;
                device->CreateShaderResourceView(fallback_resources[index].Get(), &srv, handle);
                handle.ptr += stride;
            }
        }
        check.Expect(fallback_ok, "white/black/gray/bump用の4つのDX12 fallback resourceを作れる");

        DX12::D3D12ShaderCompiler compiler;
        const std::filesystem::path dxc_path = DX12::D3D12ShaderCompiler::FindDefaultLibraryPath();
        check.Expect(compiler.Initialize(dxc_path), "DXCを初期化できる");
        const std::string reflection_source =
            "Texture2D BaseMap : register(t40);\n"
            "Texture2D NormalMap : register(t43);\n"
            "SamplerState LinearSampler : register(s0);\n"
            // 参照しない Texture は DXC が落とすため、両方を式へ入れて reflection に残す。
            "float4 main(float4 p:SV_POSITION):SV_TARGET{"
            "return BaseMap.Load(int3(0,0,0)) + NormalMap.Load(int3(0,0,0));}\n";
        const auto reflected_bytecode = compiler.CompileSource(reflection_source,
            "ShaderTextureReflection.hlsl", L"main", L"ps_6_0", false);
        check.Expect(reflected_bytecode.succeeded && !reflected_bytecode.bytecode.empty(),
            "DXCでtexture binding検証shaderをコンパイルできる");

        Microsoft::WRL::ComPtr<ID3D12ShaderReflection> reflection;
        HMODULE dxc_module = dxc_path.empty() ? nullptr : LoadLibraryW(dxc_path.wstring().c_str());
        if (dxc_module && reflected_bytecode.succeeded)
        {
            using CreateProc = HRESULT(WINAPI*)(REFCLSID, REFIID, LPVOID*);
            const auto create = reinterpret_cast<CreateProc>(GetProcAddress(dxc_module,
                "DxcCreateInstance"));
            Microsoft::WRL::ComPtr<IDxcUtils> utils;
            Microsoft::WRL::ComPtr<IDxcContainerReflection> container;
            Microsoft::WRL::ComPtr<IDxcBlobEncoding> blob;
            if (create && SUCCEEDED(create(CLSID_DxcUtils, IID_PPV_ARGS(&utils))) &&
                SUCCEEDED(create(CLSID_DxcContainerReflection, IID_PPV_ARGS(&container))) &&
                SUCCEEDED(utils->CreateBlob(reflected_bytecode.bytecode.data(),
                    static_cast<UINT32>(reflected_bytecode.bytecode.size()), DXC_CP_ACP, &blob)) &&
                SUCCEEDED(container->Load(blob.Get())))
            {
                UINT32 part = 0;
                if (SUCCEEDED(container->FindFirstPartKind(DXC_PART_REFLECTION_DATA, &part)))
                    container->GetPartReflection(part, IID_PPV_ARGS(&reflection));
            }
        }
        check.Expect(reflection != nullptr, "DXC containerからD3D12 shader reflectionを取得できる");

        D3D12_SHADER_INPUT_BIND_DESC base_desc{};
        D3D12_SHADER_INPUT_BIND_DESC normal_desc{};
        const bool base_reflected = reflection && SUCCEEDED(reflection->GetResourceBindingDescByName(
            "BaseMap", &base_desc));
        const bool normal_reflected = reflection && SUCCEEDED(reflection->GetResourceBindingDescByName(
            "NormalMap", &normal_desc));
        check.Expect(base_reflected && base_desc.BindPoint == 40,
            "BaseMapがDX12 reflectionでもt40にある");
        check.Expect(normal_reflected && normal_desc.BindPoint == 43,
            "NormalMapがDX12 reflectionでもt43にある");
        if (dxc_module) FreeLibrary(dxc_module);

        return check.Report();
    }
}
