#include "EditorCameraController.h"

#include <algorithm>
#include <cmath>

namespace ReplayEngine::Editor
{
    namespace
    {
        bool ModifiersExactlyMatch(bool shift, bool control, bool alt,
            const EditorCameraInput& input) noexcept
        {
            return input.shift_down == shift && input.control_down == control &&
                input.alt_down == alt;
        }

        float Sign(bool inverted) noexcept { return inverted ? -1.0f : 1.0f; }
    }

    void EditorCameraController::Cancel() noexcept
    {
        mode_ = Mode::None;
        ignore_next_delta_ = false;
    }

    bool EditorCameraController::ConsumeFocusRequest() noexcept
    {
        const bool requested = focus_requested_;
        focus_requested_ = false;
        return requested;
    }

    bool EditorCameraController::CanBeginInteraction(const EditorCameraInput& input) noexcept
    {
        if (!input.window_focused) return false;
        if (!input.viewport_hovered && !input.viewport_focused) return false;
        if (input.ui_wants_mouse) return false;
        if (input.ui_wants_keyboard) return false;
        if (input.ui_text_input_active) return false;
        if (input.ui_popup_open) return false;
        if (input.gizmo_dragging) return false;
        return true;
    }

    bool EditorCameraController::ModifierDown(EditorCameraModifier modifier,
        const EditorCameraInput& input) noexcept
    {
        switch (modifier)
        {
        case EditorCameraModifier::Shift: return input.shift_down;
        case EditorCameraModifier::Control: return input.control_down;
        case EditorCameraModifier::Alt: return input.alt_down;
        case EditorCameraModifier::None:
        default: return false;
        }
    }

    bool EditorCameraController::KeyChordHeld(const EditorCameraKeyChord& chord,
        const EditorCameraInput& input, bool allow_speed_modifiers) noexcept
    {
        if (chord.Empty() || !input.KeyDown(chord.key)) return false;
        if (chord.shift && !input.shift_down) return false;
        if (chord.control && !input.control_down) return false;
        if (chord.alt && !input.alt_down) return false;

        if (!allow_speed_modifiers)
            return ModifiersExactlyMatch(chord.shift, chord.control, chord.alt, input);

        // Movement key は speed modifier を追加で許可する。
        // どの modifier を速度用にするか自体が preset で選べるため、
        // Shift/Ctrl/Alt の追加押下をここでは禁止しない。
        return true;
    }

    bool EditorCameraController::MouseGestureHeld(const EditorCameraMouseGesture& gesture,
        const EditorCameraInput& input) noexcept
    {
        if (gesture.Empty() || !input.MouseDown(gesture.button)) return false;
        return ModifiersExactlyMatch(gesture.shift, gesture.control, gesture.alt, input);
    }

    bool EditorCameraController::KeyChordPressed(const EditorCameraKeyChord& chord,
        const EditorCameraInput& input) const noexcept
    {
        if (!KeyChordHeld(chord, input, false)) return false;
        if (!has_previous_input_) return true;
        return !KeyChordHeld(chord, previous_input_, false);
    }

    void EditorCameraController::SnapshotInput(const EditorCameraInput& input) noexcept
    {
        previous_input_ = input;
        has_previous_input_ = true;
    }

    bool EditorCameraController::Update(EditorViewportCamera& camera,
        const EditorCameraInput& input, const EditorCameraPreset& preset)
    {
        if (!input.window_focused)
        {
            Cancel();
            last_mouse_x_ = input.mouse_x;
            last_mouse_y_ = input.mouse_y;
            SnapshotInput(input);
            return false;
        }

        if (input.escape_pressed && mode_ != Mode::None)
        {
            Cancel();
            last_mouse_x_ = input.mouse_x;
            last_mouse_y_ = input.mouse_y;
            SnapshotInput(input);
            return true;
        }

        const bool can_begin = CanBeginInteraction(input);

        if (mode_ == Mode::None && can_begin)
        {
            // DCC 操作は modifier 付きが多いので Orbit > Pan > Dolly > Look の順。
            if (MouseGestureHeld(preset.orbit, input)) mode_ = Mode::Orbit;
            else if (MouseGestureHeld(preset.pan, input)) mode_ = Mode::Pan;
            else if (MouseGestureHeld(preset.dolly, input)) mode_ = Mode::Dolly;
            else if (MouseGestureHeld(preset.look, input)) mode_ = Mode::Fly;

            if (mode_ != Mode::None)
            {
                ignore_next_delta_ = true;
                if (mode_ == Mode::Orbit &&
                    camera.OrbitDistance() <= EditorViewportCamera::minimum_orbit_distance)
                {
                    camera.SetOrbitPivotToViewCenter();
                }
            }
        }
        else if (mode_ != Mode::None)
        {
            bool still_held = false;
            switch (mode_)
            {
            case Mode::Fly: still_held = MouseGestureHeld(preset.look, input); break;
            case Mode::Pan: still_held = MouseGestureHeld(preset.pan, input); break;
            case Mode::Orbit: still_held = MouseGestureHeld(preset.orbit, input); break;
            case Mode::Dolly: still_held = MouseGestureHeld(preset.dolly, input); break;
            case Mode::None: break;
            }
            if (!still_held) Cancel();
        }

        float delta_x = input.mouse_x - last_mouse_x_;
        float delta_y = input.mouse_y - last_mouse_y_;
        last_mouse_x_ = input.mouse_x;
        last_mouse_y_ = input.mouse_y;
        if (ignore_next_delta_)
        {
            delta_x = 0.0f;
            delta_y = 0.0f;
            ignore_next_delta_ = false;
        }

        bool consumed = false;

        if (can_begin && KeyChordPressed(preset.focus, input))
        {
            focus_requested_ = true;
            consumed = true;
        }

        // View snap は DCC の Numpad 操作。Perspective のまま向きだけ固定する。
        if (can_begin && mode_ == Mode::None)
        {
            bool snapped = false;
            if (KeyChordPressed(preset.view_front, input))
            {
                camera.SetYawPitchRoll(0.0f, 0.0f, 0.0f); snapped = true;
            }
            else if (KeyChordPressed(preset.view_back, input))
            {
                camera.SetYawPitchRoll(DirectX::XM_PI, 0.0f, 0.0f); snapped = true;
            }
            else if (KeyChordPressed(preset.view_right, input))
            {
                camera.SetYawPitchRoll(DirectX::XM_PIDIV2, 0.0f, 0.0f); snapped = true;
            }
            else if (KeyChordPressed(preset.view_left, input))
            {
                camera.SetYawPitchRoll(-DirectX::XM_PIDIV2, 0.0f, 0.0f); snapped = true;
            }
            else if (KeyChordPressed(preset.view_top, input))
            {
                camera.SetYawPitchRoll(0.0f, -EditorViewportCamera::pitch_limit, 0.0f); snapped = true;
            }
            else if (KeyChordPressed(preset.view_bottom, input))
            {
                camera.SetYawPitchRoll(0.0f, EditorViewportCamera::pitch_limit, 0.0f); snapped = true;
            }
            if (snapped)
            {
                camera.SetOrbitPivotToViewCenter();
                consumed = true;
            }
        }

        if (input.wheel != 0.0f)
        {
            if (mode_ == Mode::Fly && preset.wheel_changes_speed_while_look)
            {
                const float factor = input.wheel > 0.0f ? 1.15f : (1.0f / 1.15f);
                camera.move_speed = (std::max)(camera.move_speed * factor, 0.001f);
                consumed = true;
            }
            else if (can_begin)
            {
                camera.Zoom(input.wheel);
                consumed = true;
            }
        }

        // Keyboard rotation: arrows 等を好きな preset へ割り当てられる。
        if (can_begin && mode_ == Mode::None)
        {
            float yaw_axis = 0.0f;
            float pitch_axis = 0.0f;
            float roll_axis = 0.0f;
            if (KeyChordHeld(preset.rotate_yaw_left, input)) yaw_axis -= 1.0f;
            if (KeyChordHeld(preset.rotate_yaw_right, input)) yaw_axis += 1.0f;
            if (KeyChordHeld(preset.rotate_pitch_up, input)) pitch_axis += 1.0f;
            if (KeyChordHeld(preset.rotate_pitch_down, input)) pitch_axis -= 1.0f;
            if (KeyChordHeld(preset.rotate_roll_left, input)) roll_axis -= 1.0f;
            if (KeyChordHeld(preset.rotate_roll_right, input)) roll_axis += 1.0f;

            if (yaw_axis != 0.0f || pitch_axis != 0.0f || roll_axis != 0.0f)
            {
                const float dt = (std::min)(input.delta_time, maximum_delta_time);
                const float radians = DirectX::XMConvertToRadians(
                    preset.keyboard_rotation_degrees) * dt;
                camera.Rotate(yaw_axis * radians, pitch_axis * radians, roll_axis * radians);
                consumed = true;
            }
        }

        auto build_move_axes = [&]()
        {
            EditorViewportCamera::MoveAxes axes;
            if (KeyChordHeld(preset.move_forward, input, true)) axes.forward += 1.0f;
            if (KeyChordHeld(preset.move_back, input, true)) axes.forward -= 1.0f;
            if (KeyChordHeld(preset.move_right, input, true)) axes.right += 1.0f;
            if (KeyChordHeld(preset.move_left, input, true)) axes.right -= 1.0f;
            if (KeyChordHeld(preset.move_up, input, true)) axes.up += 1.0f;
            if (KeyChordHeld(preset.move_down, input, true)) axes.up -= 1.0f;
            return axes;
        };

        auto movement_multiplier = [&]()
        {
            float multiplier = 1.0f;
            if (ModifierDown(preset.fast_modifier, input)) multiplier *= camera.fast_multiplier;
            if (ModifierDown(preset.slow_modifier, input)) multiplier *= camera.slow_multiplier;
            return multiplier;
        };

        if (mode_ == Mode::None && can_begin && preset.keyboard_fly_without_look)
        {
            const auto axes = build_move_axes();
            if (axes.forward != 0.0f || axes.right != 0.0f || axes.up != 0.0f)
            {
                camera.Fly(axes, movement_multiplier(),
                    (std::min)(input.delta_time, maximum_delta_time),
                    preset.world_vertical_move);
                consumed = true;
            }
        }

        switch (mode_)
        {
        case Mode::Fly:
        {
            if (delta_x != 0.0f || delta_y != 0.0f)
            {
                camera.Look(delta_x * Sign(preset.invert_look_x),
                    delta_y * Sign(preset.invert_look_y));
            }
            const auto axes = build_move_axes();
            camera.Fly(axes, movement_multiplier(),
                (std::min)(input.delta_time, maximum_delta_time),
                preset.world_vertical_move);
            consumed = true;
            break;
        }
        case Mode::Pan:
            if (delta_x != 0.0f || delta_y != 0.0f)
            {
                camera.Pan(delta_x * Sign(preset.invert_pan_x),
                    delta_y * Sign(preset.invert_pan_y));
            }
            consumed = true;
            break;
        case Mode::Orbit:
            if (delta_x != 0.0f || delta_y != 0.0f)
            {
                camera.Orbit(delta_x * Sign(preset.invert_orbit_x),
                    delta_y * Sign(preset.invert_orbit_y));
            }
            consumed = true;
            break;
        case Mode::Dolly:
            if (delta_x != 0.0f || delta_y != 0.0f)
            {
                const float delta = (delta_x - delta_y) * Sign(preset.invert_dolly);
                camera.Dolly(delta);
            }
            consumed = true;
            break;
        case Mode::None:
        default:
            break;
        }

        SnapshotInput(input);
        return consumed;
    }
}
