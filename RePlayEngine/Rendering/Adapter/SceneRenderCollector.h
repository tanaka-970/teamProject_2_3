#pragma once

#include "RenderItem.h"

namespace ReplayEngine::Scene { class Scene; }

namespace ReplayEngine::Rendering
{
    // Scene を走査して描画提出リストを作る。
    //
    // Direct3D には一切触れない。純粋な読み取り処理なので、
    // 将来ワーカースレッドへ回すこともできる（現時点ではメインスレッドで実行）。
    //
    // 収集条件:
    //   - GameObject が階層的に有効
    //   - MeshRendererComponent が有効かつ visible
    //   - Asset が指定されている
    //   - 削除予約されていない
    //
    // 同じ GameObject に MeshRendererComponent が複数付いている場合も、
    // それぞれが 1 件として提出される（登録側で複数許可にすれば動く）。
    class SceneRenderCollector final
    {
    public:
        SceneRenderCollector() = delete;

        // output は毎回 Clear されてから埋められる。
        static void Collect(const Scene::Scene& scene, RenderItemList& output);
    };
}
