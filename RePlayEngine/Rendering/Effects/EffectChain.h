#pragma once

#include <d3d11.h>
#include <DirectXMath.h>
#include <wrl.h>

#include "../../UI/Effects/UIEffect.h"
#include "../../UI/Effects/UIRenderTargetPool.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace ReplayEngine::Assets { class AssetDatabase; }
namespace ReplayEngine::Rendering { class ShaderCatalog; }

namespace ReplayEngine::Rendering::Effects
{
    // UI / Model / Screen から共通利用する Effect 適用コア。
    // Texture の所有・キャッシュは呼び出し側へ残し、resolve_texture 経由で参照する。
    class EffectChain final
    {
    public:
        struct Context
        {
            ID3D11DeviceContext* device_context = nullptr;
            const Assets::AssetDatabase* asset_database = nullptr;
            const Rendering::ShaderCatalog* shader_catalog = nullptr;
            float time = 0.0f;

            ID3D11DepthStencilState* depth_disabled = nullptr;
            ID3D11RasterizerState* rasterizer = nullptr;
            ID3D11BlendState* blend_none = nullptr;
            ID3D11BlendState* blend_alpha = nullptr;
            ID3D11SamplerState* sampler = nullptr;

            // Texture の所有とキャッシュは呼び出し側。
            // EffectChain は独自の Texture cache を持たない。
            std::function<ID3D11ShaderResourceView*(const std::string&)> resolve_texture;

            // UI Track Matte のように「Assetではなく実行時RT」を t1 へ渡すための入口。
            // EffectChain は所有しない。通常Effectでは nullptr のまま。
            ID3D11ShaderResourceView* runtime_mask_texture = nullptr;
            bool runtime_mask_luma = false;
            bool runtime_mask_invert = false;

            // MotionBlur / Echo は前回の合成結果を t1 として受け取れる。
            // History の所有は Renderer 側。EffectChain は参照するだけ。
            ID3D11ShaderResourceView* runtime_history_texture = nullptr;

            // Stack 全体/個別 Effect の範囲制限。参照のみで所有しない。
            const UI::UIEffectRegion* effect_region = nullptr;

            // 描画先切替と fullscreen quad は既存 renderer の経路をそのまま使う。
            // これにより抽出前後で頂点生成・通常 UI shader・state 設定を変えない。
            std::function<void(UI::UIRenderTarget&)> configure_target;
            std::function<void(float, float, ID3D11ShaderResourceView*,
                ID3D11BlendState*)> draw_plain_fullscreen;
            std::function<void(float, float, ID3D11ShaderResourceView*,
                ID3D11BlendState*, ID3D11PixelShader*, ID3D11Buffer*)>
                draw_effect_fullscreen;
            // source_effected=t0, source_original=t2, region_mask=t1。
            // 範囲ブレンドは通常の Effect 出力と元画像を同時に読むため、
            // 呼び出し側が3枚目の ping-pong RT へ描画する。
            std::function<void(float, float, ID3D11ShaderResourceView*,
                ID3D11ShaderResourceView*, ID3D11ShaderResourceView*,
                ID3D11BlendState*, ID3D11PixelShader*, ID3D11Buffer*)>
                draw_region_fullscreen;
        };

        bool Initialize(ID3D11Device* device);
        void Release() noexcept;

        // current / first / second は呼び出し側が同一 pool から確保する ping-pong RT。
        // 戻り値は最後に Effect が描かれた RT。適用不能時は current をそのまま返す。
        UI::UIRenderTarget* Apply(const Context& context,
            const std::vector<UI::UIEffect>& effects,
            UI::UIRenderTarget* current,
            UI::UIRenderTarget* first,
            UI::UIRenderTarget* second,
            UI::UIRenderTarget* third);

        static DirectX::XMFLOAT4 ExpandBounds(const std::vector<UI::UIEffect>& effects,
            float target_width, float target_height) noexcept;
        std::uint64_t AllocatedBufferBytes() const noexcept;

    private:
        struct CachedCustomEffectShader
        {
            Microsoft::WRL::ComPtr<ID3D11PixelShader> shader;
            const void* bytecode_identity = nullptr;
            std::size_t bytecode_size = 0;
        };

        struct EffectConstants
        {
            static constexpr std::size_t MaxAdditionalEffectRegions = 7;
            static constexpr std::size_t MaxEffectRegions =
                MaxAdditionalEffectRegions + 1;
            static constexpr std::size_t MaxEffectRegionVertices = 32;
            DirectX::XMFLOAT4 effect_color{ 1.0f, 1.0f, 1.0f, 1.0f };
            DirectX::XMFLOAT4 effect_params0{ 0.0f, 1.0f, 0.5f, 1.0f };
            DirectX::XMFLOAT4 effect_params1{ 0.0f, 0.0f, 0.0f, 0.0f };
            DirectX::XMFLOAT4 effect_params2{ 1.0f, -1.0f, 0.0f, 0.0f };
            DirectX::XMFLOAT4 target_size{ 1.0f, 1.0f, 1.0f, 1.0f };
            // 既存 12 種が読む先頭 5 レジスタは動かさず、拡張値は末尾へ置く。
            DirectX::XMFLOAT4 effect_color_2{ 1.0f, 1.0f, 1.0f, 1.0f };
            DirectX::XMFLOAT4 effect_color_3{ 1.0f, 1.0f, 1.0f, 1.0f };
            DirectX::XMFLOAT4 effect_color_4{ 1.0f, 1.0f, 1.0f, 1.0f };
            DirectX::XMFLOAT4 effect_color_stops{ 0.333333f, 0.666667f, 1.0f, 0.0f };
            DirectX::XMFLOAT4 effect_params3{ 0.0f, 0.0f, 0.0f, 0.0f };
            DirectX::XMFLOAT4 brush_pattern_settings{ 0.0f, 0.0f, 0.0f, 0.0f };
            std::array<DirectX::XMFLOAT4, 4> brush_pattern_weights{};
            DirectX::XMFLOAT4 effect_region_params{ 0.5f, 0.5f, 0.5f, 0.5f };
            DirectX::XMFLOAT4 effect_region_settings{ 0.0f, 0.0f, 1.0f, 0.0f };
            DirectX::XMFLOAT4 effect_region_extra_params[MaxAdditionalEffectRegions]{};
            DirectX::XMFLOAT4 effect_region_extra_settings[MaxAdditionalEffectRegions]{};
            DirectX::XMFLOAT4 effect_region_count{ 0.0f, 0.0f, 0.0f, 0.0f };
            DirectX::XMFLOAT4 effect_region_path_counts[MaxEffectRegions]{};
            DirectX::XMFLOAT4 effect_region_path_points[MaxEffectRegions][
                MaxEffectRegionVertices]{};
        };

        struct BrushStrokeInstance
        {
            DirectX::XMFLOAT2 center{ 0.0f, 0.0f };
            DirectX::XMFLOAT2 size{ 1.0f, 1.0f };
            std::uint32_t pattern = 0;
            float padding = 0.0f;
        };

        static constexpr std::size_t effect_shader_count = 74;

        bool EnsureBrushStrokeInstanceCapacity(std::size_t instance_count);
        bool EnsureCustomEffectConstantBuffer(std::uint32_t byte_width);
        ID3D11PixelShader* EffectShaderFor(UI::UIEffectKind kind) const noexcept;
        ID3D11PixelShader* CustomEffectShaderFor(const std::string& shader_guid,
            const Assets::AssetDatabase* asset_database,
            const Rendering::ShaderCatalog* shader_catalog);

        Microsoft::WRL::ComPtr<ID3D11Device> device_;
        Microsoft::WRL::ComPtr<ID3D11VertexShader> brush_stroke_vertex_shader_;
        Microsoft::WRL::ComPtr<ID3D11PixelShader> brush_stroke_pixel_shader_;
        Microsoft::WRL::ComPtr<ID3D11PixelShader> effect_region_pixel_shader_;
        std::array<Microsoft::WRL::ComPtr<ID3D11PixelShader>,
            effect_shader_count> effect_pixel_shaders_;
        Microsoft::WRL::ComPtr<ID3D11Buffer> effect_constant_buffer_;
        Microsoft::WRL::ComPtr<ID3D11Buffer> brush_stroke_instance_buffer_;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> brush_stroke_instance_srv_;
        std::size_t brush_stroke_instance_capacity_ = 0;
        Microsoft::WRL::ComPtr<ID3D11Buffer> custom_effect_constant_buffer_;
        std::uint32_t custom_effect_constant_buffer_size_ = 0;
        std::unordered_map<std::string, CachedCustomEffectShader> custom_effect_shader_cache_;
    };
}
