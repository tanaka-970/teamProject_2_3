#pragma once

#include "MaterialBinding.h"
#include "../../Assets/AssetDatabase.h"
#include "../../Assets/ImageAsset.h"

#include <d3d11.h>
#include <wrl.h>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace ReplayEngine::Rendering
{
    // ResolvedMaterialBinding を D3D11 へ載せる、Renderer 側だけのクラス。
    // MaterialAsset / Component は GPU リソースを一切持たない。
    class MaterialGpuBinder final
    {
    public:
        using LogSink = std::function<void(const std::string& severity,
            const std::string& message)>;

        bool Initialize(ID3D11Device* device, LogSink log = {});
        void Clear() noexcept;

        bool Initialized() const noexcept { return initialized_; }

        // Catalog が保持する最後に成功した bytecode から PS を作る。
        // bytecode の実体が変わったときだけ作り直すため、Hot Reload と両立する。
        ID3D11PixelShader* ResolvePixelShader(ID3D11Device* device,
            const ShaderCatalog& catalog,
            const ResolvedMaterialBinding& binding);

        // Shader-owned additional pass。pass_index は Catalog の宣言順。
        ID3D11PixelShader* ResolvePassPixelShader(ID3D11Device* device,
            const ShaderCatalog& catalog,
            const ResolvedMaterialBinding& binding, std::size_t pass_index);

        // Forward 用。b9 と t40 以降をまとめて設定する。
        bool Bind(ID3D11Device* device, ID3D11DeviceContext* context,
            const Assets::AssetDatabase& assets,
            const ResolvedMaterialBinding& binding);

        // Deferred GBuffer の固定 cbuffer と併用するため、Texture だけを設定する。
        bool BindTextures(ID3D11Device* device, ID3D11DeviceContext* context,
            const Assets::AssetDatabase& assets,
            const ResolvedMaterialBinding& binding);

        // Deferred GBuffer用。Custom Shaderのproperty宣言順に左右されないよう、
        // semantic名を固定bridge slot(t40..t45)へ再配置して設定する。
        bool BindGBufferTextures(ID3D11Device* device,
            ID3D11DeviceContext* context, const Assets::AssetDatabase& assets,
            const ResolvedMaterialBinding& binding);

        // Bind / BindTextures で触った b9 / t40+ を外す。
        void Unbind(ID3D11DeviceContext* context) noexcept;
        void UnbindTextures(ID3D11DeviceContext* context) noexcept;

        std::size_t CachedShaderCount() const noexcept { return shader_cache_.size(); }
        std::size_t CachedTextureCount() const noexcept { return texture_cache_.size(); }
        std::uint64_t TrackedTextureBytes() const noexcept;
        std::uint64_t TrackedBufferBytes() const noexcept;
        void AppendResidentTextureIdentities(
            std::vector<std::pair<std::string, const void*>>& out) const;
        bool DefaultTexturesReady() const noexcept
        {
            return default_white_ && default_black_ && default_gray_ && default_bump_;
        }

    private:
        struct CachedPixelShader final
        {
            const ID3DBlob* bytecode_identity = nullptr; // Catalog が所有。比較だけに使う
            std::size_t bytecode_size = 0;
            Microsoft::WRL::ComPtr<ID3D11PixelShader> shader;
        };

        struct CachedTexture final
        {
            Assets::ImageAsset image;
            std::filesystem::path source_path;
        };

        static std::string ShaderCacheKey(ShaderID id, ShaderVariant variant);
        bool EnsureConstantBuffer(ID3D11Device* device, std::uint32_t byte_width);
        bool CreateDefaultTextures(ID3D11Device* device);
        bool CreateSolidTexture(ID3D11Device* device, std::uint32_t rgba,
            const char* debug_name,
            Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>& out);

        ID3D11ShaderResourceView* ResolveTexture(ID3D11Device* device,
            const Assets::AssetDatabase& assets,
            const ResolvedMaterialTexture& texture);
        ID3D11ShaderResourceView* DefaultTexture(const std::string& name) const noexcept;

        void LogOnce(std::unordered_set<std::string>& seen,
            const std::string& key, const std::string& severity,
            const std::string& message);

        bool initialized_ = false;
        LogSink log_;

        Microsoft::WRL::ComPtr<ID3D11Buffer> material_constant_buffer_;
        std::uint32_t material_constant_buffer_size_ = 0;
        bool constant_buffer_bound_ = false;

        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> default_white_;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> default_black_;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> default_gray_;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> default_bump_;

        std::unordered_map<std::string, CachedPixelShader> shader_cache_;
        std::unordered_map<std::string, CachedTexture> texture_cache_;
        std::vector<std::uint32_t> bound_texture_slots_;

        std::unordered_set<std::string> shader_failures_;
        std::unordered_set<std::string> texture_failures_;
        std::unordered_set<std::string> default_name_failures_;
    };
}
