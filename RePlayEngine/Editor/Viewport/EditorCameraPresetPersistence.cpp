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
    namespace
    {
        constexpr const char* magic = "REPLAY_CAMERA_PRESET";
        constexpr const char* user_magic = "REPLAY_CAMERA_USER";
        constexpr int user_version = 1;

        std::string ScopeName(EditorCameraPresetScope scope)
        {
            return scope == EditorCameraPresetScope::Shared ? "SHARED" : "PERSONAL";
        }

        EditorCameraPresetScope ParseScope(const std::string& text)
        {
            return text == "SHARED" ? EditorCameraPresetScope::Shared
                : EditorCameraPresetScope::Personal;
        }

        void WriteKeyChord(std::ostream& stream, const char* name,
            const EditorCameraKeyChord& chord)
        {
            stream << name << ' ' << EditorCameraPresetStore::KeyName(chord.key) << ' '
                << (chord.shift ? 1 : 0) << ' '
                << (chord.control ? 1 : 0) << ' '
                << (chord.alt ? 1 : 0) << '\n';
        }

        void WriteMouseGesture(std::ostream& stream, const char* name,
            const EditorCameraMouseGesture& gesture)
        {
            stream << name << ' '
                << EditorCameraPresetStore::MouseButtonName(gesture.button) << ' '
                << (gesture.shift ? 1 : 0) << ' '
                << (gesture.control ? 1 : 0) << ' '
                << (gesture.alt ? 1 : 0) << '\n';
        }

        bool ReadKeyChord(std::istringstream& line, EditorCameraKeyChord& chord)
        {
            std::string key_text;
            int shift = 0, control = 0, alt = 0;
            if (!(line >> key_text >> shift >> control >> alt)) return false;
            if (!EditorCameraPresetStore::ParseKey(key_text, chord.key)) return false;
            chord.shift = shift != 0;
            chord.control = control != 0;
            chord.alt = alt != 0;
            return true;
        }

        bool ReadMouseGesture(std::istringstream& line, EditorCameraMouseGesture& gesture)
        {
            std::string button_text;
            int shift = 0, control = 0, alt = 0;
            if (!(line >> button_text >> shift >> control >> alt)) return false;
            if (!EditorCameraPresetStore::ParseMouseButton(button_text, gesture.button))
                return false;
            gesture.shift = shift != 0;
            gesture.control = control != 0;
            gesture.alt = alt != 0;
            return true;
        }

        bool SavePresetFile(const EditorCameraPreset& preset,
            const std::filesystem::path& path, std::string& error)
        {
            std::error_code ec;
            std::filesystem::create_directories(path.parent_path(), ec);
            if (ec)
            {
                error = "カメラプリセットの保存フォルダーを作れません。";
                return false;
            }

            std::ofstream stream(path, std::ios::binary | std::ios::trunc);
            if (!stream)
            {
                error = "カメラプリセットを書き出せません。";
                return false;
            }
            stream.imbue(std::locale::classic());
            stream << std::setprecision(std::numeric_limits<float>::max_digits10);
            stream << magic << ' ' << EditorCameraPreset::current_version << '\n';
            stream << "ID " << std::quoted(preset.id) << '\n';
            stream << "NAME " << std::quoted(preset.name) << '\n';
            stream << "SCOPE " << ScopeName(preset.scope) << '\n';
            stream << "MOVE_SPEED " << preset.move_speed << '\n';
            stream << "FAST_MULTIPLIER " << preset.fast_multiplier << '\n';
            stream << "SLOW_MULTIPLIER " << preset.slow_multiplier << '\n';
            stream << "MOUSE_SENSITIVITY " << preset.mouse_sensitivity << '\n';
            stream << "PAN_SENSITIVITY " << preset.pan_sensitivity << '\n';
            stream << "ZOOM_SENSITIVITY " << preset.zoom_sensitivity << '\n';
            stream << "KEYBOARD_ROTATION_DEGREES " << preset.keyboard_rotation_degrees << '\n';
            stream << "FOV " << preset.field_of_view_degrees << '\n';
            stream << "NEAR_CLIP " << preset.near_clip << '\n';
            stream << "FAR_CLIP " << preset.far_clip << '\n';
            stream << "KEYBOARD_FLY_WITHOUT_LOOK " << (preset.keyboard_fly_without_look ? 1 : 0) << '\n';
            stream << "WORLD_VERTICAL_MOVE " << (preset.world_vertical_move ? 1 : 0) << '\n';
            stream << "WHEEL_SPEED_WHILE_LOOK " << (preset.wheel_changes_speed_while_look ? 1 : 0) << '\n';
            stream << "FAST_MODIFIER " << EditorCameraPresetStore::ModifierName(preset.fast_modifier) << '\n';
            stream << "SLOW_MODIFIER " << EditorCameraPresetStore::ModifierName(preset.slow_modifier) << '\n';
            stream << "INVERT_LOOK_X " << (preset.invert_look_x ? 1 : 0) << '\n';
            stream << "INVERT_LOOK_Y " << (preset.invert_look_y ? 1 : 0) << '\n';
            stream << "INVERT_ORBIT_X " << (preset.invert_orbit_x ? 1 : 0) << '\n';
            stream << "INVERT_ORBIT_Y " << (preset.invert_orbit_y ? 1 : 0) << '\n';
            stream << "INVERT_PAN_X " << (preset.invert_pan_x ? 1 : 0) << '\n';
            stream << "INVERT_PAN_Y " << (preset.invert_pan_y ? 1 : 0) << '\n';
            stream << "INVERT_DOLLY " << (preset.invert_dolly ? 1 : 0) << '\n';

            WriteKeyChord(stream, "MOVE_FORWARD", preset.move_forward);
            WriteKeyChord(stream, "MOVE_BACK", preset.move_back);
            WriteKeyChord(stream, "MOVE_LEFT", preset.move_left);
            WriteKeyChord(stream, "MOVE_RIGHT", preset.move_right);
            WriteKeyChord(stream, "MOVE_UP", preset.move_up);
            WriteKeyChord(stream, "MOVE_DOWN", preset.move_down);
            WriteMouseGesture(stream, "LOOK", preset.look);
            WriteMouseGesture(stream, "ORBIT", preset.orbit);
            WriteMouseGesture(stream, "PAN", preset.pan);
            WriteMouseGesture(stream, "DOLLY", preset.dolly);
            WriteKeyChord(stream, "FOCUS", preset.focus);
            WriteKeyChord(stream, "ROTATE_YAW_LEFT", preset.rotate_yaw_left);
            WriteKeyChord(stream, "ROTATE_YAW_RIGHT", preset.rotate_yaw_right);
            WriteKeyChord(stream, "ROTATE_PITCH_UP", preset.rotate_pitch_up);
            WriteKeyChord(stream, "ROTATE_PITCH_DOWN", preset.rotate_pitch_down);
            WriteKeyChord(stream, "ROTATE_ROLL_LEFT", preset.rotate_roll_left);
            WriteKeyChord(stream, "ROTATE_ROLL_RIGHT", preset.rotate_roll_right);
            WriteKeyChord(stream, "VIEW_FRONT", preset.view_front);
            WriteKeyChord(stream, "VIEW_BACK", preset.view_back);
            WriteKeyChord(stream, "VIEW_RIGHT", preset.view_right);
            WriteKeyChord(stream, "VIEW_LEFT", preset.view_left);
            WriteKeyChord(stream, "VIEW_TOP", preset.view_top);
            WriteKeyChord(stream, "VIEW_BOTTOM", preset.view_bottom);
            WriteKeyChord(stream, "GIZMO_MOVE", preset.gizmo_move);
            WriteKeyChord(stream, "GIZMO_ROTATE", preset.gizmo_rotate);
            WriteKeyChord(stream, "GIZMO_SCALE", preset.gizmo_scale);

            if (!stream)
            {
                error = "カメラプリセットの書き込みに失敗しました。";
                return false;
            }
            return true;
        }

        bool LoadPresetFile(const std::filesystem::path& path,
            EditorCameraPresetScope forced_scope, EditorCameraPreset& preset,
            std::string& error)
        {
            std::ifstream stream(path, std::ios::binary);
            if (!stream) return false;
            stream.imbue(std::locale::classic());

            std::string header;
            int version = 0;
            if (!(stream >> header >> version) || header != magic || version <= 0 ||
                version > EditorCameraPreset::current_version)
            {
                error = "カメラプリセット形式が不正です: " + path.generic_string();
                return false;
            }
            std::string ignored_line;
            std::getline(stream, ignored_line);

            preset = EditorCameraPreset{};
            preset.scope = forced_scope;
            preset.source_path = path;

            std::string line_text;
            while (std::getline(stream, line_text))
            {
                if (line_text.empty()) continue;
                std::istringstream line(line_text);
                line.imbue(std::locale::classic());
                std::string keyword;
                line >> keyword;

                if (keyword == "ID") line >> std::quoted(preset.id);
                else if (keyword == "NAME") line >> std::quoted(preset.name);
                else if (keyword == "SCOPE")
                {
                    std::string source_scope;
                    line >> source_scope;
                    (void)ParseScope(source_scope); // path 側の scope を正とする。
                }
                else if (keyword == "MOVE_SPEED") line >> preset.move_speed;
                else if (keyword == "FAST_MULTIPLIER") line >> preset.fast_multiplier;
                else if (keyword == "SLOW_MULTIPLIER") line >> preset.slow_multiplier;
                else if (keyword == "MOUSE_SENSITIVITY") line >> preset.mouse_sensitivity;
                else if (keyword == "PAN_SENSITIVITY") line >> preset.pan_sensitivity;
                else if (keyword == "ZOOM_SENSITIVITY") line >> preset.zoom_sensitivity;
                else if (keyword == "KEYBOARD_ROTATION_DEGREES") line >> preset.keyboard_rotation_degrees;
                else if (keyword == "FOV") line >> preset.field_of_view_degrees;
                else if (keyword == "NEAR_CLIP") line >> preset.near_clip;
                else if (keyword == "FAR_CLIP") line >> preset.far_clip;
                else if (keyword == "KEYBOARD_FLY_WITHOUT_LOOK") { int v = 0; line >> v; preset.keyboard_fly_without_look = v != 0; }
                else if (keyword == "WORLD_VERTICAL_MOVE") { int v = 0; line >> v; preset.world_vertical_move = v != 0; }
                else if (keyword == "WHEEL_SPEED_WHILE_LOOK") { int v = 0; line >> v; preset.wheel_changes_speed_while_look = v != 0; }
                else if (keyword == "FAST_MODIFIER") { std::string v; line >> v; EditorCameraPresetStore::ParseModifier(v, preset.fast_modifier); }
                else if (keyword == "SLOW_MODIFIER") { std::string v; line >> v; EditorCameraPresetStore::ParseModifier(v, preset.slow_modifier); }
                else if (keyword == "INVERT_LOOK_X") { int v = 0; line >> v; preset.invert_look_x = v != 0; }
                else if (keyword == "INVERT_LOOK_Y") { int v = 0; line >> v; preset.invert_look_y = v != 0; }
                else if (keyword == "INVERT_ORBIT_X") { int v = 0; line >> v; preset.invert_orbit_x = v != 0; }
                else if (keyword == "INVERT_ORBIT_Y") { int v = 0; line >> v; preset.invert_orbit_y = v != 0; }
                else if (keyword == "INVERT_PAN_X") { int v = 0; line >> v; preset.invert_pan_x = v != 0; }
                else if (keyword == "INVERT_PAN_Y") { int v = 0; line >> v; preset.invert_pan_y = v != 0; }
                else if (keyword == "INVERT_DOLLY") { int v = 0; line >> v; preset.invert_dolly = v != 0; }
                else if (keyword == "MOVE_FORWARD") ReadKeyChord(line, preset.move_forward);
                else if (keyword == "MOVE_BACK") ReadKeyChord(line, preset.move_back);
                else if (keyword == "MOVE_LEFT") ReadKeyChord(line, preset.move_left);
                else if (keyword == "MOVE_RIGHT") ReadKeyChord(line, preset.move_right);
                else if (keyword == "MOVE_UP") ReadKeyChord(line, preset.move_up);
                else if (keyword == "MOVE_DOWN") ReadKeyChord(line, preset.move_down);
                else if (keyword == "LOOK") ReadMouseGesture(line, preset.look);
                else if (keyword == "ORBIT") ReadMouseGesture(line, preset.orbit);
                else if (keyword == "PAN") ReadMouseGesture(line, preset.pan);
                else if (keyword == "DOLLY") ReadMouseGesture(line, preset.dolly);
                else if (keyword == "FOCUS") ReadKeyChord(line, preset.focus);
                else if (keyword == "ROTATE_YAW_LEFT") ReadKeyChord(line, preset.rotate_yaw_left);
                else if (keyword == "ROTATE_YAW_RIGHT") ReadKeyChord(line, preset.rotate_yaw_right);
                else if (keyword == "ROTATE_PITCH_UP") ReadKeyChord(line, preset.rotate_pitch_up);
                else if (keyword == "ROTATE_PITCH_DOWN") ReadKeyChord(line, preset.rotate_pitch_down);
                else if (keyword == "ROTATE_ROLL_LEFT") ReadKeyChord(line, preset.rotate_roll_left);
                else if (keyword == "ROTATE_ROLL_RIGHT") ReadKeyChord(line, preset.rotate_roll_right);
                else if (keyword == "VIEW_FRONT") ReadKeyChord(line, preset.view_front);
                else if (keyword == "VIEW_BACK") ReadKeyChord(line, preset.view_back);
                else if (keyword == "VIEW_RIGHT") ReadKeyChord(line, preset.view_right);
                else if (keyword == "VIEW_LEFT") ReadKeyChord(line, preset.view_left);
                else if (keyword == "VIEW_TOP") ReadKeyChord(line, preset.view_top);
                else if (keyword == "VIEW_BOTTOM") ReadKeyChord(line, preset.view_bottom);
                else if (keyword == "GIZMO_MOVE") ReadKeyChord(line, preset.gizmo_move);
                else if (keyword == "GIZMO_ROTATE") ReadKeyChord(line, preset.gizmo_rotate);
                else if (keyword == "GIZMO_SCALE") ReadKeyChord(line, preset.gizmo_scale);
            }

            if (preset.id.empty()) preset.id = path.stem().string();
            if (preset.name.empty()) preset.name = preset.id;
            preset.Sanitize();
            return true;
        }

        std::filesystem::path MakePersonalPath(const EditorCameraPreset& preset)
        {
            // ファイル名は ID 固定。表示名を変えても active preset が壊れない。
            return EditorCameraPresetStore::PersonalDirectory() /
                (preset.id + ".replaycamerapreset");
        }
    }

    std::filesystem::path EditorCameraPresetStore::SharedDirectory()
    {
        return std::filesystem::path("Editor") / "CameraPresets";
    }

    std::filesystem::path EditorCameraPresetStore::PersonalDirectory()
    {
        return std::filesystem::path("Saved") / "Editor" / "CameraPresets";
    }

    std::filesystem::path EditorCameraPresetStore::UserSelectionPath()
    {
        return std::filesystem::path("Saved") / "Editor" /
            "CameraUserSettings.replaycamerauser";
    }

    std::vector<EditorCameraPreset> EditorCameraPresetStore::LoadAll(std::string& error)
    {
        std::map<std::string, EditorCameraPreset> by_id;
        auto load_directory = [&](const std::filesystem::path& directory,
            EditorCameraPresetScope scope)
        {
            std::error_code ec;
            if (!std::filesystem::exists(directory, ec) || ec) return;
            for (const auto& entry : std::filesystem::directory_iterator(directory, ec))
            {
                if (ec) break;
                if (!entry.is_regular_file()) continue;
                if (entry.path().extension() != ".replaycamerapreset") continue;
                EditorCameraPreset preset;
                std::string local_error;
                if (LoadPresetFile(entry.path(), scope, preset, local_error))
                    by_id[preset.id] = preset;
                else if (error.empty())
                    error = local_error;
            }
        };

        load_directory(SharedDirectory(), EditorCameraPresetScope::Shared);
        // Personal を後に読み、同じ ID があれば user 側を優先。
        load_directory(PersonalDirectory(), EditorCameraPresetScope::Personal);

        std::vector<EditorCameraPreset> result;
        result.reserve(by_id.size());
        for (auto& pair : by_id) result.push_back(std::move(pair.second));
        std::sort(result.begin(), result.end(), [](const EditorCameraPreset& a,
            const EditorCameraPreset& b)
        {
            if (a.scope != b.scope)
                return a.scope == EditorCameraPresetScope::Personal;
            return a.name < b.name;
        });
        return result;
    }

    bool EditorCameraPresetStore::Save(EditorCameraPreset& preset, std::string& error)
    {
        if (!preset.Editable())
        {
            error = "共有カメラプリセットは直接変更しません。複製して自分用に編集してください。";
            return false;
        }
        if (preset.id.empty()) preset.id = MakeUniqueId();
        const std::filesystem::path save_path = MakePersonalPath(preset);
        if (preset.name.empty())
        {
            // UI と同じく、既存 preset の空名は採用しない。
            // 以前はここで無条件に "My Camera" へ変えていたため、
            // データ層を直接使うと保存済みの表示名を失っていた。
            EditorCameraPreset existing;
            std::string load_error;
            std::error_code exists_error;
            if (std::filesystem::exists(save_path, exists_error) && !exists_error &&
                LoadPresetFile(save_path, EditorCameraPresetScope::Personal,
                    existing, load_error) && !existing.name.empty())
            {
                preset.name = existing.name;
            }
            else
            {
                preset.name = "My Camera";
            }
        }
        preset.Sanitize();
        preset.source_path = save_path;
        return SavePresetFile(preset, preset.source_path, error);
    }

    bool EditorCameraPresetStore::DeletePersonal(const EditorCameraPreset& preset,
        std::string& error)
    {
        if (!preset.Editable())
        {
            error = "共有カメラプリセットは削除できません。";
            return false;
        }
        std::error_code ec;
        const auto path = preset.source_path.empty() ? MakePersonalPath(preset)
            : preset.source_path;
        if (!std::filesystem::exists(path, ec)) return true;
        if (!std::filesystem::remove(path, ec) || ec)
        {
            error = "個人カメラプリセットを削除できません。";
            return false;
        }
        return true;
    }

    EditorCameraPreset EditorCameraPresetStore::DuplicateAsPersonal(
        const EditorCameraPreset& source, const std::string& preferred_name)
    {
        EditorCameraPreset copy = source;
        copy.id = MakeUniqueId();
        copy.name = preferred_name.empty() ? (source.name + " Copy") : preferred_name;
        copy.scope = EditorCameraPresetScope::Personal;
        copy.source_path.clear();
        return copy;
    }

    bool EditorCameraPresetStore::PublishSharedCopy(const EditorCameraPreset& source,
        const std::string& preferred_name, EditorCameraPreset& published,
        std::string& error)
    {
        published = source;
        published.id = MakeUniqueId();
        published.name = preferred_name.empty() ? (source.name + " Shared") : preferred_name;
        published.scope = EditorCameraPresetScope::Shared;
        published.source_path = SharedDirectory() /
            (published.id + ".replaycamerapreset");
        published.Sanitize();
        return SavePresetFile(published, published.source_path, error);
    }

    bool EditorCameraPresetStore::SaveActivePresetId(const std::string& id,
        std::string& error)
    {
        const auto path = UserSelectionPath();
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec)
        {
            error = "カメラのユーザー設定フォルダーを作れません。";
            return false;
        }
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        if (!stream)
        {
            error = "カメラのユーザー設定を書き出せません。";
            return false;
        }
        stream << user_magic << ' ' << user_version << '\n';
        stream << "ACTIVE " << std::quoted(id) << '\n';
        return static_cast<bool>(stream);
    }

    bool EditorCameraPresetStore::LoadActivePresetId(std::string& id,
        std::string& error)
    {
        std::ifstream stream(UserSelectionPath(), std::ios::binary);
        if (!stream)
        {
            error = "カメラのユーザー設定がありません。";
            return false;
        }
        std::string magic_text;
        int version = 0;
        if (!(stream >> magic_text >> version) || magic_text != user_magic ||
            version <= 0 || version > user_version)
        {
            error = "カメラのユーザー設定形式が不正です。";
            return false;
        }
        std::string keyword;
        while (stream >> keyword)
        {
            if (keyword == "ACTIVE")
            {
                stream >> std::quoted(id);
                return !id.empty();
            }
            std::string ignored;
            std::getline(stream, ignored);
        }
        error = "使用中カメラプリセットが記録されていません。";
        return false;
    }

    std::string EditorCameraPresetStore::MakeUniqueId()
    {
        const auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        std::random_device random_device;
        const std::uint64_t random_value =
            (static_cast<std::uint64_t>(random_device()) << 32) ^ random_device();
        std::ostringstream stream;
        stream << std::hex << static_cast<std::uint64_t>(now) << random_value;
        return stream.str();
    }
}
