#pragma once

#include "EditorViewportCamera.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace ReplayEngine::Editor
{
    enum class EditorCameraKey : std::uint8_t
    {
        None = 0,
        W, A, S, D, Q, E, R, F, G, C, V, X, Z,
        Space,
        Left, Right, Up, Down,
        Home, End, PageUp, PageDown,
        Num0, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,
        Count
    };

    enum class EditorCameraModifier : std::uint8_t
    {
        None = 0,
        Shift,
        Control,
        Alt,
    };

    enum class EditorCameraMouseButton : std::uint8_t
    {
        None = 0,
        Left,
        Middle,
        Right,
    };

    struct EditorCameraKeyChord
    {
        EditorCameraKey key = EditorCameraKey::None;
        bool shift = false;
        bool control = false;
        bool alt = false;

        bool Empty() const noexcept { return key == EditorCameraKey::None; }
    };

    struct EditorCameraMouseGesture
    {
        EditorCameraMouseButton button = EditorCameraMouseButton::None;
        bool shift = false;
        bool control = false;
        bool alt = false;

        bool Empty() const noexcept { return button == EditorCameraMouseButton::None; }
    };

    enum class EditorCameraPresetScope : std::uint8_t
    {
        Personal = 0,
        Shared,
    };

    // 1 人 1 人の「カメラ操作そのもの」を保存するデータ。
    // Scene / GameObject / Runtime Camera には属さない。
    struct EditorCameraPreset
    {
        static constexpr int current_version = 1;

        std::string id;
        std::string name = "My Camera";
        EditorCameraPresetScope scope = EditorCameraPresetScope::Personal;
        std::filesystem::path source_path;

        // ---- Camera feel ---------------------------------------------------
        float move_speed = 5.0f;                 // 上限なし
        float fast_multiplier = 4.0f;
        float slow_multiplier = 0.25f;
        float mouse_sensitivity = 0.15f;
        float pan_sensitivity = 1.0f;
        float zoom_sensitivity = 1.0f;
        float keyboard_rotation_degrees = 90.0f; // 1 秒あたり
        float field_of_view_degrees = 60.0f;
        float near_clip = 0.05f;
        float far_clip = 10000.0f;

        bool keyboard_fly_without_look = true;
        bool world_vertical_move = true;
        bool wheel_changes_speed_while_look = true;
        EditorCameraModifier fast_modifier = EditorCameraModifier::Shift;
        EditorCameraModifier slow_modifier = EditorCameraModifier::Control;
        bool invert_look_x = false;
        bool invert_look_y = false;
        bool invert_orbit_x = false;
        bool invert_orbit_y = false;
        bool invert_pan_x = false;
        bool invert_pan_y = false;
        bool invert_dolly = false;

        // ---- Navigation bindings ------------------------------------------
        EditorCameraKeyChord move_forward{ EditorCameraKey::W };
        EditorCameraKeyChord move_back{ EditorCameraKey::S };
        EditorCameraKeyChord move_left{ EditorCameraKey::A };
        EditorCameraKeyChord move_right{ EditorCameraKey::D };
        EditorCameraKeyChord move_up{ EditorCameraKey::E };
        EditorCameraKeyChord move_down{ EditorCameraKey::Q };

        EditorCameraMouseGesture look{ EditorCameraMouseButton::Right };
        EditorCameraMouseGesture orbit{ EditorCameraMouseButton::Left, false, false, true };
        EditorCameraMouseGesture pan{ EditorCameraMouseButton::Middle };
        EditorCameraMouseGesture dolly{ EditorCameraMouseButton::Right, false, false, true };

        EditorCameraKeyChord focus{ EditorCameraKey::F };

        // キーボードでも視点回転できる。回転もプリセットで自由に差し替えられる。
        EditorCameraKeyChord rotate_yaw_left{ EditorCameraKey::Left };
        EditorCameraKeyChord rotate_yaw_right{ EditorCameraKey::Right };
        EditorCameraKeyChord rotate_pitch_up{ EditorCameraKey::Up };
        EditorCameraKeyChord rotate_pitch_down{ EditorCameraKey::Down };
        EditorCameraKeyChord rotate_roll_left{};
        EditorCameraKeyChord rotate_roll_right{};

        // DCC で便利な固定方向への View Snap。Perspective のまま向きだけ揃える。
        EditorCameraKeyChord view_front{ EditorCameraKey::Num1 };
        EditorCameraKeyChord view_back{ EditorCameraKey::Num1, false, true, false };
        EditorCameraKeyChord view_right{ EditorCameraKey::Num3 };
        EditorCameraKeyChord view_left{ EditorCameraKey::Num3, false, true, false };
        EditorCameraKeyChord view_top{ EditorCameraKey::Num7 };
        EditorCameraKeyChord view_bottom{ EditorCameraKey::Num7, false, true, false };

        // Gizmo 切替も同じプリセットに含める。
        EditorCameraKeyChord gizmo_move{ EditorCameraKey::W, true, false, false };
        EditorCameraKeyChord gizmo_rotate{ EditorCameraKey::E, true, false, false };
        EditorCameraKeyChord gizmo_scale{ EditorCameraKey::R, true, false, false };

        // Shared はチーム資産として読む。誤って他人のプリセットを上書きしない。
        bool Editable() const noexcept { return scope == EditorCameraPresetScope::Personal; }

        void Sanitize() noexcept;
        void ApplyCameraSettings(EditorViewportCamera& camera) const noexcept;
        void CaptureCameraSettings(const EditorViewportCamera& camera) noexcept;
    };

    class EditorCameraPresetStore final
    {
    public:
        EditorCameraPresetStore() = delete;

        static std::filesystem::path SharedDirectory();
        static std::filesystem::path PersonalDirectory();
        static std::filesystem::path UserSelectionPath();

        // Shared + Personal をまとめて列挙。id が衝突した場合は Personal を優先。
        static std::vector<EditorCameraPreset> LoadAll(std::string& error);

        static bool Save(EditorCameraPreset& preset, std::string& error);
        static bool DeletePersonal(const EditorCameraPreset& preset, std::string& error);

        // Shared を含む任意の preset から Personal copy を作る。
        static EditorCameraPreset DuplicateAsPersonal(const EditorCameraPreset& source,
            const std::string& preferred_name);
        static bool PublishSharedCopy(const EditorCameraPreset& source,
            const std::string& preferred_name, EditorCameraPreset& published,
            std::string& error);

        static bool SaveActivePresetId(const std::string& id, std::string& error);
        static bool LoadActivePresetId(std::string& id, std::string& error);

        static std::string MakeUniqueId();

        static const char* KeyName(EditorCameraKey key) noexcept;
        static const char* MouseButtonName(EditorCameraMouseButton button) noexcept;
        static const char* ModifierName(EditorCameraModifier modifier) noexcept;
        static bool ParseKey(const std::string& text, EditorCameraKey& key) noexcept;
        static bool ParseMouseButton(const std::string& text,
            EditorCameraMouseButton& button) noexcept;
        static bool ParseModifier(const std::string& text, EditorCameraModifier& modifier) noexcept;
    };
}
