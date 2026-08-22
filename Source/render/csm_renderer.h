#pragma once

#include <d3d11.h>
#include <wrl.h>
#include <DirectXMath.h>

// カスケードシャドウマップ。
// カメラ視錐台を分割距離で切り、各区間を包む境界球からライト空間の
// 正射影を組む。原点をテクセル単位へスナップするため、カメラが動いても
// 影の縁がちらつかない(シャドウシマリングの除去)。
class csm_renderer
{
public:
    static constexpr UINT CASCADE_COUNT = 4;
    static constexpr UINT SHADOW_MAP_SIZE = 2048;

    // Shader\csm_common.hlsli の CSM_CONSTANT_BUFFER(b5) と並びを一致させる。
    struct csm_constants
    {
        DirectX::XMFLOAT4X4 view_projection[CASCADE_COUNT];
        DirectX::XMFLOAT4   split_distances{ 12.0f, 34.0f, 90.0f, 240.0f };
        // x=depth_bias, y=normal_bias(テクセル倍率), z=filter_radius(テクセル), w=enable
        DirectX::XMFLOAT4   params{ 0.0016f, 1.4f, 2.0f, 1.0f };
        // x=shadow_map_size, y=cascade_blend(view z), z=light_size_uv, w=pcss_enable
        DirectX::XMFLOAT4   params2{ static_cast<float>(SHADOW_MAP_SIZE), 6.0f, 0.0035f, 1.0f };
        // x=slope_bias_scale, y=max_bias, z=strength, w=tap_scale
        DirectX::XMFLOAT4   params3{ 3.0f, 0.02f, 1.0f, 1.0f };
        // カスケードごとの1テクセルのワールド長 (法線オフセットの基準)
        DirectX::XMFLOAT4   texel_world{ 0.01f, 0.01f, 0.01f, 0.01f };
    };

    Microsoft::WRL::ComPtr<ID3D11Texture2D>          shadow_array;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilView>   shadow_dsv;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> shadow_srv;

    Microsoft::WRL::ComPtr<ID3D11Buffer>             csm_cb;
    Microsoft::WRL::ComPtr<ID3D11SamplerState>       comparison_sampler;
    Microsoft::WRL::ComPtr<ID3D11SamplerState>       point_sampler;

    Microsoft::WRL::ComPtr<ID3D11VertexShader>       caster_static_vs;
    Microsoft::WRL::ComPtr<ID3D11VertexShader>       caster_skinned_vs;
    Microsoft::WRL::ComPtr<ID3D11GeometryShader>     caster_gs;
    Microsoft::WRL::ComPtr<ID3D11InputLayout>        caster_static_il;
    Microsoft::WRL::ComPtr<ID3D11InputLayout>        caster_skinned_il;

    csm_constants constants{};
    D3D11_VIEWPORT viewport{};

    // 全カスケードを包むワールド空間の球。update_cascades() が毎フレーム更新する。
    // 影パスのキャスター選別に使う。ここへ届かない物体は影マップに一切写らない
    // ので、描く前に捨てられる（影パスのドローコール削減の本体）。
    DirectX::XMFLOAT3 shadow_volume_center{ 0.0f, 0.0f, 0.0f };
    float shadow_volume_radius{ 0.0f };
    // キャスター選別に使うライト方向（update_cascades() へ渡されたもの）。
    DirectX::XMFLOAT3 shadow_light_direction{ 0.0f, -1.0f, 0.0f };

    // 分割距離の対数/等間隔ブレンド係数。1に近いほど近景を細かく分ける。
    float split_lambda{ 0.85f };
    // 影を落とす最遠距離。これより遠くはシャドウを打ち切る。
    float shadow_distance{ 240.0f };
    // 画面外のキャスターを拾うためにライト方向へ伸ばす量。
    float caster_extrusion{ 60.0f };

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
    // タイルドDeferredのコンピュートシェーダー向け(スロットはステージごとに独立)。
    void bind_compute_resources(ID3D11DeviceContext* ctx);
    void unbind_compute_resources(ID3D11DeviceContext* ctx);
};
