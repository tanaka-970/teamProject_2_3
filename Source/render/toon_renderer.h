#pragma once

#include <d3d11.h>
#include <wrl.h>
#include <DirectXMath.h>

class toon_renderer
{
public:
    // b6 にバインドする Toon マテリアル定数
    struct toon_material_constants
    {
        DirectX::XMFLOAT4 shadow_tint   { 0.55f, 0.40f, 0.65f, 0.65f };
        DirectX::XMFLOAT4 rim_color     { 1.00f, 0.85f, 0.60f, 0.75f };
        DirectX::XMFLOAT4 specular_tint { 1.00f, 1.00f, 0.95f, 0.80f };
        DirectX::XMFLOAT4 toon_params   { 3.0f, 0.55f, 1.0f, 0.0f }; // rimPow, rimThresh, rimInt, _
        DirectX::XMFLOAT4 specular_params{ 32.0f, 0.60f, 0.8f, 0.4f }; // pow, thresh, int, aniso
    };

    // b7 にバインドするアウトライン定数
    struct outline_constants
    {
        DirectX::XMFLOAT4 outline_color  { 0.05f, 0.05f, 0.08f, 1.0f };
        DirectX::XMFLOAT4 outline_params { 0.020f, 0.020f, 0.0f, 0.0f }; // width(world), screen_corrected
    };

    Microsoft::WRL::ComPtr<ID3D11Buffer> toon_material_cb;
    Microsoft::WRL::ComPtr<ID3D11Buffer> outline_cb;

    Microsoft::WRL::ComPtr<ID3D11PixelShader>  static_toon_ps_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader>  skinned_toon_ps_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader>  outline_ps_;

    // アウトライン用 背面押し出し VS / IL
    Microsoft::WRL::ComPtr<ID3D11VertexShader> static_outline_vs_;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> skinned_outline_vs_;
    Microsoft::WRL::ComPtr<ID3D11InputLayout>  static_outline_il_;
    Microsoft::WRL::ComPtr<ID3D11InputLayout>  skinned_outline_il_;

    // 1次元ランプテクスチャ (なければデフォルトを作る)
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> ramp_srv;

    // 背面カリング用ラスタライザ (CCW front の場合 CullFront)
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> outline_rs;

    toon_material_constants material{};
    outline_constants       outline{};

    ID3D11PixelShader* static_mesh_ps()  const { return static_toon_ps_.Get(); }
    ID3D11PixelShader* skinned_mesh_ps() const { return skinned_toon_ps_.Get(); }
    ID3D11PixelShader* outline_ps()      const { return outline_ps_.Get(); }

    bool initialize(ID3D11Device* device);
    void update_constants(ID3D11DeviceContext* ctx);

    // トゥーン材質定数とランプテクスチャを描画パイプラインへ設定する。
    void bind_resources(ID3D11DeviceContext* ctx);
    void unbind_resources(ID3D11DeviceContext* ctx);

    // アウトラインパス: 外部から呼び出し、メッシュ render() に skinned_outline_vs / static_outline_vs と outline_ps を差し込む
    void bind_outline_pass(ID3D11DeviceContext* ctx, bool skinned);
};
