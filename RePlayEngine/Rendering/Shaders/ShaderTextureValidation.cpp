#include "ShaderTextureValidation.h"
#include "BuiltInShaders.h"
#include "ShaderConstantPacker.h"
#include "ShaderLibrary.h"
#include "../Materials/MaterialBinding.h"
#include "../Materials/MaterialGpuBinder.h"
#include "../../Assets/AssetDatabase.h"

#include <d3d11.h>
#include <wrl.h>

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
        check.Expect(!ResolvedMaterialBinding::TryGetGBufferBridgeSlot(
            "RampMap", bridge_slot),
            "RampMapはGBuffer semantic bridgeへ入らない");

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

        // Phase 12 はCPU上のslot解決だけでなく、Windows実機で4種の1x1 SRVを
        // 本当に作れるところまで検査する。Debug Layerは要求せず、Hardwareが
        // 利用できない環境ではWARPへフォールバックする。
        Microsoft::WRL::ComPtr<ID3D11Device> device;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
        HRESULT device_result = D3D11CreateDevice(nullptr,
            D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
            D3D11_SDK_VERSION, device.GetAddressOf(), nullptr,
            context.GetAddressOf());
        if (FAILED(device_result))
        {
            device.Reset();
            context.Reset();
            device_result = D3D11CreateDevice(nullptr,
                D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
                D3D11_SDK_VERSION, device.GetAddressOf(), nullptr,
                context.GetAddressOf());
        }
        check.Expect(SUCCEEDED(device_result) && device && context,
            "D3D11 HardwareまたはWARP deviceを作れる");

        if (device && context)
        {
            int gpu_log_count = 0;
            MaterialGpuBinder gpu;
            check.Expect(gpu.Initialize(device.Get(),
                [&gpu_log_count](const std::string&, const std::string&)
                {
                    ++gpu_log_count;
                }), "MaterialGpuBinderを初期化できる");
            check.Expect(gpu.DefaultTexturesReady(),
                "white/black/gray/bumpの4種の既定SRVを作れる");

            Assets::AssetDatabase empty_assets(
                std::filesystem::path("Saved/Validation/ShaderTexture/empty.replaydb"));
            ResolvedMaterialBinding defaults;
            defaults.textures = {
                { "BaseMap", 40, "", "white" },
                { "EmissiveMap", 41, "", "black" },
                { "MaskMap", 42, "", "gray" },
                { "NormalMap", 43, "", "bump" },
            };
            check.Expect(gpu.BindTextures(device.Get(), context.Get(),
                empty_assets, defaults),
                "未設定textureを4種の既定SRVへbindできる");
            gpu.UnbindTextures(context.Get());

            ResolvedMaterialBinding missing_asset;
            missing_asset.textures = {
                { "BaseMap", 40, "ffffffffffffffffffffffffffffffff", "white" },
            };
            const int logs_before_missing = gpu_log_count;
            check.Expect(gpu.BindTextures(device.Get(), context.Get(),
                empty_assets, missing_asset),
                "存在しないAssetGUIDでも既定SRVで描画を継続できる");
            check.Expect(gpu_log_count > logs_before_missing,
                "存在しないAssetGUIDを黙って落とさず診断する");
            check.Expect(gpu.CachedTextureCount() == 0,
                "失敗textureを成功cacheへ入れない");
            gpu.Unbind(context.Get());
            gpu.Clear();
            context->ClearState();
            context->Flush();
        }

        return check.Report();
    }
}
