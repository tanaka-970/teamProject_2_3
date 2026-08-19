#include "toon_renderer.h"
#include "../../RePlayEngine/Rendering/RenderStats.h"
#include "shader.h"
#include "misc.h"

using namespace DirectX;
using Microsoft::WRL::ComPtr;

// 4色ランプ (影〜中間〜ハイライト) を 1x4 のテクスチャに焼き込んだものをデフォルトで生成
static HRESULT create_default_ramp_srv(ID3D11Device* device,
                                       ComPtr<ID3D11ShaderResourceView>& out_srv)
{
    const UINT W = 4, H = 1;
    // 3段階 + 余白の rgba8。ToonPS の RampBand は 0/0.5..3 を返すので [0,1] を均等にサンプル
    uint8_t pixels[W * H * 4] =
    {
         70,  60, 100, 255,  // dark
        160, 150, 180, 255,  // mid
        240, 235, 220, 255,  // bright
        255, 255, 255, 255,  // overbright
    };

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = W;
    desc.Height = H;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA init{};
    init.pSysMem = pixels;
    init.SysMemPitch = W * 4;

    ComPtr<ID3D11Texture2D> tex;
    HRESULT hr = device->CreateTexture2D(&desc, &init, tex.GetAddressOf());
    if (FAILED(hr)) return hr;

    D3D11_SHADER_RESOURCE_VIEW_DESC srvd{};
    srvd.Format = desc.Format;
    srvd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvd.Texture2D.MipLevels = 1;
    return device->CreateShaderResourceView(tex.Get(), &srvd, out_srv.GetAddressOf());
}

bool toon_renderer::initialize(ID3D11Device* device)
{
    HRESULT hr = S_OK;

    // 定数バッファ
    D3D11_BUFFER_DESC bd{};
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

    bd.ByteWidth = sizeof(toon_material_constants);
    hr = device->CreateBuffer(&bd, nullptr, toon_material_cb.GetAddressOf());
    _ASSERT_EXPR(SUCCEEDED(hr), hr_trace(hr));

    bd.ByteWidth = sizeof(outline_constants);
    hr = device->CreateBuffer(&bd, nullptr, outline_cb.GetAddressOf());
    _ASSERT_EXPR(SUCCEEDED(hr), hr_trace(hr));

    // PS
    create_ps_from_cso(device, "skinned_mesh_toon_ps.cso", skinned_toon_ps_.GetAddressOf());
    create_ps_from_cso(device, "static_mesh_toon_ps.cso",  static_toon_ps_.GetAddressOf());
    create_ps_from_cso(device, "outline_ps.cso",            outline_ps_.GetAddressOf());

    // アウトライン VS + IL
    D3D11_INPUT_ELEMENT_DESC static_il[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    create_vs_from_cso(device, "static_mesh_outline_vs.cso",
        static_outline_vs_.GetAddressOf(), static_outline_il_.GetAddressOf(),
        static_il, ARRAYSIZE(static_il));

    D3D11_INPUT_ELEMENT_DESC skinned_il[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,     0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT,     0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TANGENT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT,  0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,        0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "WEIGHTS",  0, DXGI_FORMAT_R32G32B32A32_FLOAT,  0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "BONES",    0, DXGI_FORMAT_R32G32B32A32_UINT,   0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "MORPHPOS", 0, DXGI_FORMAT_R32G32B32_FLOAT,     0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "MORPHNORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT,  0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    create_vs_from_cso(device, "skinned_mesh_outline_vs.cso",
        skinned_outline_vs_.GetAddressOf(), skinned_outline_il_.GetAddressOf(),
        skinned_il, ARRAYSIZE(skinned_il));

    // フロントカリング (背面押し出しなので、表面 = カメラに向いている面 を描かない)
    D3D11_RASTERIZER_DESC rd{};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_FRONT;
    rd.FrontCounterClockwise = TRUE;
    rd.DepthClipEnable = TRUE;
    device->CreateRasterizerState(&rd, outline_rs.GetAddressOf());

    // デフォルトのランプ
    create_default_ramp_srv(device, ramp_srv);

    return true;
}

void toon_renderer::update_constants(ID3D11DeviceContext* ctx)
{
    ctx->UpdateSubresource(toon_material_cb.Get(), 0, nullptr, &material, 0, 0);
    ctx->UpdateSubresource(outline_cb.Get(),       0, nullptr, &outline,  0, 0);
}

void toon_renderer::bind_resources(ID3D11DeviceContext* ctx)
{
    // b6 にトゥーンマテリアル, b7 にアウトライン (アウトライン側は VS で参照)
    ctx->PSSetConstantBuffers(6, 1, toon_material_cb.GetAddressOf());
    ctx->VSSetConstantBuffers(7, 1, outline_cb.GetAddressOf());
    // t1 にランプ
    ctx->PSSetShaderResources(1, 1, ramp_srv.GetAddressOf());
}

void toon_renderer::unbind_resources(ID3D11DeviceContext* ctx)
{
    ID3D11ShaderResourceView* null_srv[1] = { nullptr };
    ctx->PSSetShaderResources(1, 1, null_srv);
}

void toon_renderer::bind_outline_pass(ID3D11DeviceContext* ctx, bool /*skinned*/)
{
    ctx->RSSetState(outline_rs.Get());
    ReplayEngine::Rendering::Stats().CountStateSet(ReplayEngine::Rendering::RenderStats::StateKind::Shader, false);
    ctx->PSSetShader(outline_ps_.Get(), nullptr, 0);
    ctx->VSSetConstantBuffers(7, 1, outline_cb.GetAddressOf());
}
