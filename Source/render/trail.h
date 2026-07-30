#pragma once

#include <d3d11.h>
#include <wrl.h>
#include <DirectXMath.h>
#include <vector>

class trail
{
public:
    static constexpr int MAX_NODES = 64;

    struct vertex
    {
        DirectX::XMFLOAT3 position;
        DirectX::XMFLOAT4 color;
        DirectX::XMFLOAT2 texcoord;
    };

    struct node
    {
        DirectX::XMFLOAT3 position;
        DirectX::XMFLOAT3 right;
        float             half_width;
        float             life_remaining;
    };

    Microsoft::WRL::ComPtr<ID3D11Buffer>       vertex_buffer;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> vs;
    Microsoft::WRL::ComPtr<ID3D11PixelShader>  ps;
    Microsoft::WRL::ComPtr<ID3D11InputLayout>  input_layout;

    DirectX::XMFLOAT4 color{ 1, 1, 1, 1 };
    float fade_time{ 0.5f };
    std::vector<node> nodes;

    bool initialize(ID3D11Device* device);
    void clear() { nodes.clear(); }
    void add_node(const DirectX::XMFLOAT3& pos, const DirectX::XMFLOAT3& right, float half_width);
    void update(float delta_time);
    void render(ID3D11DeviceContext* ctx);
};
