#pragma once

#include <d3d11.h>
#include <DirectXMath.h>
#include <wrl.h>

#include "Effects/UIRenderTargetPool.h"

#include <array>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace ReplayEngine::Assets { class AssetDatabase; }
namespace ReplayEngine::Rendering { class ShaderCatalog; }
namespace ReplayEngine::Scene { class Scene; }

namespace ReplayEngine::UI
{
    enum class UIEffectKind : int;

    class FontAtlas;

    // ---- 拡張点: Effect Stack (Phase 6) -----------------------------------
    //
    // 【今は入れていない理由】
    //   Phase 1 の目的は「Motion から動かされる側」を用意すること。
    //   Effect はオブジェクト単位のオフスクリーン描画になり、
    //   RT プールと描画順の設計が別途要るため、ここでは扱わない。
    //
    // 【入れるときにここへ足す】
    //   ・Render() の中で、Effect を持つ要素だけ一度 RT へ逃がす分岐を作る
    //   ・Flush() の呼び出し単位を「RT ごと」に割る（今はテクスチャ / ブレンド /
    //     シザーの変わり目で割っている。そこへ RT の変わり目を足す）
    //   ・RenderStates に Effect 用のブレンドとサンプラを追加する
    //
    // 【壊してはいけない前提】
    //   ・Effect は RectTransform の矩形の外へはみ出す（Glow / 影 / Blur）。
    //     Effect 基底の ExpandBounds() を累積して RT のサイズを決めること。
    //     矩形と同じ大きさで確保すると発光と影が縁で切れる。
    //   ・RT はプールから借りる。Effect 付きの要素ごとに確保すると破綻する。
    //
    //   詳細は Docs/UI_MOTION_PHASE0_DESIGN.md の 3.7 を参照。
    class UIRenderer final
    {
    public:
        struct RenderStates
        {
            ID3D11DepthStencilState* depth_disabled = nullptr;
            ID3D11RasterizerState* rasterizer = nullptr;
            ID3D11RasterizerState* rasterizer_scissor = nullptr;
            ID3D11BlendState* blend_none = nullptr;
            ID3D11BlendState* blend_alpha = nullptr;
            ID3D11BlendState* blend_add = nullptr;
            ID3D11BlendState* blend_multiply = nullptr;
            ID3D11BlendState* blend_screen = nullptr;
            ID3D11BlendState* blend_premultiplied = nullptr;
            ID3D11SamplerState* sampler = nullptr;
        };

        bool Initialize(ID3D11Device* device);
        void Release() noexcept;
        void ReleaseTransientTargets() noexcept;

        void Render(ID3D11DeviceContext* context,
            Scene::Scene& scene,
            const Assets::AssetDatabase* asset_database,
            const Rendering::ShaderCatalog* shader_catalog,
            FontAtlas& font_atlas,
            float screen_width,
            float screen_height,
            const RenderStates& states);

    private:
        struct Vertex
        {
            DirectX::XMFLOAT2 position{ 0.0f, 0.0f };
            DirectX::XMFLOAT2 uv{ 0.0f, 0.0f };
            DirectX::XMFLOAT4 color{ 1.0f, 1.0f, 1.0f, 1.0f };
        };

        struct Constants
        {
            DirectX::XMFLOAT4 screen_size{ 1.0f, 1.0f, 0.0f, 0.0f };
        };

        struct CachedCustomEffectShader
        {
            Microsoft::WRL::ComPtr<ID3D11PixelShader> shader;
            const void* bytecode_identity = nullptr;
            std::size_t bytecode_size = 0;
        };

        struct EffectConstants
        {
            DirectX::XMFLOAT4 effect_color{ 1.0f, 1.0f, 1.0f, 1.0f };
            DirectX::XMFLOAT4 effect_params0{ 0.0f, 1.0f, 0.5f, 1.0f };
            DirectX::XMFLOAT4 effect_params1{ 0.0f, 0.0f, 0.0f, 0.0f };
            DirectX::XMFLOAT4 effect_params2{ 1.0f, -1.0f, 0.0f, 0.0f };
            DirectX::XMFLOAT4 target_size{ 1.0f, 1.0f, 1.0f, 1.0f };
        };

        static constexpr std::size_t effect_shader_count = 10;

        bool EnsureVertexCapacity(ID3D11Device* device, std::size_t vertex_count);
        bool EnsureCustomEffectConstantBuffer(std::uint32_t byte_width);
        ID3D11ShaderResourceView* TextureFor(const std::string& guid,
            const Assets::AssetDatabase* asset_database);
        ID3D11PixelShader* EffectShaderFor(UIEffectKind kind) const noexcept;
        ID3D11PixelShader* CustomEffectShaderFor(const std::string& shader_guid,
            const Assets::AssetDatabase* asset_database,
            const Rendering::ShaderCatalog* shader_catalog);
        void Flush(ID3D11DeviceContext* context, ID3D11ShaderResourceView* texture,
            ID3D11BlendState* blend_state, const RenderStates& states,
            const D3D11_RECT* scissor,
            ID3D11PixelShader* pixel_shader_override = nullptr,
            ID3D11Buffer* pixel_constant_buffer = nullptr);

        Microsoft::WRL::ComPtr<ID3D11Device> device_;
        Microsoft::WRL::ComPtr<ID3D11VertexShader> vertex_shader_;
        Microsoft::WRL::ComPtr<ID3D11PixelShader> pixel_shader_;
        std::array<Microsoft::WRL::ComPtr<ID3D11PixelShader>,
            effect_shader_count> effect_pixel_shaders_;
        Microsoft::WRL::ComPtr<ID3D11InputLayout> input_layout_;
        Microsoft::WRL::ComPtr<ID3D11Buffer> vertex_buffer_;
        Microsoft::WRL::ComPtr<ID3D11Buffer> constant_buffer_;
        Microsoft::WRL::ComPtr<ID3D11Buffer> effect_constant_buffer_;
        Microsoft::WRL::ComPtr<ID3D11Buffer> custom_effect_constant_buffer_;
        std::uint32_t custom_effect_constant_buffer_size_ = 0;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> white_texture_;
        std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>> texture_cache_;
        std::unordered_map<std::string, CachedCustomEffectShader> custom_effect_shader_cache_;
        std::vector<Vertex> vertices_;
        std::size_t vertex_capacity_ = 0;
        UIRenderTargetPool render_target_pool_;
    };
}
