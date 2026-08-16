#include "deferred_renderer.h"
#include "shader.h"

#include "../RenderStats.h"

#include <d3d11sdklayers.h>

#include <cstring>

using namespace DirectX;

namespace
{
    void SetDebugName(ID3D11DeviceChild* object, const char* name)
    {
#if defined(_DEBUG) || defined(DEBUG)
        if (object == nullptr || name == nullptr || *name == '\0') return;
        object->SetPrivateData(WKPDID_D3DDebugObjectName,
            static_cast<UINT>(std::strlen(name)), name);
#else
        (void)object;
        (void)name;
#endif
    }
}

bool deferred_renderer::initialize(ID3D11Device* device, UINT w, UINT h)
{
    release();
    if (!device || w == 0 || h == 0) return false;

    width = w;
    height = h;
    viewport.TopLeftX = 0.0f;
    viewport.TopLeftY = 0.0f;
    viewport.Width = static_cast<float>(w);
    viewport.Height = static_cast<float>(h);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;

    DXGI_FORMAT formats[GBUFFER_COUNT] = {
        DXGI_FORMAT_R16G16B16A16_FLOAT,
        DXGI_FORMAT_R16G16B16A16_FLOAT,
        DXGI_FORMAT_R16G16B16A16_FLOAT,
        DXGI_FORMAT_R8G8B8A8_UNORM,
        // モーションベクターは符号付きで1ピクセル未満の精度が必要なのでfloat。
        DXGI_FORMAT_R16G16_FLOAT,
    };

    for (UINT i = 0; i < GBUFFER_COUNT; ++i)
    {
        D3D11_TEXTURE2D_DESC td{};
        td.Width = w;
        td.Height = h;
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = formats[i];
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        if (FAILED(device->CreateTexture2D(&td, nullptr, gbuffer_tex[i].GetAddressOf()))) return false;
        if (FAILED(device->CreateRenderTargetView(gbuffer_tex[i].Get(), nullptr, gbuffer_rtv[i].GetAddressOf()))) return false;
        if (FAILED(device->CreateShaderResourceView(gbuffer_tex[i].Get(), nullptr, gbuffer_srv[i].GetAddressOf()))) return false;
    }

    {
        D3D11_TEXTURE2D_DESC td{};
        td.Width = w;
        td.Height = h;
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R32_TYPELESS;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
        if (FAILED(device->CreateTexture2D(&td, nullptr, depth_tex.GetAddressOf()))) return false;

        D3D11_DEPTH_STENCIL_VIEW_DESC dvd{};
        dvd.Format = DXGI_FORMAT_D32_FLOAT;
        dvd.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
        if (FAILED(device->CreateDepthStencilView(depth_tex.Get(), &dvd, depth_dsv.GetAddressOf()))) return false;

        D3D11_SHADER_RESOURCE_VIEW_DESC svd{};
        svd.Format = DXGI_FORMAT_R32_FLOAT;
        svd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        svd.Texture2D.MipLevels = 1;
        if (FAILED(device->CreateShaderResourceView(depth_tex.Get(), &svd, depth_srv.GetAddressOf()))) return false;
    }

    {
        D3D11_TEXTURE2D_DESC td{};
        td.Width = w;
        td.Height = h;
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        // タイルドDeferredはコンピュートシェーダーからUAVで書き込む。
        td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE |
            D3D11_BIND_UNORDERED_ACCESS;
        if (FAILED(device->CreateTexture2D(&td, nullptr, lit_tex.GetAddressOf()))) return false;
        if (FAILED(device->CreateRenderTargetView(lit_tex.Get(), nullptr, lit_rtv.GetAddressOf()))) return false;
        if (FAILED(device->CreateShaderResourceView(lit_tex.Get(), nullptr, lit_srv.GetAddressOf()))) return false;
        if (FAILED(device->CreateUnorderedAccessView(lit_tex.Get(), nullptr, lit_uav.GetAddressOf()))) return false;
        SetDebugName(lit_uav.Get(),
            "deferred_renderer.lit_uav RePlayEngine/Rendering/Deferred/deferred_renderer.cpp");
    }

    D3D11_BUFFER_DESC bd{};
    bd.ByteWidth = sizeof(deferred_cb);
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    if (FAILED(device->CreateBuffer(&bd, nullptr, deferred_cb_buffer.GetAddressOf()))) return false;

    // 深度プリパス後のG-Buffer描画用。既に書かれた深度と一致する
    // ピクセルだけを通し、深度は書き換えない。
    {
        D3D11_DEPTH_STENCIL_DESC dsd{};
        dsd.DepthEnable = TRUE;
        dsd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
        dsd.DepthFunc = D3D11_COMPARISON_EQUAL;
        if (FAILED(device->CreateDepthStencilState(&dsd, depth_equal_state.GetAddressOf())))
            return false;
        SetDebugName(depth_equal_state.Get(),
            "deferred_renderer.depth_equal_state RePlayEngine/Rendering/Deferred/deferred_renderer.cpp");
    }

    create_vs_from_cso(device, "fullscreen_quad_vs.cso",
        fullscreen_vs.GetAddressOf(), nullptr, nullptr, 0);
    create_ps_from_cso(device, "deferred_lighting_ps.cso", lighting_ps.GetAddressOf());

    initialized = true;
    return true;
}

void deferred_renderer::release() noexcept
{
    initialized = false;
    for (UINT i = 0; i < GBUFFER_COUNT; ++i)
    {
        gbuffer_tex[i].Reset();
        gbuffer_rtv[i].Reset();
        gbuffer_srv[i].Reset();
    }
    depth_tex.Reset();
    depth_dsv.Reset();
    depth_srv.Reset();
    lit_tex.Reset();
    lit_rtv.Reset();
    lit_srv.Reset();
    lit_uav.Reset();
    deferred_cb_buffer.Reset();
    fullscreen_vs.Reset();
    lighting_ps.Reset();
    depth_equal_state.Reset();
    width = 0;
    height = 0;
    viewport = {};
}

void deferred_renderer::depth_prepass_begin(ID3D11DeviceContext* ctx)
{
    if (!initialized || !ctx) return;

    // 深度バッファだけを対象にする。RTVは付けない。
    ctx->ClearDepthStencilView(depth_dsv.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
    ctx->RSSetViewports(1, &viewport);
    ID3D11RenderTargetView* no_rtvs[GBUFFER_COUNT]{};
    ctx->OMSetRenderTargets(GBUFFER_COUNT, no_rtvs, depth_dsv.Get());
}

void deferred_renderer::gbuffer_begin(ID3D11DeviceContext* ctx, FLOAT clear[4],
                                      bool clear_depth)
{
    if (!initialized) return;

    ID3D11RenderTargetView* rtvs[GBUFFER_COUNT]{};
    FLOAT zero[4] = { 0, 0, 0, 0 };
    for (UINT i = 0; i < GBUFFER_COUNT; ++i)
    {
        rtvs[i] = gbuffer_rtv[i].Get();
        ctx->ClearRenderTargetView(rtvs[i], i == 0 ? clear : zero);
    }

    // 深度プリパスの結果を使う場合はクリアしない。
    if (clear_depth)
        ctx->ClearDepthStencilView(depth_dsv.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
    ctx->RSSetViewports(1, &viewport);
    ctx->OMSetRenderTargets(GBUFFER_COUNT, rtvs, depth_dsv.Get());
}

void deferred_renderer::gbuffer_end(ID3D11DeviceContext* ctx)
{
    ID3D11RenderTargetView* null_rtvs[GBUFFER_COUNT]{};
    ctx->OMSetRenderTargets(GBUFFER_COUNT, null_rtvs, nullptr);
}

void deferred_renderer::lighting_pass(ID3D11DeviceContext* ctx,
                                      const XMFLOAT4X4& view_projection,
                                      const XMFLOAT4& clear_color,
                                      int debug_mode,
                                      ID3D11ShaderResourceView* ambient_occlusion,
                                      ID3D11ShaderResourceView* screen_reflection)
{
    if (!initialized || !lighting_ps || !fullscreen_vs) return;

    deferred_cb cb{};
    XMMATRIX vp = XMLoadFloat4x4(&view_projection);
    XMMATRIX inv = XMMatrixInverse(nullptr, vp);
    XMStoreFloat4x4(&cb.inv_view_projection, inv);
    cb.rt_size = XMFLOAT4(static_cast<float>(width), static_cast<float>(height),
                          static_cast<float>(debug_mode), 0);
    ctx->UpdateSubresource(deferred_cb_buffer.Get(), 0, nullptr, &cb, 0, 0);
    ctx->PSSetConstantBuffers(8, 1, deferred_cb_buffer.GetAddressOf());

    ID3D11RenderTargetView* rtvs[1] = { lit_rtv.Get() };
    ctx->OMSetRenderTargets(1, rtvs, nullptr);
    FLOAT clear[]{ clear_color.x, clear_color.y, clear_color.z, clear_color.w };
    ctx->ClearRenderTargetView(lit_rtv.Get(), clear);
    ctx->RSSetViewports(1, &viewport);

    // t4/t5 は PBR のシャドウマップ等が使うため空けておき、
    // t6=深度, t7=SSAO, t8=SSR の順で追加のスクリーン空間入力を渡す。
    ID3D11ShaderResourceView* srvs[] = {
        gbuffer_srv[0].Get(),
        gbuffer_srv[1].Get(),
        gbuffer_srv[2].Get(),
        gbuffer_srv[3].Get(),
        nullptr,
        nullptr,
        depth_srv.Get(),
        ambient_occlusion,
        screen_reflection,
    };
    ctx->PSSetShaderResources(0, _countof(srvs), srvs);

    ctx->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    ctx->IASetInputLayout(nullptr);
    ReplayEngine::Rendering::Stats().CountStateSet(ReplayEngine::Rendering::RenderStats::StateKind::Shader, false);
    ctx->VSSetShader(fullscreen_vs.Get(), nullptr, 0);
    ReplayEngine::Rendering::Stats().CountStateSet(ReplayEngine::Rendering::RenderStats::StateKind::Shader, false);
    ctx->PSSetShader(lighting_ps.Get(), nullptr, 0);
    ReplayEngine::Rendering::Stats().CountDraw(4);
    ctx->Draw(4, 0);

    ID3D11ShaderResourceView* null_srvs[_countof(srvs)]{};
    ctx->PSSetShaderResources(0, _countof(srvs), null_srvs);
}
