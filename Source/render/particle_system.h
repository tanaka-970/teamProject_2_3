#pragma once

#include <d3d11.h>
#include <wrl.h>
#include <DirectXMath.h>

class particle_system
{
public:
    static constexpr UINT MAX_COUNT = 4096;
    static constexpr UINT THREADS   = 64;

    struct particle
    {
        DirectX::XMFLOAT3 position{};
        float             age{ 1.0f };
        DirectX::XMFLOAT3 velocity{};
        float             life{ 1.0f };
        DirectX::XMFLOAT4 color{ 1, 1, 1, 1 };
        float             size{ 0.1f };
        float             rotation{ 0.0f };
        float             padding[2]{};
    };

    struct particle_constants
    {
        DirectX::XMFLOAT4 spawn_origin    { 0, 0, 0, 200.0f };           // w=spawn_rate
        DirectX::XMFLOAT4 spawn_direction { 0, 1, 0, 0.4f };             // w=cone angle
        DirectX::XMFLOAT4 spawn_params    { 1.0f, 4.0f, 0.5f, 1.5f };    // min/max speed, min/max life
        DirectX::XMFLOAT4 spawn_color     { 1, 0.8f, 0.4f, 1.0f };
        DirectX::XMFLOAT4 spawn_scalar    { 0.10f, 1.5f, 1.8f, 0.5f };   // size, rotSpeed, gravity, damping
        DirectX::XMFLOAT4 simulation_time { 0.0f, 0.0f, 12345.0f, 0.0f }; // dt, total, seed, spawn_count
    };

    Microsoft::WRL::ComPtr<ID3D11Buffer>              particle_buffer;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>  particle_srv;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> particle_uav;

    Microsoft::WRL::ComPtr<ID3D11Buffer>              constant_buffer;

    Microsoft::WRL::ComPtr<ID3D11ComputeShader>       initialize_cs;
    Microsoft::WRL::ComPtr<ID3D11ComputeShader>       integrate_cs;

    Microsoft::WRL::ComPtr<ID3D11VertexShader>        particle_vs;
    Microsoft::WRL::ComPtr<ID3D11GeometryShader>      particle_gs;
    Microsoft::WRL::ComPtr<ID3D11PixelShader>         particle_ps;

    particle_constants constants{};
    bool initialized{ false };

    bool initialize(ID3D11Device* device);
    void simulate(ID3D11DeviceContext* ctx, float delta_time);
    void render(ID3D11DeviceContext* ctx);
};
