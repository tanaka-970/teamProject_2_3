#include "particle_system.h"
#include "shader.h"
#include "misc.h"
#include "../../RePlayEngine/Rendering/RenderStats.h"
#include <d3d11sdklayers.h>
#include <algorithm>
#include <cstring>
#include <vector>
#include <cstdio>

using namespace DirectX;
using Microsoft::WRL::ComPtr;

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

bool particle_system::initialize(ID3D11Device* device)
{
    release();
    if (!device) return false;

    HRESULT hr = S_OK;

    // パーティクル用 StructuredBuffer
    D3D11_BUFFER_DESC bd{};
    bd.ByteWidth = sizeof(particle) * MAX_COUNT;
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
    bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    bd.StructureByteStride = sizeof(particle);
    hr = device->CreateBuffer(&bd, nullptr, particle_buffer.GetAddressOf());
    if (FAILED(hr)) return false;

    D3D11_SHADER_RESOURCE_VIEW_DESC svd{};
    svd.Format = DXGI_FORMAT_UNKNOWN;
    svd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    svd.Buffer.FirstElement = 0;
    svd.Buffer.NumElements  = MAX_COUNT;
    device->CreateShaderResourceView(particle_buffer.Get(), &svd, particle_srv.GetAddressOf());

    D3D11_UNORDERED_ACCESS_VIEW_DESC uvd{};
    uvd.Format = DXGI_FORMAT_UNKNOWN;
    uvd.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    uvd.Buffer.FirstElement = 0;
    uvd.Buffer.NumElements  = MAX_COUNT;
    device->CreateUnorderedAccessView(particle_buffer.Get(), &uvd, particle_uav.GetAddressOf());
    SetDebugName(particle_uav.Get(),
        "particle_system.particle_uav Source/render/particle_system.cpp");

    // 定数バッファ
    D3D11_BUFFER_DESC cb{};
    cb.ByteWidth = sizeof(particle_constants);
    cb.Usage = D3D11_USAGE_DEFAULT;
    cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    device->CreateBuffer(&cb, nullptr, constant_buffer.GetAddressOf());

    // Compute shaders
    create_cs_from_cso(device, "initialize_particle_cs.cso", initialize_cs.GetAddressOf());
    create_cs_from_cso(device, "integrate_particle_cs.cso",  integrate_cs.GetAddressOf());

    // VS/GS/PS
    create_vs_from_cso(device, "particle_vs.cso", particle_vs.GetAddressOf(), nullptr, nullptr, 0);
    create_gs_from_cso(device, "particle_gs.cso", particle_gs.GetAddressOf());
    create_ps_from_cso(device, "particle_ps.cso", particle_ps.GetAddressOf());

    initialized = (initialize_cs && integrate_cs);
    return initialized;
}

void particle_system::release() noexcept
{
    particle_buffer.Reset();
    particle_srv.Reset();
    particle_uav.Reset();
    constant_buffer.Reset();
    initialize_cs.Reset();
    integrate_cs.Reset();
    particle_vs.Reset();
    particle_gs.Reset();
    particle_ps.Reset();
    constants = {};
    active_count = MAX_COUNT;
    pending_burst = 0;
    clear_requested = true;
    initialized = false;
}

void particle_system::simulate(ID3D11DeviceContext* ctx, float delta_time)
{
    if (!initialized) return;

    active_count = (std::max)(1u, (std::min)(active_count, MAX_COUNT));

    constants.simulation_time.x = delta_time;
    constants.simulation_time.y += delta_time;
    // 連続発生と API から要求された Burst を同じ Dispatch へまとめる。
    constants.simulation_time.w = constants.spawn_origin.w * delta_time +
        static_cast<float>((std::min)(pending_burst, active_count));
    pending_burst = 0;
    ctx->UpdateSubresource(constant_buffer.Get(), 0, nullptr, &constants, 0, 0);
    ctx->CSSetConstantBuffers(6, 1, constant_buffer.GetAddressOf());

    UINT initial_counts = 0;
    ctx->CSSetUnorderedAccessViews(0, 1, particle_uav.GetAddressOf(), &initial_counts);

    if (clear_requested)
    {
        ReplayEngine::Rendering::Stats().CountStateSet(ReplayEngine::Rendering::RenderStats::StateKind::Shader, false);
        ctx->CSSetShader(initialize_cs.Get(), nullptr, 0);
        ctx->Dispatch((MAX_COUNT + THREADS - 1) / THREADS, 1, 1);
        clear_requested = false;
    }

    ReplayEngine::Rendering::Stats().CountStateSet(ReplayEngine::Rendering::RenderStats::StateKind::Shader, false);
    ctx->CSSetShader(integrate_cs.Get(), nullptr, 0);
    ctx->Dispatch((active_count + THREADS - 1) / THREADS, 1, 1);

    // UAV detach
    ID3D11UnorderedAccessView* null_uav[1] = { nullptr };
    ctx->CSSetUnorderedAccessViews(0, 1, null_uav, nullptr);
    ReplayEngine::Rendering::Stats().CountStateSet(ReplayEngine::Rendering::RenderStats::StateKind::Shader, false);
    ctx->CSSetShader(nullptr, nullptr, 0);
}

void particle_system::render(ID3D11DeviceContext* ctx)
{
    if (!initialized) return;

    ctx->IASetInputLayout(nullptr);
    ctx->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
    ctx->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);

    ReplayEngine::Rendering::Stats().CountStateSet(ReplayEngine::Rendering::RenderStats::StateKind::Shader, false);
    ctx->VSSetShader(particle_vs.Get(), nullptr, 0);
    ReplayEngine::Rendering::Stats().CountStateSet(ReplayEngine::Rendering::RenderStats::StateKind::Shader, false);
    ctx->GSSetShader(particle_gs.Get(), nullptr, 0);
    ReplayEngine::Rendering::Stats().CountStateSet(ReplayEngine::Rendering::RenderStats::StateKind::Shader, false);
    ctx->PSSetShader(particle_ps.Get(), nullptr, 0);

    ctx->VSSetShaderResources(0, 1, particle_srv.GetAddressOf());

    ReplayEngine::Rendering::Stats().CountDraw(active_count);
    ctx->Draw(active_count, 0);

    // detach
    ID3D11ShaderResourceView* null_srv[1] = { nullptr };
    ctx->VSSetShaderResources(0, 1, null_srv);
    ReplayEngine::Rendering::Stats().CountStateSet(ReplayEngine::Rendering::RenderStats::StateKind::Shader, false);
    ctx->GSSetShader(nullptr, nullptr, 0);
}
