#pragma once

#include <DirectXMath.h>

namespace ReplayEngine::Core { class GameObject; }
namespace ReplayEngine::Scene { class Scene; }
namespace ReplayEngine::Components { class CanvasComponent; }

namespace ReplayEngine::UI
{
    class UILayout final
    {
    public:
        UILayout() = delete;

        static float CanvasScale(const Components::CanvasComponent& canvas,
            float screen_width, float screen_height) noexcept;

        static void Resolve(Scene::Scene& scene, float screen_width, float screen_height);
        static void ResolveCanvas(Core::GameObject& canvas_object,
            float screen_width, float screen_height);

        static void UpdateButtons(Scene::Scene& scene,
            float screen_width, float screen_height,
            float mouse_x, float mouse_y,
            bool mouse_down, bool mouse_pressed, bool mouse_released,
            bool input_captured);

        // ---- 拡張点: Layout Component -------------------------------------
        //
        // 【今は入れていない理由】
        //   Phase 1 は RectTransform の解決を固定し、親子順序と Canvas scale を
        //   先に安定させる段階。Horizontal / Vertical / Grid の自動配置は
        //   編集 UI と Undo の粒度を追加で決める必要がある。
        //
        // 【入れるときにここへ足す】
        //   ・ResolveChildren の直前で Layout Component を探す
        //   ・見つかった場合だけ、子の anchored_position / size_delta を一時上書きする
        //   ・保存値を書き換えず、確定矩形だけを変える経路にする
        //
        // 【壊してはいけない前提】
        //   ・RectTransform の保存値は anchor_min/max + anchored_position +
        //     size_delta + pivot
        //   ・解決は Canvas から子へ根順で行う
    };
}
