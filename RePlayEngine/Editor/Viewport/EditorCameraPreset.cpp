// Editor Camera Preset のうち、Preset model と入力名の相互変換だけを持つ。
//
//   EditorCameraPreset.cpp             ... 値の補正・適用と入力名変換（このファイル）
//   EditorCameraPresetPersistence.cpp  ... Preset / user selection の永続化

#include "EditorCameraPreset.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <map>
#include <random>
#include <sstream>

namespace ReplayEngine::Editor
{
    void EditorCameraPreset::Sanitize() noexcept
    {
        if (!(move_speed > 0.0f) || !std::isfinite(move_speed)) move_speed = 5.0f;
        if (!(fast_multiplier > 0.0f) || !std::isfinite(fast_multiplier)) fast_multiplier = 4.0f;
        if (!(slow_multiplier > 0.0f) || !std::isfinite(slow_multiplier)) slow_multiplier = 0.25f;
        if (!(mouse_sensitivity > 0.0f) || !std::isfinite(mouse_sensitivity)) mouse_sensitivity = 0.15f;
        if (!(pan_sensitivity > 0.0f) || !std::isfinite(pan_sensitivity)) pan_sensitivity = 1.0f;
        if (!(zoom_sensitivity > 0.0f) || !std::isfinite(zoom_sensitivity)) zoom_sensitivity = 1.0f;
        if (!(keyboard_rotation_degrees > 0.0f) || !std::isfinite(keyboard_rotation_degrees))
            keyboard_rotation_degrees = 90.0f;
        if (!(field_of_view_degrees > 1.0f) || field_of_view_degrees >= 179.0f ||
            !std::isfinite(field_of_view_degrees)) field_of_view_degrees = 60.0f;
        if (!(near_clip > 0.0f) || !std::isfinite(near_clip)) near_clip = 0.05f;
        if (!(far_clip > near_clip) || !std::isfinite(far_clip)) far_clip = 10000.0f;
    }

    void EditorCameraPreset::ApplyCameraSettings(EditorViewportCamera& camera) const noexcept
    {
        camera.move_speed = move_speed;
        camera.fast_multiplier = fast_multiplier;
        camera.slow_multiplier = slow_multiplier;
        camera.mouse_sensitivity = mouse_sensitivity;
        camera.pan_sensitivity = pan_sensitivity;
        camera.zoom_sensitivity = zoom_sensitivity;
        camera.field_of_view_degrees = field_of_view_degrees;
        camera.near_clip = near_clip;
        camera.far_clip = far_clip;
    }

    void EditorCameraPreset::CaptureCameraSettings(const EditorViewportCamera& camera) noexcept
    {
        move_speed = camera.move_speed;
        fast_multiplier = camera.fast_multiplier;
        slow_multiplier = camera.slow_multiplier;
        mouse_sensitivity = camera.mouse_sensitivity;
        pan_sensitivity = camera.pan_sensitivity;
        zoom_sensitivity = camera.zoom_sensitivity;
        field_of_view_degrees = camera.field_of_view_degrees;
        near_clip = camera.near_clip;
        far_clip = camera.far_clip;
        Sanitize();
    }

    const char* EditorCameraPresetStore::KeyName(EditorCameraKey key) noexcept
    {
        static constexpr const char* names[] =
        {
            "None", "W", "A", "S", "D", "Q", "E", "R", "F", "G", "C", "V", "X", "Z",
            "Space", "Left", "Right", "Up", "Down", "Home", "End", "PageUp", "PageDown",
            "Num0", "Num1", "Num2", "Num3", "Num4", "Num5", "Num6", "Num7", "Num8", "Num9"
        };
        const auto index = static_cast<std::size_t>(key);
        if (index >= static_cast<std::size_t>(EditorCameraKey::Count)) return "None";
        return names[index];
    }

    const char* EditorCameraPresetStore::MouseButtonName(
        EditorCameraMouseButton button) noexcept
    {
        switch (button)
        {
        case EditorCameraMouseButton::Left: return "LeftMouse";
        case EditorCameraMouseButton::Middle: return "MiddleMouse";
        case EditorCameraMouseButton::Right: return "RightMouse";
        case EditorCameraMouseButton::None:
        default: return "None";
        }
    }


    const char* EditorCameraPresetStore::ModifierName(EditorCameraModifier modifier) noexcept
    {
        switch (modifier)
        {
        case EditorCameraModifier::Shift: return "Shift";
        case EditorCameraModifier::Control: return "Control";
        case EditorCameraModifier::Alt: return "Alt";
        case EditorCameraModifier::None:
        default: return "None";
        }
    }

    bool EditorCameraPresetStore::ParseKey(const std::string& text,
        EditorCameraKey& key) noexcept
    {
        for (std::size_t i = 0; i < static_cast<std::size_t>(EditorCameraKey::Count); ++i)
        {
            const auto candidate = static_cast<EditorCameraKey>(i);
            if (text == KeyName(candidate))
            {
                key = candidate;
                return true;
            }
        }
        key = EditorCameraKey::None;
        return false;
    }

    bool EditorCameraPresetStore::ParseMouseButton(const std::string& text,
        EditorCameraMouseButton& button) noexcept
    {
        const EditorCameraMouseButton values[] =
        {
            EditorCameraMouseButton::None,
            EditorCameraMouseButton::Left,
            EditorCameraMouseButton::Middle,
            EditorCameraMouseButton::Right,
        };
        for (const auto value : values)
        {
            if (text == MouseButtonName(value))
            {
                button = value;
                return true;
            }
        }
        button = EditorCameraMouseButton::None;
        return false;
    }
    bool EditorCameraPresetStore::ParseModifier(const std::string& text,
        EditorCameraModifier& modifier) noexcept
    {
        const EditorCameraModifier values[] =
        {
            EditorCameraModifier::None, EditorCameraModifier::Shift,
            EditorCameraModifier::Control, EditorCameraModifier::Alt
        };
        for (const auto value : values)
        {
            if (text == ModifierName(value))
            {
                modifier = value;
                return true;
            }
        }
        modifier = EditorCameraModifier::None;
        return false;
    }

}
