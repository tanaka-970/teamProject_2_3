#pragma once

#include "EditorCameraPreset.h"
#include "EditorViewportCamera.h"

#include <array>
#include <cstddef>

namespace ReplayEngine::Editor
{
    struct EditorCameraInput
    {
        bool viewport_hovered = false;
        bool viewport_focused = false;

        bool ui_wants_mouse = false;
        bool ui_wants_keyboard = false;
        bool ui_text_input_active = false;
        bool ui_popup_open = false;
        bool gizmo_dragging = false;

        bool right_mouse_down = false;
        bool middle_mouse_down = false;
        bool left_mouse_down = false;

        float mouse_x = 0.0f;
        float mouse_y = 0.0f;
        float wheel = 0.0f;

        bool alt_down = false;
        bool shift_down = false;
        bool control_down = false;
        bool escape_pressed = false;

        std::array<bool, static_cast<std::size_t>(EditorCameraKey::Count)> keys{};

        bool window_focused = true;
        float delta_time = 0.0f;

        bool KeyDown(EditorCameraKey key) const noexcept
        {
            const auto index = static_cast<std::size_t>(key);
            return index < keys.size() && key != EditorCameraKey::None && keys[index];
        }

        bool MouseDown(EditorCameraMouseButton button) const noexcept
        {
            switch (button)
            {
            case EditorCameraMouseButton::Left: return left_mouse_down;
            case EditorCameraMouseButton::Middle: return middle_mouse_down;
            case EditorCameraMouseButton::Right: return right_mouse_down;
            case EditorCameraMouseButton::None:
            default: return false;
            }
        }
    };

    class EditorCameraController final
    {
    public:
        enum class Mode
        {
            None,
            Fly,     // Look gesture + keyboard fly
            Pan,
            Orbit,
            Dolly,
        };

        bool Update(EditorViewportCamera& camera, const EditorCameraInput& input,
            const EditorCameraPreset& preset);

        Mode CurrentMode() const noexcept { return mode_; }
        bool MouseCaptured() const noexcept { return mode_ != Mode::None; }
        void Cancel() noexcept;
        bool ConsumeFocusRequest() noexcept;

        static constexpr float maximum_delta_time = 1.0f / 15.0f;

        // ImGui / framework に依存しない開始関門。
        // framework とヘッドレス検証が同じ判定そのものを使うため公開する。
        static bool CanBeginInteraction(const EditorCameraInput& input) noexcept;

        // framework 側の Gizmo shortcut 抑止にも同じ判定を使えるよう公開。
        static bool KeyChordHeld(const EditorCameraKeyChord& chord,
            const EditorCameraInput& input, bool allow_speed_modifiers = false) noexcept;

    private:
        static bool MouseGestureHeld(const EditorCameraMouseGesture& gesture,
            const EditorCameraInput& input) noexcept;
        static bool ModifierDown(EditorCameraModifier modifier,
            const EditorCameraInput& input) noexcept;

        bool KeyChordPressed(const EditorCameraKeyChord& chord,
            const EditorCameraInput& input) const noexcept;
        void SnapshotInput(const EditorCameraInput& input) noexcept;

        Mode mode_ = Mode::None;
        float last_mouse_x_ = 0.0f;
        float last_mouse_y_ = 0.0f;
        bool ignore_next_delta_ = false;
        bool focus_requested_ = false;

        EditorCameraInput previous_input_{};
        bool has_previous_input_ = false;
    };
}
