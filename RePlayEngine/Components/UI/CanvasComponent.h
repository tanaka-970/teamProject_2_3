#pragma once

#include "../../Object/Component/Component.h"

#include <DirectXMath.h>

namespace ReplayEngine::Components
{
    class CanvasComponent final : public Core::Component
    {
        REPLAY_COMPONENT_BODY(CanvasComponent)

    public:
        enum ScaleMode : int
        {
            ConstantPixelSize = 0,
            ScaleWithScreenSize = 1,
        };

        CanvasComponent() = default;

        void OnAttach() override;

        DirectX::XMFLOAT2 reference_resolution{ 1920.0f, 1080.0f };
        int scale_mode = ScaleWithScreenSize;
        float match_width_or_height = 0.5f;
        int sort_order = 0;
        float opacity = 1.0f;

        // ---- 拡張点: 入れ子 Canvas / ワールド空間 Canvas --------------------
        //
        // 【今は入れていない理由】
        //   Phase 1 は ScreenSpaceOverlay の 1 経路だけを安定させる段階。
        //   入れ子 Canvas は描画順とスケール継承、ワールド空間 Canvas はカメラ行列と
        //   深度の扱いまで追加で決める必要があるため、ここでは固定にする。
        //
        // 【入れるときにここへ足す】
        //   ・render_mode を enum として追加する
        //   ・UILayout::ResolveCanvas で親 Canvas の有無を見て再入を許可する
        //   ・UIRenderer::RenderCanvas で Screen / World の行列を分ける
        //
        // 【壊してはいけない前提】
        //   ・Canvas を根として配下だけを UI として扱う
        //   ・Canvas の opacity は配下すべてへ乗算される
    };
}
