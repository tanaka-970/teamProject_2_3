#pragma once

#include <d3d11.h>
#include <wrl.h>
#include <DirectXMath.h>

class csm_renderer
{
public:
    static constexpr UINT CASCADE_COUNT = 4;
    static constexpr UINT SHADOW_MAP_SIZE = 1024;

    struct csm_constants
    {
        DirectX::XMFLOAT4X4 view_projection[CASCADE_COUNT];
        DirectX::XMFLOAT4   split_distances{ 5.0f, 15.0f, 50.0f, 200.0f };
        DirectX::XMFLOAT4   params{ 0.0015f, 0.05f, 1.0f, 1.0f }; // bias, normal_bias, filter_radius, enable
    };

    Microsoft::WRL::ComPtr<ID3D11Texture2D>          shadow_array;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilView>   shadow_dsv;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> shadow_srv;

    Microsoft::WRL::ComPtr<ID3D11Buffer>             csm_cb;
    Microsoft::WRL::ComPtr<ID3D11SamplerState>       comparison_sampler;

    Microsoft::WRL::ComPtr<ID3D11VertexShader>       caster_static_vs;
    Microsoft::WRL::ComPtr<ID3D11VertexShader>       caster_skinned_vs;
    Microsoft::WRL::ComPtr<ID3D11GeometryShader>     caster_gs;
    Microsoft::WRL::ComPtr<ID3D11InputLayout>        caster_static_il;
    Microsoft::WRL::ComPtr<ID3D11InputLayout>        caster_skinned_il;

    csm_constants constants{};
    D3D11_VIEWPORT viewport{};

    bool initialize(ID3D11Device* device);
    void update_cascades(const DirectX::XMFLOAT4& light_direction,
                         const DirectX::XMFLOAT4X4& view,
                         const DirectX::XMFLOAT4X4& projection,
                         float scene_radius);
    void update_constants(ID3D11DeviceContext* ctx);
    void shadow_begin(ID3D11DeviceContext* ctx);
    void shadow_end(ID3D11DeviceContext* ctx,
                    ID3D11RenderTargetView* restore_rtv,
                    ID3D11DepthStencilView* restore_dsv,
                    const D3D11_VIEWPORT& restore_vp);
    void bind_resources(ID3D11DeviceContext* ctx);
    void unbind_resources(ID3D11DeviceContext* ctx);
};
