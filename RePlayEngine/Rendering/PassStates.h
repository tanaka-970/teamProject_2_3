#pragma once

#include <d3d11.h>
#include <wrl.h>

namespace ReplayEngine::Rendering
{
    // フルスクリーンパスが「直前のパスの状態に影響されない」ことを保証するための
    // 不透明ブレンドステート。
    //
    // このエンジンではシェーダーレイヤー(追加パス)がブレンドをALPHA/ADD/MULTIPLYへ
    // 変更したまま戻さない。その後にSSAO/SSR/TAAのフルスクリーンパスが走ると、
    // 加算や乗算のまま出力されてしまい、TAAでは履歴が毎フレーム累積して
    // シーンビューが固まったように見える。
    // 各パスはExecuteの先頭でこれを設定してから描画する。
    class PassStates final
    {
    public:
        bool Initialize(ID3D11Device* device)
        {
            opaque_blend_.Reset();
            if (!device) return false;

            D3D11_BLEND_DESC blend{};
            blend.RenderTarget[0].BlendEnable = FALSE;
            blend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
            return SUCCEEDED(device->CreateBlendState(&blend, opaque_blend_.GetAddressOf()));
        }

        // 深度テストも無効にしたいが、フルスクリーンパスはDSVを外して描くため
        // ブレンドだけ揃えれば十分。
        void ApplyOpaque(ID3D11DeviceContext* context) const
        {
            if (!context || !opaque_blend_) return;
            context->OMSetBlendState(opaque_blend_.Get(), nullptr, 0xffffffff);
        }

        bool Valid() const noexcept { return opaque_blend_ != nullptr; }

    private:
        Microsoft::WRL::ComPtr<ID3D11BlendState> opaque_blend_;
    };
}
