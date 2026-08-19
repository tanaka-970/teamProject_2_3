#include "csm_renderer.h"
#include "../../RePlayEngine/Rendering/RenderStats.h"
#include "shader.h"
#include "misc.h"
#include <algorithm>
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

    // PCSSのブロッカー探索は生の深度を読むので、比較なしの点サンプラーが必要。
    D3D11_SAMPLER_DESC point_sd{};
    point_sd.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    point_sd.AddressU = point_sd.AddressV = point_sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    point_sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
    point_sd.MinLOD = 0;
    point_sd.MaxLOD = D3D11_FLOAT32_MAX;
    hr = device->CreateSamplerState(&point_sd, point_sampler.GetAddressOf());
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
        { "MORPHPOS", 0, DXGI_FORMAT_R32G32B32_FLOAT,     0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "MORPHNORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT,  0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
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
    const XMVECTOR light = XMVector3Normalize(XMLoadFloat4(&light_direction));
    const XMVECTOR up = std::fabs(XMVectorGetY(light)) > 0.99f
        ? XMVectorSet(0, 0, 1, 0)
        : XMVectorSet(0, 1, 0, 0);

    // 射影行列から実際のニア/ファーを取り出す。LH透視射影は
    // _33 = far/(far-near)、_43 = -near*far/(far-near)。
    const float m33 = projection._33;
    const float m43 = projection._43;
    const float near_plane = (m33 != 0.0f) ? -m43 / m33 : 0.1f;
    float far_plane = ((m33 - 1.0f) != 0.0f) ? m43 / (m33 - 1.0f) : 1000.0f;
    far_plane = (std::min)(far_plane, (std::max)(shadow_distance, near_plane + 1.0f));

    // 実用的分割スキーム(Practical Split Scheme)。対数分割と等間隔分割を
    // lambda で混ぜる。対数のみだと遠景が粗すぎ、等間隔のみだと近景が粗い。
    float splits[CASCADE_COUNT]{};
    const float range = far_plane - near_plane;
    const float ratio = far_plane / (std::max)(near_plane, 1.0e-3f);
    for (UINT c = 0; c < CASCADE_COUNT; ++c)
    {
        const float p = static_cast<float>(c + 1) / static_cast<float>(CASCADE_COUNT);
        const float logarithmic = near_plane * std::pow(ratio, p);
        const float uniform = near_plane + range * p;
        splits[c] = split_lambda * logarithmic + (1.0f - split_lambda) * uniform;
    }
    constants.split_distances = { splits[0], splits[1], splits[2], splits[3] };

    const XMMATRIX view_matrix = XMLoadFloat4x4(&view);
    const XMMATRIX projection_matrix = XMLoadFloat4x4(&projection);
    const XMMATRIX inverse_view_projection =
        XMMatrixInverse(nullptr, view_matrix * projection_matrix);

    float previous_split = near_plane;
    for (UINT c = 0; c < CASCADE_COUNT; ++c)
    {
        const float split_near = previous_split;
        const float split_far = splits[c];
        previous_split = split_far;

        // 分割区間の視錐台8頂点をワールド空間で求める。NDCのzは
        // 射影行列に依存するので、深度は射影して求め直す。
        const float near_ndc_z = (split_near * m33 + m43) / (std::max)(split_near, 1.0e-4f);
        const float far_ndc_z = (split_far * m33 + m43) / (std::max)(split_far, 1.0e-4f);

        XMVECTOR corners[8];
        int corner_index = 0;
        for (int z = 0; z < 2; ++z)
        {
            const float ndc_z = z == 0 ? near_ndc_z : far_ndc_z;
            for (int y = 0; y < 2; ++y)
            {
                for (int x = 0; x < 2; ++x)
                {
                    const XMVECTOR ndc = XMVectorSet(
                        x == 0 ? -1.0f : 1.0f, y == 0 ? -1.0f : 1.0f, ndc_z, 1.0f);
                    XMVECTOR world = XMVector4Transform(ndc, inverse_view_projection);
                    world = XMVectorScale(world, 1.0f / (std::max)(XMVectorGetW(world), 1.0e-6f));
                    corners[corner_index++] = XMVectorSetW(world, 1.0f);
                }
            }
        }

        // 境界球で包む。回転に対して不変なので、カメラが回っても
        // 影の解像度が変わらず、ちらつきの原因を1つ潰せる。
        XMVECTOR center = XMVectorZero();
        for (const XMVECTOR& corner : corners) center = XMVectorAdd(center, corner);
        center = XMVectorScale(center, 1.0f / 8.0f);
        center = XMVectorSetW(center, 1.0f);

        float radius = 0.0f;
        for (const XMVECTOR& corner : corners)
        {
            const float distance = XMVectorGetX(XMVector3Length(
                XMVectorSubtract(corner, center)));
            radius = (std::max)(radius, distance);
        }
        radius = (std::max)(radius, 0.5f);
        // テクセル境界へ丸める都合上、半径も安定させておく。
        radius = std::ceil(radius * 16.0f) / 16.0f;

        const float diameter = radius * 2.0f;
        const float texels_per_unit = static_cast<float>(SHADOW_MAP_SIZE) / diameter;

        // ライト空間で中心をテクセル単位へスナップする。これが
        // シャドウシマリング(縁の毎フレームのちらつき)対策の本体。
        const XMMATRIX snap_view = XMMatrixLookAtLH(
            XMVectorZero(), light, up);
        XMVECTOR light_space_center = XMVector3TransformCoord(center, snap_view);
        light_space_center = XMVectorSet(
            std::floor(XMVectorGetX(light_space_center) * texels_per_unit) / texels_per_unit,
            std::floor(XMVectorGetY(light_space_center) * texels_per_unit) / texels_per_unit,
            XMVectorGetZ(light_space_center), 1.0f);
        center = XMVector3TransformCoord(light_space_center,
            XMMatrixInverse(nullptr, snap_view));

        const XMVECTOR eye = XMVectorSubtract(center,
            XMVectorScale(light, radius + caster_extrusion));
        const XMMATRIX cascade_view = XMMatrixLookAtLH(eye, center, up);
        // 画面外のキャスターも拾えるよう、ニアを手前へ、ファーを奥へ伸ばす。
        const XMMATRIX cascade_projection = XMMatrixOrthographicLH(
            diameter, diameter, 0.05f, radius * 2.0f + caster_extrusion * 2.0f);

        XMStoreFloat4x4(&constants.view_projection[c], cascade_view * cascade_projection);

        // 法線オフセットの基準となる1テクセルのワールド長。
        // 対角方向の最悪ケースを見込んで sqrt(2) を掛ける。
        const float texel_world_size = diameter / static_cast<float>(SHADOW_MAP_SIZE);
        reinterpret_cast<float*>(&constants.texel_world)[c] =
            texel_world_size * 1.41421356f;
    }

    constants.params2.x = static_cast<float>(SHADOW_MAP_SIZE);
    (void)scene_radius; // 視錐台から自動で範囲を決めるため使用しない。
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
    ReplayEngine::Rendering::Stats().CountStateSet(ReplayEngine::Rendering::RenderStats::StateKind::Shader, false);
    ctx->GSSetShader(caster_gs.Get(), nullptr, 0);
    ReplayEngine::Rendering::Stats().CountStateSet(ReplayEngine::Rendering::RenderStats::StateKind::Shader, false);
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
    ReplayEngine::Rendering::Stats().CountStateSet(ReplayEngine::Rendering::RenderStats::StateKind::Shader, false);
    ctx->GSSetShader(nullptr, nullptr, 0);
}

void csm_renderer::bind_resources(ID3D11DeviceContext* ctx)
{
    ctx->PSSetSamplers(5, 1, comparison_sampler.GetAddressOf());
    ctx->PSSetSamplers(6, 1, point_sampler.GetAddressOf());
    ctx->PSSetShaderResources(12, 1, shadow_srv.GetAddressOf());
}

void csm_renderer::unbind_resources(ID3D11DeviceContext* ctx)
{
    ID3D11ShaderResourceView* null_srv[1] = { nullptr };
    ctx->PSSetShaderResources(12, 1, null_srv);
}

void csm_renderer::bind_compute_resources(ID3D11DeviceContext* ctx)
{
    ctx->CSSetSamplers(5, 1, comparison_sampler.GetAddressOf());
    ctx->CSSetSamplers(6, 1, point_sampler.GetAddressOf());
    ctx->CSSetShaderResources(12, 1, shadow_srv.GetAddressOf());
    ctx->CSSetConstantBuffers(5, 1, csm_cb.GetAddressOf());
}

void csm_renderer::unbind_compute_resources(ID3D11DeviceContext* ctx)
{
    ID3D11ShaderResourceView* null_srv[1] = { nullptr };
    ctx->CSSetShaderResources(12, 1, null_srv);
}
