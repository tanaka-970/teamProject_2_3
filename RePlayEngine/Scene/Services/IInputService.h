#pragma once

#include <string_view>

namespace ReplayEngine::Scene
{
    // OS / デバイス固有 API を Component から切り離す入力の読み取り窓口。
    //
    // フレーム先頭で実体側が 1 回だけ状態を採取し、以降はこのスナップショットを
    // 何か所から読んでも同じ結果を返す。Pressed を「読むと消える」設計にしないのは、
    // UI と Gameplay が同じ入力を参照しても片方だけ抜ける事故を防ぐため。
    class IInputService
    {
    public:
        virtual ~IInputService() = default;

        virtual bool Held(std::string_view action, int player_slot = 0) const noexcept = 0;
        virtual bool Pressed(std::string_view action, int player_slot = 0) const noexcept = 0;
        virtual bool Released(std::string_view action, int player_slot = 0) const noexcept = 0;
        virtual float Axis(std::string_view axis, int player_slot = 0) const noexcept = 0;

        // ポインタ差分もフレーム先頭の同じスナップショットから読む。
        // Component が GetCursorPos を直接呼んで独自の「前回値」を持たないための入口。
        virtual float PointerDeltaX() const noexcept = 0;
        virtual float PointerDeltaY() const noexcept = 0;
    };
}
