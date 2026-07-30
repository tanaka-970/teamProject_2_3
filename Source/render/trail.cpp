#include "trail.h"
#include "shader.h"
#include "misc.h"

using namespace DirectX;
using Microsoft::WRL::ComPtr;

bool trail::initialize(ID3D11Device* device)
{
    HRESULT hr = S_OK;

    D3D11_BUFFER_DESC bd{};
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    bd.ByteWidth = sizeof(vertex) * MAX_NODES * 2;
    hr = device->CreateBuffer(&bd, nullptr, vertex_buffer.GetAddressOf());
    _ASSERT_EXPR(SUCCEEDED(hr), hr_trace(hr));

    D3D11_INPUT_ELEMENT_DESC il[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 0,                            D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    create_vs_from_cso(device, "trail_vs.cso",
        vs.GetAddressOf(), input_layout.GetAddressOf(), il, ARRAYSIZE(il));
    create_ps_from_cso(device, "trail_ps.cso", ps.GetAddressOf());

    nodes.reserve(MAX_NODES);
    return true;
}

void trail::add_node(const XMFLOAT3& pos, const XMFLOAT3& right, float half_width)
{
    node n;
    n.position   = pos;
    n.right      = right;
    n.half_width = half_width;
    n.life_remaining = fade_time;
    nodes.push_back(n);
    if ((int)nodes.size() > MAX_NODES) nodes.erase(nodes.begin());
}

void trail::update(float dt)
{
    for (auto it = nodes.begin(); it != nodes.end(); )
    {
        it->life_remaining -= dt;
        if (it->life_remaining <= 0.0f) it = nodes.erase(it);
        else ++it;
    }
}

void trail::render(ID3D11DeviceContext* ctx)
{
    if (nodes.size() < 2) return;

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(ctx->Map(vertex_buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return;
    vertex* v = reinterpret_cast<vertex*>(mapped.pData);

    int n = (int) nodes.size();
    for (int i = 0; i < n; ++i)
    {
        const auto& nd = nodes[i];
        float t = 1.0f - (nd.life_remaining / fade_time);
        XMFLOAT4 c = color;
        c.w *= (1.0f - t);

        XMFLOAT3 a = { nd.position.x - nd.right.x * nd.half_width,
                       nd.position.y - nd.right.y * nd.half_width,
                       nd.position.z - nd.right.z * nd.half_width };
        XMFLOAT3 b = { nd.position.x + nd.right.x * nd.half_width,
                       nd.position.y + nd.right.y * nd.half_width,
                       nd.position.z + nd.right.z * nd.half_width };
        float u = (float) i / (float)(n - 1);
        v[i*2+0] = { a, c, {0, u} };
        v[i*2+1] = { b, c, {1, u} };
    }
    ctx->Unmap(vertex_buffer.Get(), 0);

    UINT stride = sizeof(vertex), offset = 0;
    ctx->IASetVertexBuffers(0, 1, vertex_buffer.GetAddressOf(), &stride, &offset);
    ctx->IASetInputLayout(input_layout.Get());
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    ctx->VSSetShader(vs.Get(), nullptr, 0);
    ctx->PSSetShader(ps.Get(), nullptr, 0);
    ctx->Draw(n * 2, 0);
}
