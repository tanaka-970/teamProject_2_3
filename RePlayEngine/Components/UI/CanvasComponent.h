#pragma once

#include "../../Object/Component/Component.h"

#include <DirectXMath.h>

namespace ReplayEngine::Components
{
    class CanvasComponent final : public Core::Component
    {
        REPLAY_COMPONENT_BODY(CanvasComponent)

    public:
        enum RenderMode : int
        {
            ScreenSpaceOverlay = 0,
            WorldSpace = 1,
        };

        enum ScaleMode : int
        {
            ConstantPixelSize = 0,
            ScaleWithScreenSize = 1,
        };

        CanvasComponent() = default;

        void OnAttach() override;

        DirectX::XMFLOAT2 reference_resolution{ 1920.0f, 1080.0f };
        int render_mode = ScreenSpaceOverlay;
        int scale_mode = ScaleWithScreenSize;
        float match_width_or_height = 0.5f;
        int sort_order = 0;
        float opacity = 1.0f;

        // Screen Space は従来どおり解決済み矩形を画面へ投影する。
        // World Space は解決矩形を変更せず、Canvas のワールド変換とカメラ行列で
        // 3D 平面へ投影する。現在の最終 UI 合成先は DSV を持たないため、深度は
        // 画面最前面となる。深度付き UI ターゲットを渡せる経路では共有 depth state を使う。
    };
}
