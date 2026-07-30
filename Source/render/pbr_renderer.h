#pragma once

#include <d3d11.h>
#include <wrl.h>
#include <DirectXMath.h>

class pbr_renderer
{
public:
    struct light_constants
    {
        DirectX::XMFLOAT4 directional_color{ 1.0f, 1.0f, 1.0f, 3.598f };
        DirectX::XMFLOAT4 ibl_params{ 1.372f, 1.021f, 0.791f, 1.188f };
        DirectX::XMFLOAT4 shadow_params{ 0.741f, 0.00092f, 1.500f, 1.0f };
    };

    struct shadow_constants
    {
        DirectX::XMFLOAT4X4 light_view_projection;
    };

    struct stage_material_constants
    {
        // x=幾何法線、y=法線マップ、z=拡散IBL、w=影
        DirectX::XMFLOAT4 options{ 0.0f, 0.0f, 1.0f, 0.0f };
        DirectX::XMFLOAT4 base_tint{ 1.0f, 1.0f, 1.0f, 1.0f };
    };

    Microsoft::WRL::ComPtr<ID3D11SamplerState> shadow_sampler_state;
    Microsoft::WRL::ComPtr<ID3D11Buffer> light_cb;
    Microsoft::WRL::ComPtr<ID3D11Buffer> shadow_cb;
    Microsoft::WRL::ComPtr<ID3D11Buffer> stage_material_cb;

    light_constants light{};
    shadow_constants shadow{};
    stage_material_constants stage_material{};

    UINT shadow_map_size = 2048;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> shadow_depth_tex;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilView> shadow_dsv;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> shadow_srv;
    D3D11_VIEWPORT shadow_viewport{};

    Microsoft::WRL::ComPtr<ID3D11VertexShader> shadow_caster_static_vs;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> shadow_caster_skinned_vs;
    Microsoft::WRL::ComPtr<ID3D11InputLayout> shadow_caster_static_il;
    Microsoft::WRL::ComPtr<ID3D11InputLayout> shadow_caster_skinned_il;

    Microsoft::WRL::ComPtr<ID3D11PixelShader> static_mesh_pbr_ps_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> skinned_mesh_pbr_ps_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> skinned_mesh_pbr_stage_ps_;

    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> diffuse_iem_srv;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> specular_pmrem_srv;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> lut_ggx_srv;

    ID3D11PixelShader* static_mesh_ps() const { return static_mesh_pbr_ps_.Get(); }
    ID3D11PixelShader* skinned_mesh_ps() const { return skinned_mesh_pbr_ps_.Get(); }
    ID3D11PixelShader* skinned_mesh_stage_ps() const { return skinned_mesh_pbr_stage_ps_.Get(); }

    bool initialize(ID3D11Device* device);

    void load_ibl(ID3D11Device* device,
                  const wchar_t* diffuse_iem_dds,
                  const wchar_t* specular_pmrem_dds,
                  const wchar_t* lut_ggx_dds);

    void update_light_vp(const DirectX::XMFLOAT4& light_direction,
                         const DirectX::XMFLOAT3& scene_center,
                         float scene_radius);

    void update_constants(ID3D11DeviceContext* ctx);

    void shadow_begin(ID3D11DeviceContext* ctx);
    void shadow_end(ID3D11DeviceContext* ctx,
                    ID3D11RenderTargetView* restore_rtv,
                    ID3D11DepthStencilView* restore_dsv,
                    const D3D11_VIEWPORT& restore_vp);

    void bind_pbr_resources(ID3D11DeviceContext* ctx);
    void unbind_pbr_resources(ID3D11DeviceContext* ctx);
};
