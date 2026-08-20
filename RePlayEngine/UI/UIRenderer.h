#pragma once

#include <d3d11.h>
#include <DirectXMath.h>
#include <wrl.h>

#include "Effects/UIRenderTargetPool.h"
#include "../Assets/SpriteAtlasAsset.h"
#include "../Rendering/Effects/EffectChain.h"

#include <array>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ReplayEngine::Assets { class AssetDatabase; }
namespace ReplayEngine::Components { class UIImageComponent; }
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
            // World Space は UI 合成パス上で描くため、通常は深度なしを選ぶ。
            // 呼び出し側が深度付きの描画先を用意した場合だけ利用する。
            ID3D11DepthStencilState* depth_enabled = nullptr;
            DirectX::XMFLOAT4X4 world_view_projection{
                1.0f, 0.0f, 0.0f, 0.0f,
                0.0f, 1.0f, 0.0f, 0.0f,
                0.0f, 0.0f, 1.0f, 0.0f,
                0.0f, 0.0f, 0.0f, 1.0f };
            ID3D11RasterizerState* rasterizer = nullptr;
            ID3D11RasterizerState* rasterizer_scissor = nullptr;
            ID3D11BlendState* blend_none = nullptr;
            ID3D11BlendState* blend_alpha = nullptr;
            ID3D11BlendState* blend_add = nullptr;
            ID3D11BlendState* blend_multiply = nullptr;
            ID3D11BlendState* blend_screen = nullptr;
            ID3D11BlendState* blend_premultiplied = nullptr;
            ID3D11SamplerState* sampler = nullptr;
            float scissor_offset_x = 0.0f;
            float scissor_offset_y = 0.0f;
            float viewport_scale_x = 1.0f;
            float viewport_scale_y = 1.0f;
            bool scissor_bounds_enabled = false;
            D3D11_RECT scissor_bounds{};
            bool focus_outline_enabled = true;
            DirectX::XMFLOAT4 focus_outline_color{ 0.25f, 0.78f, 1.0f, 1.0f };
            float focus_outline_width = 2.0f;
            float focus_corner_radius = 4.0f;
        };

        bool Initialize(ID3D11Device* device);
        void Release() noexcept;
        void ReleaseTransientTargets() noexcept;
        std::uint64_t RenderTargetPoolBytes() const noexcept
        {
            return render_target_pool_.AllocatedBytes();
        }
        std::uint64_t TrackedBufferBytes() const noexcept;
        void AppendResidentTextureIdentities(
            std::vector<std::pair<std::string, const void*>>& out) const;

        void Render(ID3D11DeviceContext* context,
            Scene::Scene& scene,
            const Assets::AssetDatabase* asset_database,
            const Rendering::ShaderCatalog* shader_catalog,
            FontAtlas& font_atlas,
            float screen_width,
            float screen_height,
            float effect_time,
            const RenderStates& states);

    private:
        struct Vertex
        {
            DirectX::XMFLOAT2 position{ 0.0f, 0.0f };
            DirectX::XMFLOAT2 uv{ 0.0f, 0.0f };
            DirectX::XMFLOAT2 gradient_uv{ 0.0f, 0.0f };
            DirectX::XMFLOAT4 color{ 1.0f, 1.0f, 1.0f, 1.0f };
            // Text のグリフ単位でアトラス内のサンプル範囲を制限する。
            DirectX::XMFLOAT4 uv_bounds{ 0.0f, 0.0f, 1.0f, 1.0f };
        };

        struct Constants
        {
            DirectX::XMFLOAT4 screen_size{ 1.0f, 1.0f, 0.0f, 0.0f };
            DirectX::XMFLOAT4X4 world_canvas_matrix{
                1.0f, 0.0f, 0.0f, 0.0f,
                0.0f, 1.0f, 0.0f, 0.0f,
                0.0f, 0.0f, 1.0f, 0.0f,
                0.0f, 0.0f, 0.0f, 1.0f };
            DirectX::XMFLOAT4X4 world_view_projection{
                1.0f, 0.0f, 0.0f, 0.0f,
                0.0f, 1.0f, 0.0f, 0.0f,
                0.0f, 0.0f, 1.0f, 0.0f,
                0.0f, 0.0f, 0.0f, 1.0f };
            // x = World Space 有効、y = Canvas 平面幅、z = Canvas 平面高さ。
            DirectX::XMFLOAT4 world_canvas_params{
                0.0f, 0.0f, 0.0f, 0.0f };
        };


        struct VisualConstants
        {
            DirectX::XMFLOAT4 fill_color_2{ 1.0f, 1.0f, 1.0f, 1.0f };
            // x = fill mode, y = angle in radians, z/w = fill center.
            DirectX::XMFLOAT4 fill_params{ 0.0f, 0.0f, 0.5f, 0.5f };
            DirectX::XMFLOAT4 stroke_color_2{ 1.0f, 1.0f, 1.0f, 1.0f };
            // x = stroke mode, y = outline width, z = text mode.
            DirectX::XMFLOAT4 stroke_params{ 0.0f, 0.0f, 0.0f, 0.0f };
            DirectX::XMFLOAT4 outline_color{ 0.0f, 0.0f, 0.0f, 1.0f };
            DirectX::XMFLOAT4 shadow_offset{ 0.0f, 0.0f, 0.0f, 0.0f };
            DirectX::XMFLOAT4 shadow_color{ 0.0f, 0.0f, 0.0f, 0.0f };
            // x/y = アトラスの実寸、z = SDF の spread。境界のぼかしは
            // 画面上の変化量なので、シェーダーでは fwidth(distance) を使う。
            DirectX::XMFLOAT4 atlas_size{ 2048.0f, 2048.0f,
                8.0f, 0.0f };
            DirectX::XMFLOAT4 fill_color_3{ 1.0f, 1.0f, 1.0f, 1.0f };
            DirectX::XMFLOAT4 fill_color_4{ 1.0f, 1.0f, 1.0f, 1.0f };
            // x/y/z = 色 2/3/4 の位置。y/z が負なら色 3/4 は未設定。
            DirectX::XMFLOAT4 fill_stops{ 1.0f, -1.0f, -1.0f, 0.0f };
        };

        struct ResolvedImageSource
        {
            std::string texture_guid;
            DirectX::XMFLOAT4 uv{ 0.0f, 0.0f, 1.0f, 1.0f };
            DirectX::XMFLOAT2 atlas_pivot{ 0.5f, 0.5f };
            bool rotated = false;
            bool from_atlas = false;
        };

        struct CachedSpriteAtlas
        {
            Assets::SpriteAtlasAsset asset;
            std::filesystem::path path;
            std::filesystem::file_time_type timestamp{};
            bool loaded = false;
        };

        struct TemporalHistoryEntry
        {
            UIRenderTarget target;
            std::uint64_t last_used_serial = 0;
            bool valid = false;
        };

        bool EnsureVertexCapacity(ID3D11Device* device, std::size_t vertex_count);
        TemporalHistoryEntry* TemporalHistoryFor(std::uint64_t owner_key,
            std::uint32_t width, std::uint32_t height);
        void PruneTemporalHistory() noexcept;
        bool ResolveImageSource(const Components::UIImageComponent& image,
            const Assets::AssetDatabase* asset_database, ResolvedImageSource& out);
        ID3D11ShaderResourceView* TextureFor(const std::string& guid,
            const Assets::AssetDatabase* asset_database);
        void Flush(ID3D11DeviceContext* context, ID3D11ShaderResourceView* texture,
            ID3D11BlendState* blend_state, const RenderStates& states,
            const D3D11_RECT* scissor,
            ID3D11PixelShader* pixel_shader_override = nullptr,
            ID3D11Buffer* pixel_constant_buffer = nullptr);

        Microsoft::WRL::ComPtr<ID3D11Device> device_;
        Microsoft::WRL::ComPtr<ID3D11VertexShader> vertex_shader_;
        Microsoft::WRL::ComPtr<ID3D11PixelShader> pixel_shader_;
        Microsoft::WRL::ComPtr<ID3D11InputLayout> input_layout_;
        Microsoft::WRL::ComPtr<ID3D11Buffer> vertex_buffer_;
        Microsoft::WRL::ComPtr<ID3D11Buffer> constant_buffer_;
        Microsoft::WRL::ComPtr<ID3D11Buffer> visual_constant_buffer_;
        // GPU へ送る前の CPU 側の値。バッチごとに組み立ててから 1 回だけ転送する。
        VisualConstants visual_constants_{};
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> white_texture_;
        std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>> texture_cache_;
        std::unordered_map<std::string, CachedSpriteAtlas> sprite_atlas_cache_;
        std::unordered_map<std::uint64_t, TemporalHistoryEntry> temporal_history_cache_;
        std::uint64_t render_serial_ = 0;
        std::vector<Vertex> vertices_;
        std::size_t vertex_capacity_ = 0;
        bool world_space_canvas_ = false;
        UIRenderTargetPool render_target_pool_;
        Rendering::Effects::EffectChain effect_chain_;
    };
}
