#pragma once

#include "../RenderTexture.h"

#include <d3d11.h>
#include <wrl.h>
#include <DirectXMath.h>

#include <cstdint>

class fullscreen_quad;

namespace ReplayEngine::Rendering
{
    // GTAO相当のSSAOと、深度を見るバイラテラルブラーをまとめたパス。
    // 深度とG-Buffer法線だけで動くので、Deferred照明の前に一度走らせれば
    // 照明側は結果テクスチャを掛けるだけでよい。
    class SsaoPass final
    {
    public:
        // Shader\PostEffects\AmbientOcclusion\ssao_common.hlsli の
        // SSAO_CONSTANT_BUFFER(b12) と並びを一致させる。
        struct Constants
        {
            DirectX::XMFLOAT4 params0{ 0.75f, 1.0f, 1.6f, 1.0f };
            DirectX::XMFLOAT4 params1{ 4.0f, 8.0f, 96.0f, 2.0f };
            DirectX::XMFLOAT4 params2{ 60.0f, 140.0f, 1.0f, 0.0f };
            DirectX::XMFLOAT4 params3{ 1.0f, 0.35f, 1.0f, 0.0f };
        };

        static constexpr uint32_t kConstantSlot = 12;

        bool Initialize(ID3D11Device* device, uint32_t width, uint32_t height);
        // depth と world normal から遮蔽率を作り、ブラー後のSRVを返す。
        ID3D11ShaderResourceView* Execute(ID3D11DeviceContext* context,
            fullscreen_quad& quad,
            ID3D11ShaderResourceView* depth,
            ID3D11ShaderResourceView* world_normal);
        ID3D11ShaderResourceView* Output() const noexcept;
        bool Initialized() const noexcept { return initialized_; }

        // エディタから触る調整値。
        float radius = 0.75f;          // ワールド単位の探索半径
        float intensity = 1.0f;        // 効果の強さ (0で無効相当)
        float power = 1.6f;            // コントラスト
        float thin_occluder = 1.0f;    // 薄い遮蔽物の過剰遮蔽を抑える量
        int   slice_count = 4;         // 方向スライス数
        int   step_count = 8;          // 1方向あたりの探索ステップ数
        float fade_start = 60.0f;      // AOを薄め始めるビュー空間距離
        float fade_end = 140.0f;       // 完全に消えるビュー空間距離
        float normal_bias = 0.35f;     // 自己遮蔽を避ける法線オフセット
        float blur_sharpness = 1.0f;   // 大きいほど輪郭を残す
        bool  enabled = true;
        bool  blur_enabled = true;

    private:
        void UploadConstants(ID3D11DeviceContext* context,
            float blur_direction_x, float blur_direction_y);

        RenderTexture occlusion_;   // 生のAO (R=可視性, G=view z)
        RenderTexture blur_;        // 横ブラー結果
        RenderTexture resolved_;    // 縦ブラー結果 (最終出力)
        Microsoft::WRL::ComPtr<ID3D11PixelShader> occlusion_shader_;
        Microsoft::WRL::ComPtr<ID3D11PixelShader> blur_shader_;
        Microsoft::WRL::ComPtr<ID3D11Buffer> constants_;
        bool initialized_ = false;
    };
}
