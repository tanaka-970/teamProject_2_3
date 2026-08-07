#include "MaterialGpuBinder.h"

#include "../Shaders/ShaderConstantPacker.h"
#include "../Shaders/ShaderCatalog.h"

#include <d3d11sdklayers.h>
#include <d3dcompiler.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <utility>

namespace ReplayEngine::Rendering
{
    namespace
    {
        constexpr std::uint32_t Align16(std::uint32_t value) noexcept
        {
            return (value + 15u) & ~15u;
        }

        void SetDebugName(ID3D11DeviceChild* object, const char* name)
        {
#if defined(_DEBUG) || defined(DEBUG)
            if (object == nullptr || name == nullptr || *name == '\0') return;
            object->SetPrivateData(WKPDID_D3DDebugObjectName,
                static_cast<UINT>(std::strlen(name)), name);
#else
            (void)object;
            (void)name;
#endif
        }
    }

    bool MaterialGpuBinder::Initialize(ID3D11Device* device, LogSink log)
    {
        Clear();
        log_ = std::move(log);
        if (device == nullptr)
        {
            if (log_) log_("Error", "MaterialGpuBinder: D3D11 device がありません");
            return false;
        }

        if (!CreateDefaultTextures(device))
        {
            if (log_) log_("Error", "MaterialGpuBinder: 既定テクスチャを作れません");
            Clear();
            return false;
        }

        initialized_ = true;
        return true;
    }

    void MaterialGpuBinder::Clear() noexcept
    {
        initialized_ = false;
        constant_buffer_bound_ = false;
        material_constant_buffer_.Reset();
        material_constant_buffer_size_ = 0;

        default_white_.Reset();
        default_black_.Reset();
        default_gray_.Reset();
        default_bump_.Reset();

        shader_cache_.clear();
        texture_cache_.clear();
        bound_texture_slots_.clear();
        shader_failures_.clear();
        texture_failures_.clear();
        default_name_failures_.clear();
        log_ = {};
    }

    std::string MaterialGpuBinder::ShaderCacheKey(ShaderID id,
        ShaderVariant variant)
    {
        return id.ToString() + (variant == ShaderVariant::Skinned
            ? ":Skinned" : ":Static");
    }

    void MaterialGpuBinder::LogOnce(std::unordered_set<std::string>& seen,
        const std::string& key, const std::string& severity,
        const std::string& message)
    {
        if (!seen.insert(key).second) return;
        if (log_) log_(severity, message);
    }

    bool MaterialGpuBinder::CreateSolidTexture(ID3D11Device* device,
        std::uint32_t rgba, const char* debug_name,
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>& out)
    {
        out.Reset();
        if (device == nullptr) return false;

        D3D11_TEXTURE2D_DESC description{};
        description.Width = 1;
        description.Height = 1;
        description.MipLevels = 1;
        description.ArraySize = 1;
        description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        description.SampleDesc.Count = 1;
        description.Usage = D3D11_USAGE_IMMUTABLE;
        description.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        D3D11_SUBRESOURCE_DATA initial{};
        initial.pSysMem = &rgba;
        initial.SysMemPitch = sizeof(rgba);

        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
        HRESULT result = device->CreateTexture2D(&description, &initial,
            texture.GetAddressOf());
        if (FAILED(result)) return false;

        result = device->CreateShaderResourceView(texture.Get(), nullptr,
            out.GetAddressOf());
        if (FAILED(result)) return false;

        SetDebugName(texture.Get(), debug_name);
        SetDebugName(out.Get(), debug_name);
        return true;
    }

    bool MaterialGpuBinder::CreateDefaultTextures(ID3D11Device* device)
    {
        // DWORD は little endian で R,G,B,A の順にメモリへ並ぶ。
        return CreateSolidTexture(device, 0xFFFFFFFFu,
                "ReplayMaterial.DefaultWhite", default_white_) &&
            CreateSolidTexture(device, 0xFF000000u,
                "ReplayMaterial.DefaultBlack", default_black_) &&
            CreateSolidTexture(device, 0xFF808080u,
                "ReplayMaterial.DefaultGray", default_gray_) &&
            CreateSolidTexture(device, 0xFFFF8080u,
                "ReplayMaterial.DefaultBump", default_bump_);
    }

    bool MaterialGpuBinder::EnsureConstantBuffer(ID3D11Device* device,
        std::uint32_t byte_width)
    {
        if (byte_width == 0) return true;
        if (device == nullptr) return false;

        byte_width = Align16(byte_width);
        if (material_constant_buffer_ &&
            material_constant_buffer_size_ == byte_width)
        {
            return true;
        }

        D3D11_BUFFER_DESC description{};
        description.ByteWidth = byte_width;
        description.Usage = D3D11_USAGE_DEFAULT;
        description.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

        Microsoft::WRL::ComPtr<ID3D11Buffer> replacement;
        const HRESULT result = device->CreateBuffer(&description, nullptr,
            replacement.GetAddressOf());
        if (FAILED(result))
        {
            LogOnce(shader_failures_, "cb:" + std::to_string(byte_width),
                "Error", "Material constant buffer を作れません: " +
                std::to_string(byte_width) + " bytes");
            return false;
        }

        SetDebugName(replacement.Get(), "ReplayMaterial.ConstantBuffer.b9");
        material_constant_buffer_ = replacement;
        material_constant_buffer_size_ = byte_width;
        return true;
    }

    ID3D11PixelShader* MaterialGpuBinder::ResolvePixelShader(
        ID3D11Device* device, const ShaderCatalog& catalog,
        const ResolvedMaterialBinding& binding)
    {
        if (!initialized_ || device == nullptr || !binding.usable_shader ||
            !binding.shader.IsValid())
        {
            return nullptr;
        }

        const ShaderCatalog::Entry* entry = catalog.Find(binding.shader);
        if (entry == nullptr || !entry->UsesVariant(binding.variant)) return nullptr;

        const ShaderCatalog::VariantResult& result = entry->At(binding.variant);
        if (!result.bytecode) return nullptr;

        const std::string key = ShaderCacheKey(binding.shader, binding.variant);
        CachedPixelShader& cached = shader_cache_[key];
        const std::size_t bytecode_size = result.bytecode->GetBufferSize();

        if (cached.shader && cached.bytecode_identity == result.bytecode.Get() &&
            cached.bytecode_size == bytecode_size)
        {
            return cached.shader.Get();
        }

        Microsoft::WRL::ComPtr<ID3D11PixelShader> replacement;
        const HRESULT created = device->CreatePixelShader(
            result.bytecode->GetBufferPointer(), bytecode_size, nullptr,
            replacement.GetAddressOf());
        if (FAILED(created))
        {
            LogOnce(shader_failures_, key, "Error",
                "Catalog bytecode から PixelShader を作れません: " + key);
            // Hot Reload の新 bytecode だけが失敗した場合、直前に作れた PS を残す。
            return cached.shader.Get();
        }

        const std::string debug_name = "ReplayMaterial.PS:" + key;
        SetDebugName(replacement.Get(), debug_name.c_str());
        cached.shader = replacement;
        cached.bytecode_identity = result.bytecode.Get();
        cached.bytecode_size = bytecode_size;
        shader_failures_.erase(key);
        return cached.shader.Get();
    }

    ID3D11ShaderResourceView* MaterialGpuBinder::DefaultTexture(
        const std::string& name) const noexcept
    {
        if (name == "black") return default_black_.Get();
        if (name == "gray") return default_gray_.Get();
        if (name == "bump") return default_bump_.Get();
        return default_white_.Get();
    }

    ID3D11ShaderResourceView* MaterialGpuBinder::ResolveTexture(
        ID3D11Device* device, const Assets::AssetDatabase& assets,
        const ResolvedMaterialTexture& texture)
    {
        if (texture.asset_guid.empty())
        {
            if (texture.default_texture != "white" &&
                texture.default_texture != "black" &&
                texture.default_texture != "gray" &&
                texture.default_texture != "bump")
            {
                LogOnce(default_name_failures_, texture.default_texture,
                    "Warning", "不明な既定テクスチャ名を white として扱います: " +
                    texture.default_texture);
            }
            return DefaultTexture(texture.default_texture);
        }

        const auto cached = texture_cache_.find(texture.asset_guid);
        if (cached != texture_cache_.end() && cached->second.image.IsLoaded())
            return cached->second.image.View();

        const Assets::AssetRecord* record = assets.FindByGuid(texture.asset_guid);
        if (record == nullptr)
        {
            LogOnce(texture_failures_, texture.asset_guid, "Error",
                "Material texture の AssetGUID が見つかりません: " +
                texture.asset_guid + " (" + texture.property_name + ")");
            return DefaultTexture(texture.default_texture);
        }
        if (record->kind != Assets::AssetKind::Image)
        {
            LogOnce(texture_failures_, texture.asset_guid, "Error",
                "Material texture が Image Asset ではありません: " +
                texture.asset_guid + " / " + record->source_path.generic_u8string());
            return DefaultTexture(texture.default_texture);
        }

        std::error_code error;
        if (!std::filesystem::is_regular_file(record->source_path, error) || error)
        {
            LogOnce(texture_failures_, texture.asset_guid, "Error",
                "Material texture のファイルがありません: " +
                record->source_path.generic_u8string());
            return DefaultTexture(texture.default_texture);
        }

        CachedTexture loaded;
        loaded.source_path = record->source_path;
        if (!loaded.image.LoadFile(device, record->source_path.wstring()))
        {
            LogOnce(texture_failures_, texture.asset_guid, "Error",
                "Material texture を読み込めません: " +
                record->source_path.generic_u8string());
            return DefaultTexture(texture.default_texture);
        }

        auto inserted = texture_cache_.insert_or_assign(texture.asset_guid,
            std::move(loaded));
        texture_failures_.erase(texture.asset_guid);
        return inserted.first->second.image.View();
    }

    bool MaterialGpuBinder::BindTextures(ID3D11Device* device,
        ID3D11DeviceContext* context, const Assets::AssetDatabase& assets,
        const ResolvedMaterialBinding& binding)
    {
        if (!initialized_ || device == nullptr || context == nullptr) return false;

        // 前 draw の長い Schema が残らないよう、先に自分が触ったスロットを外す。
        UnbindTextures(context);

        bool all_resolved = true;
        for (const ResolvedMaterialTexture& texture : binding.textures)
        {
            if (texture.slot >= D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT)
            {
                LogOnce(texture_failures_, "slot:" + std::to_string(texture.slot),
                    "Error", "Material texture slot が D3D11 上限を超えています: t" +
                    std::to_string(texture.slot));
                all_resolved = false;
                continue;
            }

            ID3D11ShaderResourceView* view = ResolveTexture(device, assets, texture);
            if (view == nullptr)
            {
                view = default_white_.Get();
                all_resolved = false;
            }
            context->PSSetShaderResources(texture.slot, 1, &view);
            bound_texture_slots_.push_back(texture.slot);
        }
        return all_resolved;
    }


    bool MaterialGpuBinder::BindGBufferTextures(ID3D11Device* device,
        ID3D11DeviceContext* context, const Assets::AssetDatabase& assets,
        const ResolvedMaterialBinding& binding)
    {
        if (!initialized_ || device == nullptr || context == nullptr) return false;
        UnbindTextures(context);

        bool all_resolved = true;
        for (const ResolvedMaterialTexture& source : binding.textures)
        {
            std::uint32_t bridge_slot = 0;
            if (!ResolvedMaterialBinding::TryGetGBufferBridgeSlot(
                source.property_name, bridge_slot))
            {
                continue;
            }

            ResolvedMaterialTexture bridge = source;
            bridge.slot = bridge_slot;
            ID3D11ShaderResourceView* view = ResolveTexture(device, assets, bridge);
            if (view == nullptr)
            {
                view = default_white_.Get();
                all_resolved = false;
            }
            context->PSSetShaderResources(bridge_slot, 1, &view);
            bound_texture_slots_.push_back(bridge_slot);
        }
        return all_resolved;
    }

    bool MaterialGpuBinder::Bind(ID3D11Device* device,
        ID3D11DeviceContext* context, const Assets::AssetDatabase& assets,
        const ResolvedMaterialBinding& binding)
    {
        if (!initialized_ || device == nullptr || context == nullptr) return false;

        bool constants_ok = true;
        if (!binding.constants.empty())
        {
            constants_ok = EnsureConstantBuffer(device,
                static_cast<std::uint32_t>(binding.constants.size()));
            if (constants_ok)
            {
                context->UpdateSubresource(material_constant_buffer_.Get(), 0,
                    nullptr, binding.constants.data(), 0, 0);
                ID3D11Buffer* buffer = material_constant_buffer_.Get();
                context->PSSetConstantBuffers(
                    ShaderConstantPacker::material_constant_register, 1, &buffer);
                constant_buffer_bound_ = true;
            }
        }
        else
        {
            ID3D11Buffer* null_buffer = nullptr;
            context->PSSetConstantBuffers(
                ShaderConstantPacker::material_constant_register, 1, &null_buffer);
            constant_buffer_bound_ = false;
        }

        const bool textures_ok = BindTextures(device, context, assets, binding);
        return constants_ok && textures_ok;
    }

    void MaterialGpuBinder::UnbindTextures(ID3D11DeviceContext* context) noexcept
    {
        if (context == nullptr)
        {
            bound_texture_slots_.clear();
            return;
        }

        ID3D11ShaderResourceView* null_view = nullptr;
        for (const std::uint32_t slot : bound_texture_slots_)
        {
            if (slot < D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT)
                context->PSSetShaderResources(slot, 1, &null_view);
        }
        bound_texture_slots_.clear();
    }

    void MaterialGpuBinder::Unbind(ID3D11DeviceContext* context) noexcept
    {
        UnbindTextures(context);
        if (context != nullptr && constant_buffer_bound_)
        {
            ID3D11Buffer* null_buffer = nullptr;
            context->PSSetConstantBuffers(
                ShaderConstantPacker::material_constant_register, 1, &null_buffer);
        }
        constant_buffer_bound_ = false;
    }
}
