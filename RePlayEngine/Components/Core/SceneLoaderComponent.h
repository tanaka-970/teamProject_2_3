#pragma once

#include "../../Object/Component/Component.h"

namespace ReplayEngine::Components
{
    // 既存の SceneFlowService / RuntimeSceneService の状態を UI へ公開するビュー。
    // 読み込み処理や非同期 Task をここで再実装しない。
    class SceneLoaderComponent final : public Core::Component
    {
        REPLAY_COMPONENT_BODY(SceneLoaderComponent)

    public:
        float progress = 1.0f;
        bool is_loading = false;
        int state = 0;

        void OnUpdate(float delta_time) override;
    };
}
