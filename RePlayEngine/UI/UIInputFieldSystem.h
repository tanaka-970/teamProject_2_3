#pragma once

#include <Windows.h>

namespace ReplayEngine::Scene { class Scene; }

namespace ReplayEngine::UI
{
    class UIInputFieldSystem final
    {
    public:
        UIInputFieldSystem() = delete;

        // Runtime UI がこの Win32 メッセージを消費した場合 true。
        // ImGui が文字入力を所有している場合は呼び出し側でこの関数へ流さない。
        static bool HandleWindowMessage(Scene::Scene& scene, HWND hwnd,
            UINT message, WPARAM wparam, LPARAM lparam);

        // Editor shortcut と Runtime text edit の所有判定に使う。
        static bool HasFocusedInput(Scene::Scene& scene) noexcept;

        // フォーカス移動・外部 PropertyLink 変更後の表示同期。
        static void Refresh(Scene::Scene& scene);
    };
}
