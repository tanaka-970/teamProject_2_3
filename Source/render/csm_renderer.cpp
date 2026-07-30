#include "csm_renderer.h"
#include "shader.h"
#include "misc.h"
#include <cmath>

using namespace DirectX;
using Microsoft::WRL::ComPtr;

bool csm_renderer::initialize(ID3D11Device* device)
{
    HRESULT hr = S_OK;

    D3D11_TEXTURE2D_DESC td{};
    td.Width  = SHADOW_MAP_SIZE;
    td.Height = SHADOW_MAP_SIZE;
    td.MipLevels = 1;
    td.ArraySize = CASCADE_COUNT;
    td.Format = DXGI_FORMAT_R32_TYPELESS;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
    hr = device->CreateTexture2D(&td, nullptr, shadow_array.GetAddressOf());
    _ASSERT_EXPR(SUCCEEDED(hr), hr_trace(hr));

    D3D11_DEPTH_STENCIL_VIEW_DESC dvd{};
    dvd.Format = DXGI_FORMAT_D32_FLOAT;
    dvd.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
    dvd.Texture2DArray.MipSlice = 0;
    dvd.Texture2DArray.FirstArraySlice = 0;
    dvd.Texture2DArray.ArraySize = CASCADE_COUNT;
    hr = device->CreateDepthStencilView(shadow_array.Get(), &dvd, shadow_dsv.GetAddressOf());
    _ASSERT_EXPR(SUCCEEDED(hr), hr_trace(hr));

    D3D11_SHADER_RESOURCE_VIEW_DESC svd{};
    svd.Format = DXGI_FORMAT_R32_FLOAT;
    svd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
    svd.Texture2DArray.MipLevels = 1;
    svd.Texture2DArray.ArraySize = CASCADE_COUNT;
    hr = device->CreateShaderResourceView(shadow_array.Get(), &svd, shadow_srv.GetAddressOf());
    _ASSERT_EXPR(SUCCEEDED(hr), hr_trace(hr));

    viewport.Width    = static_cast<float>(SHADOW_MAP_SIZE);
    viewport.Height   = static_cast<float>(SHADOW_MAP_SIZE);
    viewport.MinDepth = 0;
    viewport.MaxDepth = 1;

    D3D11_BUFFER_DESC bd{};
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bd.ByteWidth = sizeof(csm_constants);
    hr = device->CreateBuffer(&bd, nullptr, csm_cb.GetAddressOf());
    _ASSERT_EXPR(SUCCEEDED(hr), hr_trace(hr));

    D3D11_SAMPLER_DESC sd{};
    sd.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.ComparisonFunc = D3D11_COMPARISON_LESS_EQUAL;
    sd.BorderColor[0] = sd.BorderColor[1] = sd.BorderColor[2] = sd.BorderColor[3] = 1.0f;
    sd.MinLOD = 0;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    hr = device->CreateSamplerState(&sd, comparison_sampler.GetAddressOf());
    _ASSERT_EXPR(SUCCEEDED(hr), hr_trace(hr));

    D3D11_INPUT_ELEMENT_DESC static_il[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    create_vs_from_cso(device, "csm_caster_static_vs.cso",
        caster_static_vs.GetAddressOf(), caster_static_il.GetAddressOf(),
        static_il, ARRAYSIZE(static_il));

    D3D11_INPUT_ELEMENT_DESC skinned_il[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,     0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT,     0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TANGENT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT,  0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,        0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "WEIGHTS",  0, DXGI_FORMAT_R32G32B32A32_FLOAT,  0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "BONES",    0, DXGI_FORMAT_R32G32B32A32_UINT,   0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    create_vs_from_cso(device, "csm_caster_skinned_vs.cso",
        caster_skinned_vs.GetAddressOf(), caster_skinned_il.GetAddressOf(),
        skinned_il, ARRAYSIZE(skinned_il));

    create_gs_from_cso(device, "csm_caster_gs.cso", caster_gs.GetAddressOf());

    return true;
}

void csm_renderer::update_cascades(const XMFLOAT4& light_direction,
                                   const XMFLOAT4X4& view,
                                   const XMFLOAT4X4& projection,
                                   float scene_radius)
{
    XMVECTOR dir = XMVector3Normalize(XMLoadFloat4(&light_direction));
    XMVECTOR up  = std::fabs(XMVectorGetY(dir)) > 0.99f
                   ? XMVectorSet(0, 0, 1, 0)
                   : XMVectorSet(0, 1, 0, 0);

    for (UINT c = 0; c < CASCADE_COUNT; ++c)
    {
        float radius = scene_radius * (1.0f + c * 0.8f);
        XMVECTOR focus = XMVectorSet(0, 0, 0, 1);
        XMVECTOR eye   = focus - dir * (radius * 2.0f);
        XMMATRIX V = XMMatrixLookAtLH(eye, focus, up);
        XMMATRIX P = XMMatrixOrthographicLH(radius * 2.0f, radius * 2.0f, 0.1f, radius * 4.0f);
        XMStoreFloat4x4(&constants.view_projection[c], V * P);
    }
}

void csm_renderer::update_constants(ID3D11DeviceContext* ctx)
{
    ctx->UpdateSubresource(csm_cb.Get(), 0, nullptr, &constants, 0, 0);
    ctx->VSSetConstantBuffers(5, 1, csm_cb.GetAddressOf());
    ctx->GSSetConstantBuffers(5, 1, csm_cb.GetAddressOf());
    ctx->PSSetConstantBuffers(5, 1, csm_cb.GetAddressOf());
}

void csm_renderer::shadow_begin(ID3D11DeviceContext* ctx)
{
    ID3D11ShaderResourceView* null_srv[1] = { nullptr };
    ctx->PSSetShaderResources(12, 1, null_srv);

    ctx->OMSetRenderTargets(0, nullptr, shadow_dsv.Get());
    ctx->ClearDepthStencilView(shadow_dsv.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
    ctx->RSSetViewports(1, &viewport);
    ctx->GSSetShader(caster_gs.Get(), nullptr, 0);
    ctx->PSSetShader(nullptr, nullptr, 0);
}

void csm_renderer::shadow_end(ID3D11DeviceContext* ctx,
                              ID3D11RenderTargetView* restore_rtv,
                              ID3D11DepthStencilView* restore_dsv,
                              const D3D11_VIEWPORT& restore_vp)
{
    ID3D11RenderTargetView* rtv[1] = { restore_rtv };
    ctx->OMSetRenderTargets(1, rtv, restore_dsv);
    ctx->RSSetViewports(1, &restore_vp);
    ctx->GSSetShader(nullptr, nullptr, 0);
}

void csm_renderer::bind_resources(ID3D11DeviceContext* ctx)
{
    ctx->PSSetSamplers(5, 1, comparison_sampler.GetAddressOf());
    ctx->PSSetShaderResources(12, 1, shadow_srv.GetAddressOf());
}

void csm_renderer::unbind_resources(ID3D11DeviceContext* ctx)
{
    ID3D11ShaderResourceView* null_srv[1] = { nullptr };
    ctx->PSSetShaderResources(12, 1, null_srv);
}
