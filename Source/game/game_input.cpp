#include "game_input.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <vector>

namespace GameInput
{
    namespace
    {
        template<typename T>
        T Clamp(T value, T minimum, T maximum) noexcept
        {
            return (std::max)(minimum, (std::min)(maximum, value));
        }
    }

    InputState::InputState()
    {
        ResetDefaultBindings();
    }

    bool InputState::ValidateDeterministicQueries(std::string& error)
    {
        error.clear();

        InputState input;
        const auto set_jump_state = [&input](bool previous_down, bool current_down)
        {
            input.keyboard_previous_.fill(0);
            input.keyboard_current_.fill(0);
            input.previous_keyboard_captured_ = false;
            input.keyboard_captured_ = false;
            input.previous_mouse_captured_ = false;
            input.mouse_captured_ = false;
            if (previous_down) input.keyboard_previous_[VK_SPACE] = 0x80u;
            if (current_down) input.keyboard_current_[VK_SPACE] = 0x80u;
            for (InputState::PadSnapshot& pad : input.pads_)
            {
                pad.current = {};
                pad.previous = {};
                pad.connected = false;
                pad.previous_connected = false;
            }
        };

        const auto expect_stable = [&input, &error](const char* label,
            bool pressed, bool held, bool released)
        {
            const bool pressed_a = input.Pressed("Jump");
            const bool pressed_b = input.Pressed("Jump");
            const bool held_a = input.Held("Jump");
            const bool held_b = input.Held("Jump");
            const bool released_a = input.Released("Jump");
            const bool released_b = input.Released("Jump");
            if (pressed_a == pressed && pressed_b == pressed &&
                held_a == held && held_b == held &&
                released_a == released && released_b == released)
            {
                return true;
            }

            std::ostringstream stream;
            stream << label << " expected P/H/R="
                << pressed << '/' << held << '/' << released
                << " actual P=" << pressed_a << ',' << pressed_b
                << " H=" << held_a << ',' << held_b
                << " R=" << released_a << ',' << released_b;
            error = stream.str();
            return false;
        };

        set_jump_state(false, true);
        if (!expect_stable("pressed", true, true, false)) return false;

        set_jump_state(true, true);
        if (!expect_stable("held", false, true, false)) return false;

        set_jump_state(true, false);
        if (!expect_stable("released", false, false, true)) return false;

        set_jump_state(false, false);
        if (!expect_stable("idle", false, false, false)) return false;

        input.actions_["Jump"].keyboard_primary_modifiers = ActionModifierCtrl;
        set_jump_state(false, true);
        if (!expect_stable("modifier missing", false, false, false)) return false;
        set_jump_state(false, true);
        input.keyboard_current_[VK_CONTROL] = 0x80u;
        if (!expect_stable("modifier pressed", true, true, false)) return false;
        set_jump_state(true, true);
        input.keyboard_previous_[VK_CONTROL] = 0x80u;
        input.keyboard_current_[VK_CONTROL] = 0x80u;
        if (!expect_stable("modifier held", false, true, false)) return false;
        set_jump_state(true, true);
        input.keyboard_previous_[VK_CONTROL] = 0x80u;
        if (!expect_stable("modifier released", false, false, true)) return false;

        const std::filesystem::path compatibility_path =
            std::filesystem::temp_directory_path() /
            ("RePlayInputBindingsV1_" + std::to_string(::GetCurrentProcessId()) + ".ini");
        {
            std::ofstream legacy(compatibility_path, std::ios::binary | std::ios::trunc);
            legacy << "# RePlayEngine Input Bindings v1\n"
                << "action \"Jump\" 74 0 4096\n"
                << "axis \"MoveX\" 65 37 68 39 1 0.18\n";
            if (!legacy)
            {
                error = "legacy InputBindings test file could not be written";
                return false;
            }
        }
        InputState legacy_input;
        const bool legacy_loaded = legacy_input.LoadBindings(compatibility_path, error);
        std::error_code remove_error;
        std::filesystem::remove(compatibility_path, remove_error);
        const auto jump = legacy_input.Actions().find("Jump");
        const auto save = legacy_input.Actions().find(u8"保存");
        if (!legacy_loaded || jump == legacy_input.Actions().end() ||
            jump->second.keyboard_primary != 'J' ||
            jump->second.keyboard_primary_modifiers != ActionModifierNone ||
            save == legacy_input.Actions().end() ||
            save->second.keyboard_primary != 'S' ||
            save->second.keyboard_primary_modifiers != ActionModifierCtrl)
        {
            if (error.empty()) error = "legacy InputBindings compatibility failed";
            return false;
        }
        if (!legacy_input.SaveBindings(compatibility_path, error)) return false;
        InputState round_trip_input;
        const bool round_trip_loaded = round_trip_input.LoadBindings(compatibility_path, error);
        std::filesystem::remove(compatibility_path, remove_error);
        const auto fullscreen = round_trip_input.Actions().find(u8"全画面");
        if (!round_trip_loaded || fullscreen == round_trip_input.Actions().end() ||
            fullscreen->second.keyboard_primary != VK_F11 ||
            fullscreen->second.keyboard_primary_modifiers != ActionModifierNone ||
            fullscreen->second.keyboard_secondary != VK_RETURN ||
            fullscreen->second.keyboard_secondary_modifiers != ActionModifierAlt)
        {
            if (error.empty()) error = "InputBindings v2 round trip failed";
            return false;
        }
        return true;
    }

    void InputState::ResetDefaultBindings()
    {
        actions_.clear();
        axes_.clear();

        actions_["Jump"] = { VK_SPACE, 0, XINPUT_GAMEPAD_A };
        actions_["Dash"] = { VK_SHIFT, 0, XINPUT_GAMEPAD_LEFT_SHOULDER };
        actions_["Menu"] = { 'P', 0, XINPUT_GAMEPAD_START };
        actions_["UISubmit"] = { VK_RETURN, VK_SPACE, XINPUT_GAMEPAD_A };
        actions_["UICancel"] = { VK_ESCAPE, 0, XINPUT_GAMEPAD_B };
        actions_["NavigateUp"] = { VK_UP, 0, XINPUT_GAMEPAD_DPAD_UP };
        actions_["NavigateDown"] = { VK_DOWN, 0, XINPUT_GAMEPAD_DPAD_DOWN };
        actions_["NavigateLeft"] = { VK_LEFT, 0, XINPUT_GAMEPAD_DPAD_LEFT };
        actions_["NavigateRight"] = { VK_RIGHT, 0, XINPUT_GAMEPAD_DPAD_RIGHT };
        actions_["PrimaryClick"] = { VK_LBUTTON, 0, 0 };
        actions_["CameraRotate"] = { VK_RBUTTON, 0, 0 };
        actions_["CameraZoomDrag"] = { VK_MBUTTON, 0, 0 };
        actions_["CameraYawLeft"] = { 'J', 0, 0 };
        actions_["CameraYawRight"] = { 'L', 0, 0 };
        actions_["CameraPitchUp"] = { 'I', 0, 0 };
        actions_["CameraPitchDown"] = { 'K', 0, 0 };
        actions_["CameraPanUp"] = { 'U', 0, 0 };
        actions_["CameraPanDown"] = { 'O', 0, 0 };
        actions_["CameraZoomIn"] = { VK_PRIOR, 0, 0 };
        actions_["CameraZoomOut"] = { VK_NEXT, 0, 0 };

        actions_[u8"保存"] = { 'S', 0, 0, "Editor", ActionModifierCtrl };
        actions_[u8"名前を付けて保存"] = { 'S', 0, 0, "Editor",
            ActionModifierCtrl | ActionModifierShift };
        actions_[u8"元に戻す"] = { 'Z', 0, 0, "Editor", ActionModifierCtrl };
        actions_[u8"やり直し"] = { 'Y', 0, 0, "Editor", ActionModifierCtrl };
        actions_[u8"複製"] = { 'D', 0, 0, "Editor", ActionModifierCtrl };
        actions_[u8"コピー"] = { 'C', 0, 0, "Editor", ActionModifierCtrl };
        actions_[u8"貼り付け"] = { 'V', 0, 0, "Editor", ActionModifierCtrl };
        actions_[u8"新規フォルダー"] = { 'N', 0, 0, "Editor",
            ActionModifierCtrl | ActionModifierShift };
        actions_[u8"検索"] = { 'F', 0, 0, "Editor", ActionModifierCtrl };
        actions_[u8"移動ギズモ"] = { 'W', 0, 0, "Editor", ActionModifierShift };
        actions_[u8"回転ギズモ"] = { 'E', 0, 0, "Editor", ActionModifierShift };
        actions_[u8"拡縮ギズモ"] = { 'R', 0, 0, "Editor", ActionModifierShift };
        actions_[u8"エディタ表示"] = { VK_F1, 0, 0, "Editor" };
        actions_[u8"名前変更"] = { VK_F2, 0, 0, "Editor" };
        actions_[u8"描画出力切り替え"] = { VK_F2, 0, 0, "Editor", ActionModifierCtrl };
        actions_[u8"入力キャプチャ"] = { VK_F3, 0, 0, "Editor" };
        actions_[u8"プロファイラ"] = { VK_F4, 0, 0, "Editor" };
        actions_[u8"実行"] = { VK_F5, 0, 0, "Editor" };
        actions_[u8"停止"] = { VK_F5, 0, 0, "Editor", ActionModifierShift };
        actions_[u8"全画面"] = { VK_F11, VK_RETURN, 0, "Editor",
            ActionModifierNone, ActionModifierAlt };

        axes_["MoveX"] = { 'A', VK_LEFT, 'D', VK_RIGHT,
            GamepadAxis::LeftX, 0.18f };
        axes_["MoveY"] = { 'S', VK_DOWN, 'W', VK_UP,
            GamepadAxis::LeftY, 0.18f };

        for (const char* name : { "Menu", "UISubmit", "UICancel", "NavigateUp",
            "NavigateDown", "NavigateLeft", "NavigateRight" })
        {
            auto found = actions_.find(name);
            if (found != actions_.end()) found->second.action_map = "UI";
        }
    }

    void InputState::BeginFrame(bool keyboard_captured, bool mouse_captured) noexcept
    {
        if (suppressed_)
        {
            keyboard_previous_.fill(0);
            keyboard_current_.fill(0);
            previous_keyboard_captured_ = true;
            previous_mouse_captured_ = true;
            keyboard_captured_ = true;
            mouse_captured_ = true;
            mouse_delta_x_ = 0.0f;
            mouse_delta_y_ = 0.0f;
            wheel_delta_ = 0.0f;
            pending_wheel_ = 0.0f;
            for (PadSnapshot& pad : pads_)
            {
                pad.current = {};
                pad.previous = {};
                pad.connected = false;
                pad.previous_connected = false;
            }
            return;
        }
        keyboard_previous_ = keyboard_current_;
        keyboard_current_.fill(0);
        previous_keyboard_captured_ = keyboard_captured_;
        previous_mouse_captured_ = mouse_captured_;
        keyboard_captured_ = keyboard_captured;
        mouse_captured_ = mouse_captured;
        mouse_delta_x_ = 0.0f;
        mouse_delta_y_ = 0.0f;
        // ホイールは前フレームに積まれたぶんをこのフレームの値として確定させる。
        wheel_delta_ = pending_wheel_;
        pending_wheel_ = 0.0f;

        // マウス座標もフレーム先頭で 1 回だけ採取する。
        // Game/Camera が個別に GetCursorPos を呼ぶと、Play 再開や複数Cameraで
        // それぞれ別の「前回値」を持ち始めるため、InputState を正本にする。
        POINT cursor{};
        if (::GetCursorPos(&cursor))
        {
            if (mouse_initialized_)
            {
                mouse_delta_x_ = static_cast<float>(cursor.x - mouse_x_);
                mouse_delta_y_ = static_cast<float>(cursor.y - mouse_y_);
            }
            mouse_x_ = cursor.x;
            mouse_y_ = cursor.y;
            mouse_initialized_ = true;
        }

        // GetKeyboardState は 256 キーを一括で採取する。
        // GetAsyncKeyState の「下位ビットを先に読んだ場所が消費する」性質を使わない。
        if (!::GetKeyboardState(keyboard_current_.data()))
        {
            keyboard_current_.fill(0);
        }

        for (DWORD slot = 0; slot < XUSER_MAX_COUNT; ++slot)
        {
            PadSnapshot& pad = pads_[slot];
            pad.previous = pad.current;
            pad.previous_connected = pad.connected;
            pad.current = {};
            pad.connected = (::XInputGetState(slot, &pad.current) == ERROR_SUCCESS);
        }
    }

    bool InputState::KeyDown(const std::array<BYTE, 256>& state, int vk) noexcept
    {
        return vk > 0 && vk < static_cast<int>(state.size()) &&
            (state[static_cast<std::size_t>(vk)] & 0x80u) != 0;
    }

    bool InputState::IsMouseVirtualKey(int vk) noexcept
    {
        return vk == VK_LBUTTON || vk == VK_RBUTTON || vk == VK_MBUTTON ||
            vk == VK_XBUTTON1 || vk == VK_XBUTTON2;
    }

    bool InputState::BoundKeyDown(const std::array<BYTE, 256>& state, int vk,
        bool allow_keyboard, bool allow_mouse) noexcept
    {
        if (vk == 0) return false;
        return (IsMouseVirtualKey(vk) ? allow_mouse : allow_keyboard) && KeyDown(state, vk);
    }

    bool InputState::ModifiersMatch(const std::array<BYTE, 256>& state, int vk,
        std::uint8_t required) noexcept
    {
        std::uint8_t actual = ActionModifierNone;
        if (vk != VK_CONTROL && vk != VK_LCONTROL && vk != VK_RCONTROL &&
            (KeyDown(state, VK_CONTROL) || KeyDown(state, VK_LCONTROL) ||
                KeyDown(state, VK_RCONTROL))) actual |= ActionModifierCtrl;
        if (vk != VK_SHIFT && vk != VK_LSHIFT && vk != VK_RSHIFT &&
            (KeyDown(state, VK_SHIFT) || KeyDown(state, VK_LSHIFT) ||
                KeyDown(state, VK_RSHIFT))) actual |= ActionModifierShift;
        if (vk != VK_MENU && vk != VK_LMENU && vk != VK_RMENU &&
            (KeyDown(state, VK_MENU) || KeyDown(state, VK_LMENU) ||
                KeyDown(state, VK_RMENU))) actual |= ActionModifierAlt;
        return actual == (required & ActionModifierAll);
    }

    bool InputState::KeyboardBindingDown(const std::array<BYTE, 256>& state, int vk,
        std::uint8_t modifiers, bool allow_keyboard, bool allow_mouse) noexcept
    {
        if ((modifiers & ActionModifierAll) != 0 && !allow_keyboard) return false;
        return BoundKeyDown(state, vk, allow_keyboard, allow_mouse) &&
            ModifiersMatch(state, vk, modifiers);
    }

    float InputState::NormalizeStick(SHORT value, float dead_zone) noexcept
    {
        const float normalized = value < 0
            ? static_cast<float>(value) / 32768.0f
            : static_cast<float>(value) / 32767.0f;
        const float magnitude = std::fabs(normalized);
        const float dz = Clamp(dead_zone, 0.0f, 0.95f);
        if (magnitude <= dz) return 0.0f;
        const float scaled = (magnitude - dz) / (1.0f - dz);
        return std::copysign(Clamp(scaled, 0.0f, 1.0f), normalized);
    }

    float InputState::NormalizeTrigger(BYTE value, float dead_zone) noexcept
    {
        const float normalized = static_cast<float>(value) / 255.0f;
        const float dz = Clamp(dead_zone, 0.0f, 0.95f);
        if (normalized <= dz) return 0.0f;
        return Clamp((normalized - dz) / (1.0f - dz), 0.0f, 1.0f);
    }

    float InputState::PadAxis(const XINPUT_GAMEPAD& pad, GamepadAxis axis,
        float dead_zone) noexcept
    {
        switch (axis)
        {
        case GamepadAxis::LeftX: return NormalizeStick(pad.sThumbLX, dead_zone);
        case GamepadAxis::LeftY: return NormalizeStick(pad.sThumbLY, dead_zone);
        case GamepadAxis::RightX: return NormalizeStick(pad.sThumbRX, dead_zone);
        case GamepadAxis::RightY: return NormalizeStick(pad.sThumbRY, dead_zone);
        case GamepadAxis::LeftTrigger: return NormalizeTrigger(pad.bLeftTrigger, dead_zone);
        case GamepadAxis::RightTrigger: return NormalizeTrigger(pad.bRightTrigger, dead_zone);
        default: return 0.0f;
        }
    }

    const ActionBinding* InputState::FindAction(std::string_view name) const noexcept
    {
        const auto it = actions_.find(std::string(name));
        return it != actions_.end() ? &it->second : nullptr;
    }

    const AxisBinding* InputState::FindAxis(std::string_view name) const noexcept
    {
        const auto it = axes_.find(std::string(name));
        return it != axes_.end() ? &it->second : nullptr;
    }

    bool InputState::ActionAvailable(std::string_view action) const noexcept
    {
        return FindAction(action) != nullptr;
    }

    bool InputState::AxisAvailable(std::string_view axis) const noexcept
    {
        return FindAxis(axis) != nullptr;
    }

    const InputState::PadSnapshot* InputState::Pad(int player_slot) const noexcept
    {
        if (player_slot < 0 || player_slot >= static_cast<int>(pads_.size())) return nullptr;
        return &pads_[static_cast<std::size_t>(player_slot)];
    }

    bool InputState::ActionDown(const ActionBinding& binding,
        const std::array<BYTE, 256>& keyboard,
        const PadSnapshot* pad, bool allow_keyboard, bool allow_mouse) const noexcept
    {
        if (KeyboardBindingDown(keyboard, binding.keyboard_primary,
                binding.keyboard_primary_modifiers, allow_keyboard, allow_mouse) ||
            KeyboardBindingDown(keyboard, binding.keyboard_secondary,
                binding.keyboard_secondary_modifiers, allow_keyboard, allow_mouse))
        {
            return true;
        }
        return pad != nullptr && pad->connected && binding.gamepad_button != 0 &&
            (pad->current.Gamepad.wButtons & binding.gamepad_button) != 0;
    }

    float InputState::AxisValue(const AxisBinding& binding,
        const std::array<BYTE, 256>& keyboard,
        const PadSnapshot* pad, bool allow_keyboard) const noexcept
    {
        float keyboard_value = 0.0f;
        if (allow_keyboard)
        {
            if (KeyDown(keyboard, binding.positive_primary) ||
                KeyDown(keyboard, binding.positive_secondary)) keyboard_value += 1.0f;
            if (KeyDown(keyboard, binding.negative_primary) ||
                KeyDown(keyboard, binding.negative_secondary)) keyboard_value -= 1.0f;
        }

        float gamepad_value = 0.0f;
        if (pad != nullptr && pad->connected)
        {
            gamepad_value = PadAxis(pad->current.Gamepad,
                binding.gamepad_axis, binding.dead_zone);
        }

        // キーボードとスティックを足すと斜め入力以外でも 2.0 になりうるため、
        // 絶対値が大きい方を採用する。入力デバイスを切り替えた瞬間も跳ねない。
        return std::fabs(gamepad_value) > std::fabs(keyboard_value)
            ? gamepad_value : keyboard_value;
    }

    bool InputState::Held(std::string_view action, int player_slot) const noexcept
    {
        const ActionBinding* binding = FindAction(action);
        if (binding == nullptr) return false;
        return ActionDown(*binding, keyboard_current_, Pad(player_slot),
            !keyboard_captured_, !mouse_captured_);
    }

    bool InputState::Pressed(std::string_view action, int player_slot) const noexcept
    {
        const ActionBinding* binding = FindAction(action);
        if (binding == nullptr) return false;
        const PadSnapshot* pad = Pad(player_slot);
        const bool current = ActionDown(*binding, keyboard_current_, pad,
            !keyboard_captured_, !mouse_captured_);

        bool previous = KeyboardBindingDown(keyboard_previous_, binding->keyboard_primary,
            binding->keyboard_primary_modifiers,
            !previous_keyboard_captured_, !previous_mouse_captured_) ||
            KeyboardBindingDown(keyboard_previous_, binding->keyboard_secondary,
                binding->keyboard_secondary_modifiers,
                !previous_keyboard_captured_, !previous_mouse_captured_);
        if (pad != nullptr && pad->previous_connected && binding->gamepad_button != 0)
        {
            previous = previous ||
                (pad->previous.Gamepad.wButtons & binding->gamepad_button) != 0;
        }
        return current && !previous;
    }

    bool InputState::Released(std::string_view action, int player_slot) const noexcept
    {
        const ActionBinding* binding = FindAction(action);
        if (binding == nullptr) return false;
        const PadSnapshot* pad = Pad(player_slot);
        const bool current = ActionDown(*binding, keyboard_current_, pad,
            !keyboard_captured_, !mouse_captured_);

        bool previous = KeyboardBindingDown(keyboard_previous_, binding->keyboard_primary,
            binding->keyboard_primary_modifiers,
            !previous_keyboard_captured_, !previous_mouse_captured_) ||
            KeyboardBindingDown(keyboard_previous_, binding->keyboard_secondary,
                binding->keyboard_secondary_modifiers,
                !previous_keyboard_captured_, !previous_mouse_captured_);
        if (pad != nullptr && pad->previous_connected && binding->gamepad_button != 0)
        {
            previous = previous ||
                (pad->previous.Gamepad.wButtons & binding->gamepad_button) != 0;
        }
        return !current && previous;
    }

    float InputState::Axis(std::string_view axis, int player_slot) const noexcept
    {
        const AxisBinding* binding = FindAxis(axis);
        if (binding == nullptr) return 0.0f;
        return AxisValue(*binding, keyboard_current_, Pad(player_slot), !keyboard_captured_);
    }

    bool InputState::LoadActionAsset(const std::filesystem::path& path, std::string& error)
    {
        error.clear();
        std::unordered_map<std::string, ActionBinding> editor_actions;
        for (const auto& entry : actions_)
            if (entry.second.action_map == "Editor") editor_actions[entry.first] = entry.second;
        ResetDefaultBindings();
        const auto restore_editor_actions = [&]()
        {
            for (const auto& entry : editor_actions) actions_[entry.first] = entry.second;
        };

        std::ifstream stream(path, std::ios::binary);
        if (!stream)
        {
            error = "Input Action Asset が見つかりません: " + path.generic_string();
            restore_editor_actions();
            return false;
        }

        // UTF-8 BOM は許容する。先頭 token に混ざると magic が一致しないため除去する。
        char bom[3]{};
        stream.read(bom, 3);
        if (!(stream.gcount() == 3 && static_cast<unsigned char>(bom[0]) == 0xEF &&
            static_cast<unsigned char>(bom[1]) == 0xBB &&
            static_cast<unsigned char>(bom[2]) == 0xBF))
        {
            stream.clear();
            stream.seekg(0, std::ios::beg);
        }

        std::string magic;
        int version = 0;
        if (!(stream >> magic >> version) || magic != "REPLAY_INPUT" ||
            (version != 1 && version != 2))
        {
            error = "Input Action Asset の形式が不正です。hard-coded default を使用します。";
            ResetDefaultBindings();
            restore_editor_actions();
            return false;
        }

        // 先に hard-coded defaults を退避し、Asset が既存 Action/Axis を欠落させないことを検証する。
        const auto default_actions = actions_;
        const auto default_axes = axes_;
        std::unordered_map<std::string, ActionBinding> loaded_actions;
        std::unordered_map<std::string, AxisBinding> loaded_axes;

        std::string line;
        std::getline(stream, line);
        while (std::getline(stream, line))
        {
            if (line.empty() || line[0] == '#') continue;
            std::istringstream row(line);
            std::string kind;
            if (!(row >> kind)) continue;
            if (kind == "scheme" || kind == "map") continue;

            std::string name;
            std::string map;
            if (!(row >> std::quoted(name) >> std::quoted(map))) continue;
            if (kind == "action")
            {
                if (map == "Editor") continue;
                ActionBinding binding{};
                const auto existing = actions_.find(name);
                if (existing != actions_.end()) binding = existing->second;
                unsigned int button = 0;
                if (row >> binding.keyboard_primary >> binding.keyboard_secondary >> button)
                {
                    unsigned int primary_modifiers = binding.keyboard_primary_modifiers;
                    unsigned int secondary_modifiers = binding.keyboard_secondary_modifiers;
                    if (version >= 2 && !(row >> primary_modifiers >> secondary_modifiers)) continue;
                    binding.gamepad_button = static_cast<WORD>(button);
                    binding.keyboard_primary_modifiers = static_cast<std::uint8_t>(
                        primary_modifiers & ActionModifierAll);
                    binding.keyboard_secondary_modifiers = static_cast<std::uint8_t>(
                        secondary_modifiers & ActionModifierAll);
                    binding.action_map = map.empty() ? "Gameplay" : map;
                    loaded_actions[name] = binding;
                }
            }
            else if (kind == "axis")
            {
                AxisBinding binding{};
                int axis = 0;
                if (row >> binding.negative_primary >> binding.negative_secondary >>
                    binding.positive_primary >> binding.positive_secondary >> axis >> binding.dead_zone &&
                    axis >= static_cast<int>(GamepadAxis::None) &&
                    axis <= static_cast<int>(GamepadAxis::RightTrigger))
                {
                    binding.gamepad_axis = static_cast<GamepadAxis>(axis);
                    binding.dead_zone = Clamp(binding.dead_zone, 0.0f, 0.95f);
                    binding.action_map = map.empty() ? "Gameplay" : map;
                    loaded_axes[name] = binding;
                }
            }
        }

        if (!stream.eof() && stream.fail())
        {
            error = "Input Action Asset の読み取り中に I/O error が発生しました。";
            ResetDefaultBindings();
            restore_editor_actions();
            return false;
        }

        for (const auto& entry : default_actions)
        {
            if (entry.second.action_map == "Editor") continue;
            if (loaded_actions.find(entry.first) == loaded_actions.end())
            {
                error = "既存 Action が不足しています: " + entry.first +
                    "。hard-coded default を使用します。";
                ResetDefaultBindings();
                restore_editor_actions();
                return false;
            }
        }
        for (const auto& entry : default_actions)
            if (entry.second.action_map == "Editor" &&
                loaded_actions.find(entry.first) == loaded_actions.end())
                loaded_actions[entry.first] = entry.second;
        for (const auto& entry : editor_actions) loaded_actions[entry.first] = entry.second;
        for (const auto& entry : default_axes)
        {
            if (loaded_axes.find(entry.first) == loaded_axes.end())
            {
                error = "既存 Axis が不足しています: " + entry.first +
                    "。hard-coded default を使用します。";
                ResetDefaultBindings();
                restore_editor_actions();
                return false;
            }
        }

        actions_ = std::move(loaded_actions);
        axes_ = std::move(loaded_axes);
        return true;
    }

    bool InputState::SaveActionAsset(const std::filesystem::path& path, std::string& error) const
    {
        error.clear();
        std::error_code ec;
        if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path(), ec);
        if (ec)
        {
            error = "Input Asset folder を作成できません: " + ec.message();
            return false;
        }

        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        if (!stream)
        {
            error = "Input Action Asset を開けません。";
            return false;
        }

        stream << "REPLAY_INPUT 2\n";
        stream << "scheme " << std::quoted("Keyboard&Mouse") << '\n';
        stream << "scheme " << std::quoted("Gamepad") << '\n';

        std::vector<std::string> maps;
        const auto add_map = [&maps](const std::string& map)
        {
            const std::string value = map.empty() ? "Gameplay" : map;
            if (std::find(maps.begin(), maps.end(), value) == maps.end()) maps.push_back(value);
        };
        for (const auto& entry : actions_)
            if (entry.second.action_map != "Editor") add_map(entry.second.action_map);
        for (const auto& entry : axes_) add_map(entry.second.action_map);
        std::sort(maps.begin(), maps.end());
        for (const std::string& map : maps) stream << "map " << std::quoted(map) << '\n';

        std::vector<std::string> names;
        names.reserve(actions_.size());
        for (const auto& pair : actions_)
            if (pair.second.action_map != "Editor") names.push_back(pair.first);
        std::sort(names.begin(), names.end());
        for (const std::string& name : names)
        {
            const ActionBinding& a = actions_.at(name);
            stream << "action " << std::quoted(name) << ' ' <<
                std::quoted(a.action_map.empty() ? "Gameplay" : a.action_map) << ' ' <<
                a.keyboard_primary << ' ' << a.keyboard_secondary << ' ' <<
                static_cast<unsigned int>(a.gamepad_button) << ' ' <<
                static_cast<unsigned int>(a.keyboard_primary_modifiers) << ' ' <<
                static_cast<unsigned int>(a.keyboard_secondary_modifiers) << '\n';
        }

        names.clear();
        names.reserve(axes_.size());
        for (const auto& pair : axes_) names.push_back(pair.first);
        std::sort(names.begin(), names.end());
        for (const std::string& name : names)
        {
            const AxisBinding& a = axes_.at(name);
            stream << "axis " << std::quoted(name) << ' ' <<
                std::quoted(a.action_map.empty() ? "Gameplay" : a.action_map) << ' ' <<
                a.negative_primary << ' ' << a.negative_secondary << ' ' <<
                a.positive_primary << ' ' << a.positive_secondary << ' ' <<
                static_cast<int>(a.gamepad_axis) << ' ' << a.dead_zone << '\n';
        }

        if (!stream)
        {
            error = "Input Action Asset の書き込みに失敗しました。";
            return false;
        }
        return true;
    }

    bool InputState::LoadBindings(const std::filesystem::path& path, std::string& error,
        bool editor_only)
    {
        error.clear();
        if (!editor_only) ResetDefaultBindings();

        std::ifstream stream(path);
        if (!stream)
        {
            // 初回起動は正常系。既定値を保存して次回から同じ経路で読む。
            return SaveBindings(path, error);
        }

        std::string line;
        while (std::getline(stream, line))
        {
            if (line.empty() || line[0] == '#') continue;

            std::istringstream row(line);
            std::string kind;
            std::string name;
            if (!(row >> kind >> std::quoted(name))) continue;

            if (kind == "action")
            {
                ActionBinding binding{};
                const auto existing = actions_.find(name);
                if (existing != actions_.end()) binding = existing->second;
                if (editor_only && (existing == actions_.end() ||
                    existing->second.action_map != "Editor")) continue;
                unsigned int pad_button = 0;
                if (row >> binding.keyboard_primary >> binding.keyboard_secondary >> pad_button)
                {
                    unsigned int primary_modifiers = binding.keyboard_primary_modifiers;
                    unsigned int secondary_modifiers = binding.keyboard_secondary_modifiers;
                    if (row >> primary_modifiers)
                    {
                        if (!(row >> secondary_modifiers)) continue;
                    }
                    binding.gamepad_button = static_cast<WORD>(pad_button);
                    binding.keyboard_primary_modifiers = static_cast<std::uint8_t>(
                        primary_modifiers & ActionModifierAll);
                    binding.keyboard_secondary_modifiers = static_cast<std::uint8_t>(
                        secondary_modifiers & ActionModifierAll);
                    actions_[name] = binding;
                }
            }
            else if (kind == "axis" && !editor_only)
            {
                AxisBinding binding{};
                const auto existing = axes_.find(name);
                if (existing != axes_.end()) binding = existing->second;
                int gamepad_axis = 0;
                if (row >> binding.negative_primary >> binding.negative_secondary >>
                    binding.positive_primary >> binding.positive_secondary >>
                    gamepad_axis >> binding.dead_zone)
                {
                    if (gamepad_axis >= static_cast<int>(GamepadAxis::None) &&
                        gamepad_axis <= static_cast<int>(GamepadAxis::RightTrigger))
                    {
                        binding.gamepad_axis = static_cast<GamepadAxis>(gamepad_axis);
                        binding.dead_zone = Clamp(binding.dead_zone, 0.0f, 0.95f);
                        axes_[name] = binding;
                    }
                }
            }
        }

        if (!stream.eof() && stream.fail())
        {
            error = "InputBindings の読み取り中に I/O エラーが発生しました";
            return false;
        }
        return true;
    }

    bool InputState::SaveBindings(const std::filesystem::path& path, std::string& error) const
    {
        error.clear();
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec)
        {
            error = "InputBindings の保存フォルダを作成できません: " + ec.message();
            return false;
        }

        std::ofstream stream(path, std::ios::trunc);
        if (!stream)
        {
            error = "InputBindings を開けません: " + path.string();
            return false;
        }

        stream << "# RePlayEngine Input Bindings v2\n";
        stream << "# action \"name\" keyboard_primary keyboard_secondary gamepad_button primary_modifiers secondary_modifiers\n";
        stream << "# axis \"name\" neg1 neg2 pos1 pos2 gamepad_axis dead_zone\n";

        std::vector<std::string> action_names;
        action_names.reserve(actions_.size());
        for (const auto& pair : actions_) action_names.push_back(pair.first);
        std::sort(action_names.begin(), action_names.end());
        for (const std::string& name : action_names)
        {
            const ActionBinding& b = actions_.at(name);
            stream << "action " << std::quoted(name) << ' '
                << b.keyboard_primary << ' ' << b.keyboard_secondary << ' '
                << static_cast<unsigned int>(b.gamepad_button) << ' '
                << static_cast<unsigned int>(b.keyboard_primary_modifiers) << ' '
                << static_cast<unsigned int>(b.keyboard_secondary_modifiers) << '\n';
        }

        std::vector<std::string> axis_names;
        axis_names.reserve(axes_.size());
        for (const auto& pair : axes_) axis_names.push_back(pair.first);
        std::sort(axis_names.begin(), axis_names.end());
        for (const std::string& name : axis_names)
        {
            const AxisBinding& b = axes_.at(name);
            stream << "axis " << std::quoted(name) << ' '
                << b.negative_primary << ' ' << b.negative_secondary << ' '
                << b.positive_primary << ' ' << b.positive_secondary << ' '
                << static_cast<int>(b.gamepad_axis) << ' ' << b.dead_zone << '\n';
        }
        return static_cast<bool>(stream);
    }

    // ---- 生デバイス --------------------------------------------------------

    namespace
    {
        constexpr int MouseVirtualKey(int button) noexcept
        {
            switch (button)
            {
            case 0: return VK_LBUTTON;
            case 1: return VK_RBUTTON;
            case 2: return VK_MBUTTON;
            case 3: return VK_XBUTTON1;
            case 4: return VK_XBUTTON2;
            default: return 0;
            }
        }
    }

    bool InputState::KeyHeld(int key) const noexcept
    {
        if (keyboard_captured_ || IsMouseVirtualKey(key)) return false;
        return KeyDown(keyboard_current_, key);
    }

    bool InputState::KeyPressed(int key) const noexcept
    {
        if (keyboard_captured_ || previous_keyboard_captured_) return false;
        if (IsMouseVirtualKey(key)) return false;
        return KeyDown(keyboard_current_, key) && !KeyDown(keyboard_previous_, key);
    }

    bool InputState::KeyReleased(int key) const noexcept
    {
        if (keyboard_captured_ || previous_keyboard_captured_) return false;
        if (IsMouseVirtualKey(key)) return false;
        return !KeyDown(keyboard_current_, key) && KeyDown(keyboard_previous_, key);
    }

    bool InputState::MouseButtonHeld(int button) const noexcept
    {
        const int key = MouseVirtualKey(button);
        if (key == 0 || mouse_captured_) return false;
        return KeyDown(keyboard_current_, key);
    }

    bool InputState::MouseButtonPressed(int button) const noexcept
    {
        const int key = MouseVirtualKey(button);
        if (key == 0 || mouse_captured_ || previous_mouse_captured_) return false;
        return KeyDown(keyboard_current_, key) && !KeyDown(keyboard_previous_, key);
    }

    bool InputState::MouseButtonReleased(int button) const noexcept
    {
        const int key = MouseVirtualKey(button);
        if (key == 0 || mouse_captured_ || previous_mouse_captured_) return false;
        return !KeyDown(keyboard_current_, key) && KeyDown(keyboard_previous_, key);
    }

    bool InputState::GamepadConnected(int player_slot) const noexcept
    {
        const PadSnapshot* pad = Pad(player_slot);
        return pad != nullptr && pad->connected;
    }

    bool InputState::GamepadButtonHeld(int player_slot, int button) const noexcept
    {
        const PadSnapshot* pad = Pad(player_slot);
        if (pad == nullptr || !pad->connected || button == 0) return false;
        return (pad->current.Gamepad.wButtons & static_cast<WORD>(button)) != 0;
    }

    bool InputState::GamepadButtonPressed(int player_slot, int button) const noexcept
    {
        const PadSnapshot* pad = Pad(player_slot);
        if (pad == nullptr || !pad->connected || button == 0) return false;
        const WORD mask = static_cast<WORD>(button);
        return (pad->current.Gamepad.wButtons & mask) != 0 &&
            (!pad->previous_connected || (pad->previous.Gamepad.wButtons & mask) == 0);
    }

    bool InputState::GamepadButtonReleased(int player_slot, int button) const noexcept
    {
        const PadSnapshot* pad = Pad(player_slot);
        if (pad == nullptr || !pad->connected || !pad->previous_connected) return false;
        const WORD mask = static_cast<WORD>(button);
        return (pad->current.Gamepad.wButtons & mask) == 0 &&
            (pad->previous.Gamepad.wButtons & mask) != 0;
    }

    float InputState::GamepadAxisValue(int player_slot, int axis) const noexcept
    {
        const PadSnapshot* pad = Pad(player_slot);
        if (pad == nullptr || !pad->connected) return 0.0f;

        // IInputService::GamepadAxisId と同じ並び。C# 側の enum とも一致させる。
        switch (axis)
        {
        case 0: return PadAxis(pad->current.Gamepad, GamepadAxis::LeftX, 0.18f);
        case 1: return PadAxis(pad->current.Gamepad, GamepadAxis::LeftY, 0.18f);
        case 2: return PadAxis(pad->current.Gamepad, GamepadAxis::RightX, 0.18f);
        case 3: return PadAxis(pad->current.Gamepad, GamepadAxis::RightY, 0.18f);
        case 4: return PadAxis(pad->current.Gamepad, GamepadAxis::LeftTrigger, 0.05f);
        case 5: return PadAxis(pad->current.Gamepad, GamepadAxis::RightTrigger, 0.05f);
        default: return 0.0f;
        }
    }

    bool InputState::SetGamepadVibration(int player_slot, float low, float high) noexcept
    {
        if (player_slot < 0 || player_slot >= static_cast<int>(pads_.size())) return false;
        if (!pads_[static_cast<std::size_t>(player_slot)].connected) return false;

        const auto scale = [](float value) noexcept -> WORD
        {
            if (!(value > 0.0f)) return 0;
            if (value > 1.0f) value = 1.0f;
            return static_cast<WORD>(value * 65535.0f);
        };
        XINPUT_VIBRATION vibration{};
        vibration.wLeftMotorSpeed = scale(low);
        vibration.wRightMotorSpeed = scale(high);
        return ::XInputSetState(static_cast<DWORD>(player_slot), &vibration) ==
            ERROR_SUCCESS;
    }
}
