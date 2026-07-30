#pragma once

#include <d3d11.h>
#include <wrl.h>
#include <DirectXMath.h>


class deferred_renderer
{
public:
    static constexpr UINT GBUFFER_COUNT = 4;

    struct deferred_cb
    {
        DirectX::XMFLOAT4X4 inv_view_projection;
        DirectX::XMFLOAT4   rt_size; // x=width, y=height
    };

    // GBuffer用 RT/SRV/RTV
    Microsoft::WRL::ComPtr<ID3D11Texture2D>          gbuffer_tex[GBUFFER_COUNT];
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView>   gbuffer_rtv[GBUFFER_COUNT];
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> gbuffer_srv[GBUFFER_COUNT];

    // Depth (SRV から読めるように R32_TYPELESS)
    Microsoft::WRL::ComPtr<ID3D11Texture2D>          depth_tex;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilView>   depth_dsv;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> depth_srv;

    // ライティング結果 (HDR)
    Microsoft::WRL::ComPtr<ID3D11Texture2D>          lit_tex;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView>   lit_rtv;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> lit_srv;

    Microsoft::WRL::ComPtr<ID3D11Buffer>             deferred_cb_buffer;
    Microsoft::WRL::ComPtr<ID3D11VertexShader>       fullscreen_vs;
    Microsoft::WRL::ComPtr<ID3D11PixelShader>        lighting_ps;

    UINT width{}, height{};
    D3D11_VIEWPORT viewport{};
    bool initialized{ false };

    bool initialize(ID3D11Device* device, UINT w, UINT h);
    void gbuffer_begin(ID3D11DeviceContext* ctx, FLOAT clear[4]);
    void gbuffer_end(ID3D11DeviceContext* ctx);
    void lighting_pass(ID3D11DeviceContext* ctx,
                       const DirectX::XMFLOAT4X4& view_projection,
                       const DirectX::XMFLOAT4& clear_color,
                       int debug_mode);
};
