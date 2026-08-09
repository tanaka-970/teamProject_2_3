#pragma once

#include <d3d11.h>
#include <wrl.h>
#include <DirectXMath.h>


class deferred_renderer
{
public:
    // 0=base color+shading model, 1=emissive, 2=normal, 3=material params,
    // 4=motion vector (TAA用のスクリーン空間移動量)
    static constexpr UINT GBUFFER_COUNT = 5;
    static constexpr UINT GBUFFER_VELOCITY_INDEX = 4;

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
    // タイルドDeferredのコンピュートシェーダーから書けるようUAVも持つ。
    Microsoft::WRL::ComPtr<ID3D11Texture2D>          lit_tex;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView>   lit_rtv;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> lit_srv;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> lit_uav;

    Microsoft::WRL::ComPtr<ID3D11Buffer>             deferred_cb_buffer;
    Microsoft::WRL::ComPtr<ID3D11VertexShader>       fullscreen_vs;
    Microsoft::WRL::ComPtr<ID3D11PixelShader>        lighting_ps;

    UINT width{}, height{};
    D3D11_VIEWPORT viewport{};
    bool initialized{ false };

    // 深度プリパス用。DepthFunc=EQUAL で深度書き込みを止めた状態。
    // G-Buffer本描画でこれを使うと、各ピクセルは最前面の1回だけ書かれる。
    Microsoft::WRL::ComPtr<ID3D11DepthStencilState> depth_equal_state;

    bool initialize(ID3D11Device* device, UINT w, UINT h);
    void release() noexcept;

    // 深度だけを先に描くパス。RTVを外してピクセルシェーダーも外すため、
    // G-Buffer本描画のオーバードローを1回に抑えられる。
    // 柱が重なるようなシーンではG-Buffer PSの実行回数が目に見えて減る。
    void depth_prepass_begin(ID3D11DeviceContext* ctx);
    // clear_depth=false で呼ぶと深度を残す(プリパスの結果を使う)。
    void gbuffer_begin(ID3D11DeviceContext* ctx, FLOAT clear[4], bool clear_depth = true);
    void gbuffer_end(ID3D11DeviceContext* ctx);
    // ambient_occlusion / screen_reflection は無ければ nullptr でよい。
    // シェーダー側は未バインドを「効果なし」として扱う。
    void lighting_pass(ID3D11DeviceContext* ctx,
                       const DirectX::XMFLOAT4X4& view_projection,
                       const DirectX::XMFLOAT4& clear_color,
                       int debug_mode,
                       ID3D11ShaderResourceView* ambient_occlusion = nullptr,
                       ID3D11ShaderResourceView* screen_reflection = nullptr);
};
