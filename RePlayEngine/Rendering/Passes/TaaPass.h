#pragma once

#include "../RenderTexture.h"

#include <d3d11.h>
#include <wrl.h>
#include <DirectXMath.h>

#include <cstdint>

class fullscreen_quad;

namespace ReplayEngine::Rendering
{
    // テンポラルアンチエイリアシング。
    //
    // 射影行列へフレームごとのサブピクセルジッターを与え、その結果を
    // モーションベクターで再投影した履歴と混ぜることで、実質的な
    // スーパーサンプリングを得る。ジッター量の生成もこのクラスが持つ。
    class TaaPass final
    {
    public:
        // Shader\PostEffects\TemporalAA\taa_resolve_ps.hlsl の
        // TAA_CONSTANT_BUFFER(b12) と並びを一致させる。
        struct Constants
        {
            DirectX::XMFLOAT4 params0{ 0.9f, 1.25f, 0.15f, 1.0f };
            DirectX::XMFLOAT4 params1{ 0.0f, 1.0f, 48.0f, 0.0f };
        };

        static constexpr uint32_t kConstantSlot = 12;
        // ジッター列の長さ。Halton(2,3)を8フレーム周期で回す。
        static constexpr uint32_t kJitterSampleCount = 8;

        bool Initialize(ID3D11Device* device, uint32_t width, uint32_t height);

        // 今フレームのジッターをNDC単位で返す。射影行列の _31/_32 へ加算する。
        DirectX::XMFLOAT2 CurrentJitter(uint32_t frame_index) const noexcept;
        DirectX::XMFLOAT2 PreviousJitter() const noexcept { return previous_jitter_; }
        void SetJitter(const DirectX::XMFLOAT2& jitter) noexcept
        {
            previous_jitter_ = current_jitter_;
            current_jitter_ = jitter;
        }

        // scene_color を履歴と合成した結果のSRVを返す。履歴が無ければ入力をそのまま返す。
        ID3D11ShaderResourceView* Execute(ID3D11DeviceContext* context,
            fullscreen_quad& quad,
            ID3D11ShaderResourceView* scene_color,
            ID3D11ShaderResourceView* depth,
            ID3D11ShaderResourceView* velocity);

        void InvalidateHistory() noexcept { history_valid_ = false; }
        bool Initialized() const noexcept { return initialized_; }
        bool HistoryValid() const noexcept { return history_valid_; }

        // エディタから触る調整値。
        float blend = 0.9f;           // 履歴の比率。高いほど安定するが残像が増える
        float variance_gamma = 1.25f; // クリップ範囲の広さ。小さいほどゴーストに強い
        float sharpness = 0.15f;      // TAA後のシャープ化量
        float max_velocity = 48.0f;   // これ以上速い動きは履歴を大きく捨てる
        bool  enabled = true;

    private:
        void UploadConstants(ID3D11DeviceContext* context);

        RenderTexture resolved_;  // 今フレームの合成結果
        RenderTexture history_;   // 前フレームの合成結果
        Microsoft::WRL::ComPtr<ID3D11PixelShader> resolve_shader_;
        Microsoft::WRL::ComPtr<ID3D11Buffer> constants_;
        DirectX::XMFLOAT2 current_jitter_{ 0.0f, 0.0f };
        DirectX::XMFLOAT2 previous_jitter_{ 0.0f, 0.0f };
        bool initialized_ = false;
        bool history_valid_ = false;
    };
}
