#pragma once

#include "../../Object/Component/Component.h"

namespace ReplayEngine::Components
{
    class UIMaskComponent final : public Core::Component
    {
        REPLAY_COMPONENT_BODY(UIMaskComponent)

    public:
        UIMaskComponent() = default;

        void OnAttach() override;

        bool enabled_mask = true;
        bool show_mask_graphic = true;

        // ---- 拡張点: ステンシル / 円形 / テクスチャマスク --------------------
        //
        // 【今は入れていない理由】
        //   Phase 1 は矩形シザーだけを扱う。回転 Mask や Texture Mask は
        //   Effect Stack と同じオフスクリーン経路が必要になるため Phase 6 へ送る。
        //
        // 【入れるときにここへ足す】
        //   ・UIRenderer::Emit のシザースタックを stencil stack へ置き換える
        //   ・Mask 自身を描く pass と、子孫を stencil test で描く pass を分ける
        //   ・Circle / Texture の種類を enum としてこの Component に追加する
        //
        // 【壊してはいけない前提】
        //   ・Phase 1 の矩形 Mask は RectTransform の resolved_rect を使う
        //   ・シザー用 rasterizer state は framework の共有ステートを使う
    };
}
