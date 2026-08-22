#pragma once

#include <d3d11.h>
#include <wrl.h>
#include <DirectXMath.h>

#include <cstdint>
#include <vector>

namespace ReplayEngine::Rendering
{
    // Point / Spot の動的シャドウマップ。Texture2DArray へ Spot を 1 枚、Point を 6 枚ずつ並べる。
    class LocalShadowAtlas final
    {
    public:
        // Shader\local_shadow_common.hlsli の LocalShadowSlice と並びを一致させる。
        struct Slice
        {
            DirectX::XMFLOAT4X4 view_projection{
                1.0f, 0.0f, 0.0f, 0.0f,
                0.0f, 1.0f, 0.0f, 0.0f,
                0.0f, 0.0f, 1.0f, 0.0f,
                0.0f, 0.0f, 0.0f, 1.0f };
            // x=near, y=far, z=depth_bias, w=未使用
            DirectX::XMFLOAT4 params{ 0.1f, 50.0f, 0.0015f, 0.0f };
        };

        // Shader\local_shadow_caster_gs.hlsl の LOCAL_SHADOW_PASS と一致させる。
        struct PassConstants
        {
            // x=先頭スライス番号, y=このパスで描くスライス数, z/w=予約
            DirectX::XMINT4 range{ 0, 1, 0, 0 };
        };

        // 影付き Spot / Point の上限。増やすほど Scene 全体を描き直す回数が増える。
        static constexpr std::uint32_t kMaxSpotShadows = 4;
        static constexpr std::uint32_t kMaxPointShadows = 2;
        static constexpr std::uint32_t kPointFaceCount = 6;
        static constexpr std::uint32_t kSliceCount =
            kMaxSpotShadows + kMaxPointShadows * kPointFaceCount;

        static constexpr std::uint32_t kDefaultResolution = 1024;
        // Shader\local_shadow_common.hlsli のレジスタと一致させる。
        static constexpr std::uint32_t kAtlasSlot = 13;
        static constexpr std::uint32_t kSliceBufferSlot = 21;
        static constexpr std::uint32_t kSamplerSlot = 7;
        static constexpr std::uint32_t kPassConstantSlot = 11;

        bool Initialize(ID3D11Device* device);

        // 影付きライトが現れたフレームで初めて影マップを確保する。
        bool EnsureAtlas(ID3D11Device* device, std::uint32_t resolution);
        bool AtlasReady() const noexcept { return atlas_srv_ != nullptr; }
        std::uint32_t Resolution() const noexcept { return resolution_; }

        // ---- 1 フレーム分のスライス割り当て ----------------------------------
        void BeginFrame() noexcept;
        // Spot 1 灯ぶんのスライスを確保する。取れなければ -1。
        int AllocateSpotSlice() noexcept;
        // Point 1 灯ぶんの 6 面を確保する。先頭スライス番号。取れなければ -1。
        int AllocatePointSlices() noexcept;
        void SetSlice(int slice, const DirectX::XMFLOAT4X4& view_projection,
            float near_plane, float far_plane, float depth_bias) noexcept;
        std::uint32_t UsedSliceCount() const noexcept
        {
            return next_spot_slice_ + (next_point_base_ - kMaxSpotShadows);
        }

        // Spot の視錐台行列を作る。outer_angle は度。
        static DirectX::XMFLOAT4X4 MakeSpotViewProjection(
            const DirectX::XMFLOAT3& position, const DirectX::XMFLOAT3& direction,
            float outer_angle_degrees, float near_plane, float far_plane) noexcept;
        // Point の 6 面のうち face 番目の行列を作る。face は 0..5。
        static DirectX::XMFLOAT4X4 MakePointFaceViewProjection(
            const DirectX::XMFLOAT3& position, int face,
            float near_plane, float far_plane) noexcept;

        // ---- 描画 ---- 1 ライトぶんのスライスへ描き始める。GS と Viewport を設定する。
        void BeginLight(ID3D11DeviceContext* context, int base_slice, int slice_count);
        // 影パスを終え、元の RenderTarget と Viewport へ戻す。
        void End(ID3D11DeviceContext* context,
            ID3D11RenderTargetView* restore_rtv,
            ID3D11DepthStencilView* restore_dsv,
            const D3D11_VIEWPORT& restore_viewport);
        // 確保したスライス行列を GPU へ送る。BeginLight の前に 1 回呼ぶ。
        void UploadSlices(ID3D11DeviceContext* context);

        // 照明シェーダーからサンプルできるように貼る / 外す。
        void BindResources(ID3D11DeviceContext* context);
        void UnbindResources(ID3D11DeviceContext* context);
        void BindComputeResources(ID3D11DeviceContext* context);
        void UnbindComputeResources(ID3D11DeviceContext* context);

        ID3D11GeometryShader* CasterGeometryShader() const noexcept
        {
            return caster_gs_.Get();
        }

        // エディタから触る値。
        bool enabled = true;
        std::uint32_t resolution_setting = kDefaultResolution;

    private:
        Microsoft::WRL::ComPtr<ID3D11Device> device_;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> atlas_;
        Microsoft::WRL::ComPtr<ID3D11DepthStencilView> atlas_dsv_;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> atlas_srv_;
        Microsoft::WRL::ComPtr<ID3D11SamplerState> comparison_sampler_;
        Microsoft::WRL::ComPtr<ID3D11Buffer> slice_buffer_;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> slice_srv_;
        Microsoft::WRL::ComPtr<ID3D11Buffer> pass_constants_;
        Microsoft::WRL::ComPtr<ID3D11GeometryShader> caster_gs_;

        Slice slices_[kSliceCount]{};
        // Spot と Point で確保カウンタを分ける。1 本の連番だと Point が Spot の枠を食う。
        std::uint32_t next_spot_slice_ = 0;
        std::uint32_t next_point_base_ = kMaxSpotShadows;
        std::uint32_t resolution_ = 0;
        D3D11_VIEWPORT viewport_{};
        bool cleared_this_frame_ = false;
    };
}
