#include "pbr_renderer.h"
#include "shader.h"
#include "misc.h"
#include "DirectXTK-main/Inc/DDSTextureLoader.h"

#include <cmath>

using namespace DirectX;
using Microsoft::WRL::ComPtr;

bool pbr_renderer::initialize(ID3D11Device* device)
{
    HRESULT hr = S_OK;

    D3D11_SAMPLER_DESC sampler_desc{};
    sampler_desc.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
    sampler_desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler_desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler_desc.MaxAnisotropy = 1;
    sampler_desc.ComparisonFunc = D3D11_COMPARISON_LESS_EQUAL;
    sampler_desc.BorderColor[0] = 1.0f;
    sampler_desc.BorderColor[1] = 1.0f;
    sampler_desc.BorderColor[2] = 1.0f;
    sampler_desc.BorderColor[3] = 1.0f;
    sampler_desc.MinLOD = 0.0f;
    sampler_desc.MaxLOD = D3D11_FLOAT32_MAX;
    hr = device->CreateSamplerState(&sampler_desc, shadow_sampler_state.GetAddressOf());
    _ASSERT_EXPR(SUCCEEDED(hr), hr_trace(hr));

    D3D11_BUFFER_DESC buffer_desc{};
    buffer_desc.Usage = D3D11_USAGE_DEFAULT;
    buffer_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

    buffer_desc.ByteWidth = sizeof(light_constants);
    hr = device->CreateBuffer(&buffer_desc, nullptr, light_cb.GetAddressOf());
    _ASSERT_EXPR(SUCCEEDED(hr), hr_trace(hr));

    buffer_desc.ByteWidth = sizeof(shadow_constants);
    hr = device->CreateBuffer(&buffer_desc, nullptr, shadow_cb.GetAddressOf());
    _ASSERT_EXPR(SUCCEEDED(hr), hr_trace(hr));

    buffer_desc.ByteWidth = sizeof(stage_material_constants);
    hr = device->CreateBuffer(&buffer_desc, nullptr, stage_material_cb.GetAddressOf());
    _ASSERT_EXPR(SUCCEEDED(hr), hr_trace(hr));

    // 同じ深度テクスチャをDSVとSRVの両方で使えるようTypelessで作成する。
    D3D11_TEXTURE2D_DESC texture_desc{};
    texture_desc.Width = shadow_map_size;
    texture_desc.Height = shadow_map_size;
    texture_desc.MipLevels = 1;
    texture_desc.ArraySize = 1;
    texture_desc.Format = DXGI_FORMAT_R32_TYPELESS;
    texture_desc.SampleDesc.Count = 1;
    texture_desc.Usage = D3D11_USAGE_DEFAULT;
    texture_desc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
    hr = device->CreateTexture2D(&texture_desc, nullptr, shadow_depth_tex.GetAddressOf());
    _ASSERT_EXPR(SUCCEEDED(hr), hr_trace(hr));

    D3D11_DEPTH_STENCIL_VIEW_DESC dsv_desc{};
    dsv_desc.Format = DXGI_FORMAT_D32_FLOAT;
    dsv_desc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    hr = device->CreateDepthStencilView(shadow_depth_tex.Get(), &dsv_desc, shadow_dsv.GetAddressOf());
    _ASSERT_EXPR(SUCCEEDED(hr), hr_trace(hr));

    D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc{};
    srv_desc.Format = DXGI_FORMAT_R32_FLOAT;
    srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srv_desc.Texture2D.MipLevels = 1;
    hr = device->CreateShaderResourceView(shadow_depth_tex.Get(), &srv_desc, shadow_srv.GetAddressOf());
    _ASSERT_EXPR(SUCCEEDED(hr), hr_trace(hr));

    shadow_viewport.TopLeftX = 0.0f;
    shadow_viewport.TopLeftY = 0.0f;
    shadow_viewport.Width = static_cast<float>(shadow_map_size);
    shadow_viewport.Height = static_cast<float>(shadow_map_size);
    shadow_viewport.MinDepth = 0.0f;
    shadow_viewport.MaxDepth = 1.0f;

    D3D11_INPUT_ELEMENT_DESC static_il[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    create_vs_from_cso(device, "shadow_caster_static_mesh_vs.cso",
        shadow_caster_static_vs.GetAddressOf(), shadow_caster_static_il.GetAddressOf(),
        static_il, ARRAYSIZE(static_il));

    D3D11_INPUT_ELEMENT_DESC skinned_il[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TANGENT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "WEIGHTS", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "BONES", 0, DXGI_FORMAT_R32G32B32A32_UINT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    create_vs_from_cso(device, "shadow_caster_skinned_mesh_vs.cso",
        shadow_caster_skinned_vs.GetAddressOf(), shadow_caster_skinned_il.GetAddressOf(),
        skinned_il, ARRAYSIZE(skinned_il));

    create_ps_from_cso(device, "static_mesh_pbr_ps.cso", static_mesh_pbr_ps_.GetAddressOf());
    create_ps_from_cso(device, "skinned_mesh_pbr_ps.cso", skinned_mesh_pbr_ps_.GetAddressOf());

    return true;
}

void pbr_renderer::load_ibl(ID3D11Device* device,
                            const wchar_t* diffuse_iem_dds,
                            const wchar_t* specular_pmrem_dds,
                            const wchar_t* lut_ggx_dds)
{
    auto try_load = [&](const wchar_t* path, ComPtr<ID3D11ShaderResourceView>& srv) {
        if (!path) return;

        ComPtr<ID3D11Resource> texture;
        HRESULT hr = CreateDDSTextureFromFile(device, path, texture.GetAddressOf(), srv.GetAddressOf());
        if (FAILED(hr))
        {
            OutputDebugStringW(L"[pbr_renderer] IBL load failed: ");
            OutputDebugStringW(path);
            OutputDebugStringW(L"\n");
        }
    };

    try_load(diffuse_iem_dds, diffuse_iem_srv);
    try_load(specular_pmrem_dds, specular_pmrem_srv);
    try_load(lut_ggx_dds, lut_ggx_srv);
}

void pbr_renderer::update_light_vp(const XMFLOAT4& light_direction,
                                   const XMFLOAT3& scene_center,
                                   float scene_radius)
{
    XMVECTOR dir = XMVector3Normalize(XMLoadFloat4(&light_direction));
    XMVECTOR focus = XMLoadFloat3(&scene_center);
    XMVECTOR eye = focus - dir * (scene_radius * 2.0f);
    XMVECTOR up = XMVectorSet(0, 1, 0, 0);
    // 光方向と上方向が平行に近い場合はLookAt行列が退化しない軸へ切り替える。
    if (std::fabs(XMVectorGetY(dir)) > 0.99f)
    {
        up = XMVectorSet(0, 0, 1, 0);
    }

    XMMATRIX view = XMMatrixLookAtLH(eye, focus, up);
    XMMATRIX projection = XMMatrixOrthographicLH(scene_radius * 4.0f, scene_radius * 4.0f,
        0.1f, scene_radius * 4.0f);
    XMStoreFloat4x4(&shadow.light_view_projection, view * projection);
}

void pbr_renderer::update_constants(ID3D11DeviceContext* ctx)
{
    ctx->UpdateSubresource(light_cb.Get(), 0, nullptr, &light, 0, 0);
    ctx->UpdateSubresource(shadow_cb.Get(), 0, nullptr, &shadow, 0, 0);
    ctx->UpdateSubresource(stage_material_cb.Get(), 0, nullptr, &stage_material, 0, 0);

    ID3D11Buffer* buffers[2] = { light_cb.Get(), shadow_cb.Get() };
    ctx->VSSetConstantBuffers(2, 2, buffers);
    ctx->PSSetConstantBuffers(2, 2, buffers);
    ctx->PSSetConstantBuffers(11, 1, stage_material_cb.GetAddressOf());
}

void pbr_renderer::shadow_begin(ID3D11DeviceContext* ctx)
{
    // DSVとして再利用する前に同じテクスチャのSRVバインドを解除する。
    ID3D11ShaderResourceView* null_srv[1] = { nullptr };
    ctx->PSSetShaderResources(4, 1, null_srv);

    ctx->OMSetRenderTargets(0, nullptr, shadow_dsv.Get());
    ctx->ClearDepthStencilView(shadow_dsv.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
    ctx->RSSetViewports(1, &shadow_viewport);
    ctx->PSSetShader(nullptr, nullptr, 0);
}

void pbr_renderer::shadow_end(ID3D11DeviceContext* ctx,
                              ID3D11RenderTargetView* restore_rtv,
                              ID3D11DepthStencilView* restore_dsv,
                              const D3D11_VIEWPORT& restore_vp)
{
    ID3D11RenderTargetView* rtvs[1] = { restore_rtv };
    ctx->OMSetRenderTargets(1, rtvs, restore_dsv);
    ctx->RSSetViewports(1, &restore_vp);
}

void pbr_renderer::bind_pbr_resources(ID3D11DeviceContext* ctx)
{
    ctx->PSSetSamplers(4, 1, shadow_sampler_state.GetAddressOf());
    ctx->PSSetShaderResources(4, 1, shadow_srv.GetAddressOf());

    ID3D11ShaderResourceView* ibl[3] = {
        diffuse_iem_srv.Get(),
        specular_pmrem_srv.Get(),
        lut_ggx_srv.Get()
    };
    ctx->PSSetShaderResources(33, 3, ibl);
}

void pbr_renderer::unbind_pbr_resources(ID3D11DeviceContext* ctx)
{
    // 次のパスで同じリソースを書き込み対象にできるよう読み取りスロットを空ける。
    ID3D11ShaderResourceView* null_shadow[1] = { nullptr };
    ID3D11ShaderResourceView* null_ibl[3] = { nullptr, nullptr, nullptr };
    ctx->PSSetShaderResources(4, 1, null_shadow);
    ctx->PSSetShaderResources(33, 3, null_ibl);
}

void pbr_renderer::bind_compute_resources(ID3D11DeviceContext* ctx)
{
    ctx->CSSetSamplers(4, 1, shadow_sampler_state.GetAddressOf());
    ctx->CSSetShaderResources(4, 1, shadow_srv.GetAddressOf());

    ID3D11ShaderResourceView* ibl[3] = {
        diffuse_iem_srv.Get(),
        specular_pmrem_srv.Get(),
        lut_ggx_srv.Get()
    };
    ctx->CSSetShaderResources(33, 3, ibl);

    // b2=LIGHT, b3=SHADOW。ピクセルシェーダー版と同じ内容を使う。
    ID3D11Buffer* buffers[2] = { light_cb.Get(), shadow_cb.Get() };
    ctx->CSSetConstantBuffers(2, 2, buffers);
}

void pbr_renderer::unbind_compute_resources(ID3D11DeviceContext* ctx)
{
    ID3D11ShaderResourceView* null_shadow[1] = { nullptr };
    ID3D11ShaderResourceView* null_ibl[3] = { nullptr, nullptr, nullptr };
    ctx->CSSetShaderResources(4, 1, null_shadow);
    ctx->CSSetShaderResources(33, 3, null_ibl);
}
