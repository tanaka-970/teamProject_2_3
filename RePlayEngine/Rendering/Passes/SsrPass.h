#pragma once

#include "../RenderTexture.h"
#include "../PassStates.h"

#include <d3d11.h>
#include <wrl.h>
#include <DirectXMath.h>

#include <cstdint>

class fullscreen_quad;

namespace ReplayEngine::Rendering
{
    // スクリーンスペース反射。
    // 反射色は「前フレームのライティング結果」から取る。照明パスより前に
    // 走らせてPBRの鏡面項へ差し込めるようにするため、履歴を1枚保持する。
    class SsrPass final
    {
    public:
        // Shader\PostEffects\Reflection\ssr_common.hlsli の
        // SSR_CONSTANT_BUFFER(b13) と並びを一致させる。
        struct Constants
        {
            DirectX::XMFLOAT4 params0{ 40.0f, 0.4f, 3.0f, 48.0f };
            DirectX::XMFLOAT4 params1{ 0.65f, 1.0f, 0.12f, 5.0f };
            DirectX::XMFLOAT4 params2{ 1.0f, 12.0f, 1.0f, 0.0f };
            DirectX::XMFLOAT4 params3{ 8.0f, 4.0f, 0.0f, 0.0f };
            // SSRパス自身の解像度 (x=w, y=h, z=1/w, w=1/h)。半解像度対応に使う。
            DirectX::XMFLOAT4 target_size{ 1.0f, 1.0f, 1.0f, 1.0f };
        };

        static constexpr uint32_t kConstantSlot = 13;

        // resolution_divisor=2 で半解像度。反射はラフネスでぼかす前提なので
        // 半分でも見た目の劣化が小さく、レイマーチの負荷が1/4になる。
        // 履歴(反射ソース)は lit テクスチャからのコピーなのでフル解像度のまま。
        bool Initialize(ID3D11Device* device, uint32_t width, uint32_t height,
            uint32_t resolution_divisor = 2);
        // 反射色(rgb)と信頼度(a)を返す。履歴が無い初回フレームはnullptr。
        ID3D11ShaderResourceView* Execute(ID3D11DeviceContext* context,
            fullscreen_quad& quad,
            ID3D11ShaderResourceView* depth,
            ID3D11ShaderResourceView* world_normal,
            ID3D11ShaderResourceView* material);
        // 照明後のHDRカラーを次フレームの反射ソースとして取り込む。
        void CaptureHistory(ID3D11DeviceContext* context, ID3D11Resource* lit_color);
        void InvalidateHistory() noexcept { history_valid_ = false; }

        ID3D11ShaderResourceView* Output() const noexcept;
        bool Initialized() const noexcept { return initialized_; }

        // エディタから触る調整値。
        float max_distance = 40.0f;    // レイの最大長 (ビュー空間)
        float thickness = 0.4f;        // 交差判定の厚み
        float stride = 3.0f;           // 1ステップの画面上の距離 (ピクセル)
        int   max_step = 48;           // 最大マーチ回数
        int   refine_step = 5;         // 二分探索の回数
        float max_roughness = 0.65f;   // これより粗い面はSSRを使わない
        float intensity = 1.0f;
        float edge_fade = 0.12f;       // 画面端フェード幅 (UV)
        float ray_bias = 1.0f;         // 自己交差を避ける押し出し量
        float resolve_radius = 12.0f;  // resolveのぼかし半径 (ピクセル)
        int   resolve_tap_count = 8;
        bool  enabled = true;

    private:
        void UploadConstants(ID3D11DeviceContext* context);

        RenderTexture trace_;     // レイマーチ結果 (rgb=色, a=信頼度)
        RenderTexture resolved_;  // ノイズ除去後
        RenderTexture history_;   // 前フレームのライティング結果
        Microsoft::WRL::ComPtr<ID3D11PixelShader> trace_shader_;
        Microsoft::WRL::ComPtr<ID3D11PixelShader> resolve_shader_;
        Microsoft::WRL::ComPtr<ID3D11Buffer> constants_;
        // 直前のパスが残したブレンド設定に影響されないようにする。
        PassStates states_;
        bool initialized_ = false;
        bool history_valid_ = false;
    };
}
