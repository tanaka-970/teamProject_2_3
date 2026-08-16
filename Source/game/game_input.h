#pragma once

#include "../../RePlayEngine/Scene/Services/IInputService.h"

#include <array>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>

#include <windows.h>
#include <Xinput.h>

namespace GameInput
{
    // 名前付き Action / Axis の既定マップ。
    // 数字の VK を呼び出し側へ漏らさず、設定ファイルから差し替えられるようにする。
    struct ActionBinding final
    {
        int keyboard_primary = 0;
        int keyboard_secondary = 0;
        WORD gamepad_button = 0;
    };

    enum class GamepadAxis : int
    {
        None = 0,
        LeftX = 1,
        LeftY = 2,
        RightX = 3,
        RightY = 4,
        LeftTrigger = 5,
        RightTrigger = 6,
    };

    struct AxisBinding final
    {
        int negative_primary = 0;
        int negative_secondary = 0;
        int positive_primary = 0;
        int positive_secondary = 0;
        GamepadAxis gamepad_axis = GamepadAxis::None;
        float dead_zone = 0.18f;
    };

    class InputState final : public ReplayEngine::Scene::IInputService
    {
    public:
        InputState();

        static bool ValidateDeterministicQueries(std::string& error);

        // 1 フレームに 1 回だけ呼ぶ。
        // keyboard_captured=true の間も実デバイス状態は更新し続けるが、Action/Axis へは
        // キーボード入力を公開しない。ImGui の文字入力から Gameplay へ漏らさないため。
        void BeginFrame(bool keyboard_captured, bool mouse_captured) noexcept;
        void SetSuppressed(bool suppressed) noexcept { suppressed_ = suppressed; }
        bool Suppressed() const noexcept { return suppressed_; }

        bool Held(std::string_view action, int player_slot = 0) const noexcept override;
        bool Pressed(std::string_view action, int player_slot = 0) const noexcept override;
        bool Released(std::string_view action, int player_slot = 0) const noexcept override;
        float Axis(std::string_view axis, int player_slot = 0) const noexcept override;
        bool ActionAvailable(std::string_view action) const noexcept override;
        bool AxisAvailable(std::string_view axis) const noexcept override;
        float PointerDeltaX() const noexcept override
        {
            return mouse_captured_ ? 0.0f : mouse_delta_x_;
        }
        float PointerDeltaY() const noexcept override
        {
            return mouse_captured_ ? 0.0f : mouse_delta_y_;
        }
        long PointerScreenX() const noexcept { return mouse_x_; }
        long PointerScreenY() const noexcept { return mouse_y_; }

        // Saved/Editor/InputBindings.ini。壊れた行は無視し、既定値を残して続行する。
        bool LoadBindings(const std::filesystem::path& path, std::string& error);
        bool SaveBindings(const std::filesystem::path& path, std::string& error) const;
        void ResetDefaultBindings();

    private:
        struct PadSnapshot final
        {
            XINPUT_STATE current{};
            XINPUT_STATE previous{};
            bool connected = false;
            bool previous_connected = false;
        };

        static bool KeyDown(const std::array<BYTE, 256>& state, int vk) noexcept;
        static bool IsMouseVirtualKey(int vk) noexcept;
        static bool BoundKeyDown(const std::array<BYTE, 256>& state, int vk,
            bool allow_keyboard, bool allow_mouse) noexcept;
        static float NormalizeStick(SHORT value, float dead_zone) noexcept;
        static float NormalizeTrigger(BYTE value, float dead_zone) noexcept;
        static float PadAxis(const XINPUT_GAMEPAD& pad, GamepadAxis axis,
            float dead_zone) noexcept;

        const ActionBinding* FindAction(std::string_view name) const noexcept;
        const AxisBinding* FindAxis(std::string_view name) const noexcept;
        const PadSnapshot* Pad(int player_slot) const noexcept;

        bool ActionDown(const ActionBinding& binding,
            const std::array<BYTE, 256>& keyboard,
            const PadSnapshot* pad, bool allow_keyboard, bool allow_mouse) const noexcept;
        float AxisValue(const AxisBinding& binding,
            const std::array<BYTE, 256>& keyboard,
            const PadSnapshot* pad, bool allow_keyboard) const noexcept;

        std::array<BYTE, 256> keyboard_current_{};
        std::array<BYTE, 256> keyboard_previous_{};
        std::array<PadSnapshot, XUSER_MAX_COUNT> pads_{};
        std::unordered_map<std::string, ActionBinding> actions_;
        std::unordered_map<std::string, AxisBinding> axes_;
        bool keyboard_captured_ = false;
        bool previous_keyboard_captured_ = false;
        bool mouse_captured_ = false;
        bool previous_mouse_captured_ = false;
        bool mouse_initialized_ = false;
        bool suppressed_ = false;
        long mouse_x_ = 0;
        long mouse_y_ = 0;
        float mouse_delta_x_ = 0.0f;
        float mouse_delta_y_ = 0.0f;
    };
}
