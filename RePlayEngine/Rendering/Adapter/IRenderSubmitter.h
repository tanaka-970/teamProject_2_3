#pragma once

#include "RenderItem.h"

namespace ReplayEngine::Core { class GameObject; }

namespace ReplayEngine::Rendering
{
    // 「自分を描画提出リストへ入れられる」Component が実装する境界。
    //
    // これを挟む理由:
    //   SceneRenderCollector に「MeshRenderer なら…、SkinnedMeshRenderer なら…」という
    //   型ごとの分岐を書きたくないため。
    //   収集側はこのインターフェイスだけを見るので、
    //   描画 Component が増えても Collector は変更不要。
    //
    // 実装側は D3D に触れない。詰めるのは Asset の GUID・行列・見た目の値だけ。
    class IRenderSubmitter
    {
    public:
        virtual ~IRenderSubmitter() = default;

        // 描画すべきなら out を埋めて true を返す。
        // 非表示・Asset 未指定・無効状態なら false を返して提出しない。
        virtual bool BuildRenderItem(const Core::GameObject& owner, RenderItem& out) const = 0;
    };
}
