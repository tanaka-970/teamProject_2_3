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

        // Runtime APIが未知のAction/Axisを入力値の「false/0」と区別するための照会。
        // 既存のテスト用入力サービスとの互換性を保つため既定値は true とする。
        virtual bool ActionAvailable(std::string_view /*action*/) const noexcept { return true; }
        virtual bool AxisAvailable(std::string_view /*axis*/) const noexcept { return true; }

        // ポインタ差分もフレーム先頭の同じスナップショットから読む。
        // Component が GetCursorPos を直接呼んで独自の「前回値」を持たないための入口。
        virtual float PointerDeltaX() const noexcept = 0;
        virtual float PointerDeltaY() const noexcept = 0;

        // ---- 生デバイス ---------------------------------------------------
        //
        // Action / Axis で足りる場合は必ずそちらを使うこと。
        // ここは「Action を定義するほどでもない一時的な入力」と、
        // キーコンフィグ画面のような「押された生キーを知りたい」場合のための窓口。
        //
        // 既存の差し替え用サービスがそのまま使えるよう、すべて既定実装を持つ。
        // 実体を持たないサービスでは「何も押されていない」と同じ結果になる。

        // key は仮想キーコード (VK_*)。
        virtual bool KeyHeld(int /*key*/) const noexcept { return false; }
        virtual bool KeyPressed(int /*key*/) const noexcept { return false; }
        virtual bool KeyReleased(int /*key*/) const noexcept { return false; }

        // button は 0=左 1=右 2=中 3=X1 4=X2。
        virtual bool MouseButtonHeld(int /*button*/) const noexcept { return false; }
        virtual bool MouseButtonPressed(int /*button*/) const noexcept { return false; }
        virtual bool MouseButtonReleased(int /*button*/) const noexcept { return false; }

        // クライアント座標ではなく画面座標。ウィンドウ移動の影響を受けない。
        virtual float PointerX() const noexcept { return 0.0f; }
        virtual float PointerY() const noexcept { return 0.0f; }
        virtual float WheelDelta() const noexcept { return 0.0f; }

        virtual bool GamepadConnected(int /*player_slot*/) const noexcept { return false; }
        // button は XINPUT_GAMEPAD_* のビット。
        virtual bool GamepadButtonHeld(int /*player_slot*/, int /*button*/) const noexcept
        {
            return false;
        }
        virtual bool GamepadButtonPressed(int /*player_slot*/, int /*button*/) const noexcept
        {
            return false;
        }
        virtual bool GamepadButtonReleased(int /*player_slot*/, int /*button*/) const noexcept
        {
            return false;
        }
        // axis は GamepadAxisId の値。
        virtual float GamepadAxisValue(int /*player_slot*/, int /*axis*/) const noexcept
        {
            return 0.0f;
        }
        // 0..1。対応していないサービスでは false を返すだけで副作用は無い。
        virtual bool SetGamepadVibration(int /*player_slot*/, float /*low*/,
            float /*high*/) noexcept
        {
            return false;
        }
    };

    // GamepadAxisValue の axis 番号。保存データには載らないが、C# 側と一致させる。
    enum class GamepadAxisId : int
    {
        LeftStickX = 0,
        LeftStickY = 1,
        RightStickX = 2,
        RightStickY = 3,
        LeftTrigger = 4,
        RightTrigger = 5,
    };
}
