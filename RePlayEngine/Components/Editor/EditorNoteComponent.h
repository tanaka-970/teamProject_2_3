#pragma once

#include "../../Object/Component/Component.h"

#include <DirectXMath.h>
#include <string>

namespace ReplayEngine::Components
{
    // Editor 専用の制作メモ。
    // Runtime のゲームロジックには参加せず、Scene View のオーバーレイだけが読む。
    // Component として保存することで GameObject 追従・Scene 保存・Undo/Prefab の
    // 既存経路をそのまま利用する。
    class EditorNoteComponent final : public Core::Component
    {
        REPLAY_COMPONENT_BODY(EditorNoteComponent)

    public:
        std::string text{ "ここを修正" };

        // 0=TODO 1=BUG 2=ART 3=PROGRAM 4=LEVEL 5=IDEA
        int category = 0;
        int priority = 1; // 0=Low 1=Normal 2=High 3=Critical
        bool completed = false;
        bool show_in_viewport = true;
        bool hide_when_completed = false;
        DirectX::XMFLOAT3 offset{ 0.0f, 1.5f, 0.0f };
    };
}
