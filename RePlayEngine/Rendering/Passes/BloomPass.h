#pragma once

#include <d3d11.h>
#include <wrl.h>

#include <array>
#include <cstdint>
#include <memory>

class framebuffer;
class fullscreen_quad;

namespace ReplayEngine::Rendering
{
// PIKUONSを参考にした多重解像度ブルーム。
// 5段階のぼかし結果を最終パス前に合成し、従来の単段加算で生じた硬い光輪を抑える。
    class BloomPass final
    {
    public:
        BloomPass();
        ~BloomPass();
        bool Initialize(ID3D11Device* device, uint32_t width, uint32_t height);
        ID3D11ShaderResourceView* Execute(ID3D11DeviceContext* context,
            fullscreen_quad& quad, ID3D11ShaderResourceView* scene_color);
        ID3D11ShaderResourceView* Output() const noexcept;

        float threshold = 1.0f;

    private:
        static constexpr size_t kLevelCount = 5;
        struct Constants { float x, y, z, w; };

        std::array<std::unique_ptr<framebuffer>, kLevelCount> levels_;
        std::array<uint32_t, kLevelCount> widths_{};
        std::array<uint32_t, kLevelCount> heights_{};
        Microsoft::WRL::ComPtr<ID3D11PixelShader> extraction_shader_;
        Microsoft::WRL::ComPtr<ID3D11PixelShader> downsample_shader_;
        Microsoft::WRL::ComPtr<ID3D11PixelShader> upsample_shader_;
        Microsoft::WRL::ComPtr<ID3D11Buffer> constants_;
        Microsoft::WRL::ComPtr<ID3D11BlendState> additive_blend_;
        Microsoft::WRL::ComPtr<ID3D11BlendState> opaque_blend_;
        bool initialized_ = false;
    };
}
