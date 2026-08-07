#include "EditorCameraStateStore.h"

#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>

namespace ReplayEngine::Editor
{
    namespace
    {
        constexpr const char* magic_token = "REPLAY_SCENEVIEW_CAMERA";
        constexpr const char* preference_magic_token = "REPLAY_EDITOR_CAMERA_SETTINGS";
        constexpr int preference_version = 1;
    }

    EditorCameraStateStore::State EditorCameraStateStore::Capture(
        const EditorViewportCamera& camera)
    {
        State state;
        state.position = camera.Position();
        state.yaw = camera.Yaw();
        state.pitch = camera.Pitch();
        state.orbit_pivot = camera.OrbitPivot();
        state.orbit_distance = camera.OrbitDistance();
        state.move_speed = camera.move_speed;
        state.field_of_view_degrees = camera.field_of_view_degrees;
        return state;
    }

    void EditorCameraStateStore::Apply(const State& state, EditorViewportCamera& camera)
    {
        camera.SetPosition(state.position);
        camera.SetYawPitch(state.yaw, state.pitch);
        camera.SetOrbitPivot(state.orbit_pivot);
        camera.move_speed = state.move_speed;
        camera.field_of_view_degrees = state.field_of_view_degrees;

        // SetOrbitPivot は位置から距離を求め直すので、
        // 保存されていた距離とわずかにずれる場合がある。
        // 保存値を優先せず、位置と Pivot から導いた値を正とする
        // （位置が正しければ見た目は必ず一致する）。
    }

    std::filesystem::path EditorCameraStateStore::PathForKey(const std::string& key)
    {
        const std::string safe = key.empty() ? std::string("default") : key;
        return std::filesystem::path("Saved") / "Editor" / "SceneView" /
            (safe + file_extension);
    }

    std::string EditorCameraStateStore::KeyFromScenePath(
        const std::filesystem::path& scene_path)
    {
        // FNV-1a。暗号強度は不要で、同じパスから常に同じ値が出ればよい。
        const std::string text = scene_path.lexically_normal().generic_string();
        std::uint64_t hash = 1469598103934665603ull;
        for (const char character : text)
        {
            hash ^= static_cast<std::uint8_t>(character);
            hash *= 1099511628211ull;
        }

        std::ostringstream stream;
        stream.imbue(std::locale::classic());
        stream << "path_" << std::hex << hash;
        return stream.str();
    }

    bool EditorCameraStateStore::Save(const State& state,
        const std::filesystem::path& path, std::string& error)
    {
        std::error_code filesystem_error;
        if (!path.parent_path().empty())
        {
            std::filesystem::create_directories(path.parent_path(), filesystem_error);
            if (filesystem_error)
            {
                error = "編集カメラの保存先フォルダーを作成できません。";
                return false;
            }
        }

        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        if (!stream)
        {
            error = "編集カメラの状態を書き出せません。";
            return false;
        }
        stream.imbue(std::locale::classic());

        stream << magic_token << ' ' << current_version << '\n';
        stream << "POSITION " << state.position.x << ' ' << state.position.y << ' '
            << state.position.z << '\n';
        stream << "ROTATION " << state.yaw << ' ' << state.pitch << '\n';
        stream << "PIVOT " << state.orbit_pivot.x << ' ' << state.orbit_pivot.y << ' '
            << state.orbit_pivot.z << '\n';
        stream << "DISTANCE " << state.orbit_distance << '\n';
        stream << "MOVE_SPEED " << state.move_speed << '\n';
        stream << "FOV " << state.field_of_view_degrees << '\n';

        if (!stream)
        {
            error = "編集カメラの状態の書き込みに失敗しました。";
            return false;
        }
        return true;
    }

    bool EditorCameraStateStore::Load(State& state, const std::filesystem::path& path,
        std::string& error)
    {
        state = State{};

        std::error_code filesystem_error;
        if (!std::filesystem::exists(path, filesystem_error) || filesystem_error)
        {
            // まだ保存されていない Scene。既定位置を使えばよい。
            error = "保存された編集カメラの状態がありません。";
            return false;
        }

        std::ifstream stream(path, std::ios::binary);
        if (!stream)
        {
            error = "編集カメラの状態を開けません。";
            return false;
        }
        stream.imbue(std::locale::classic());

        std::string magic;
        int version = 0;
        if (!(stream >> magic >> version) || magic != magic_token ||
            version <= 0 || version > current_version)
        {
            // 壊れている・別形式。既定値のまま false を返す。
            // 【重要】ここで失敗しても Scene 本体の読み込みは止めない。
            error = "編集カメラの状態が読めません（既定位置を使います）。";
            state = State{};
            return false;
        }

        // キーワード方式。未知の項目は読み飛ばすので、
        // 途中が壊れていても読める範囲だけ復元して続行する。
        std::string keyword;
        while (stream >> keyword)
        {
            if (keyword == "POSITION")
            {
                stream >> state.position.x >> state.position.y >> state.position.z;
            }
            else if (keyword == "ROTATION")
            {
                stream >> state.yaw >> state.pitch;
            }
            else if (keyword == "PIVOT")
            {
                stream >> state.orbit_pivot.x >> state.orbit_pivot.y >> state.orbit_pivot.z;
            }
            else if (keyword == "DISTANCE")
            {
                stream >> state.orbit_distance;
            }
            else if (keyword == "MOVE_SPEED")
            {
                stream >> state.move_speed;
            }
            else if (keyword == "FOV")
            {
                stream >> state.field_of_view_degrees;
            }
            else
            {
                // 未知のキーワード。その行を捨てる。
                std::string ignored;
                std::getline(stream, ignored);
            }

            if (stream.fail())
            {
                // 数値が壊れていた。読めたところまでで打ち切り、
                // 異常値だけ弾いて既定へ寄せる。
                stream.clear();
                break;
            }
        }

        // 壊れた値でカメラが飛ばないよう最低限の健全化。
        if (!(state.orbit_distance > 0.0f)) state.orbit_distance = 8.0f;
        if (!(state.move_speed > 0.0f)) state.move_speed = 5.0f;
        if (!(state.field_of_view_degrees > 1.0f) ||
            state.field_of_view_degrees > 179.0f)
        {
            state.field_of_view_degrees = 60.0f;
        }
        return true;
    }

    std::filesystem::path EditorCameraStateStore::MoveSpeedPreferencePath()
    {
        return std::filesystem::path("Saved") / "Editor" /
            "CameraSettings.replaycamsettings";
    }

    bool EditorCameraStateStore::SaveMoveSpeedPreference(float move_speed,
        std::string& error)
    {
        // 500 / 1000 などの恣意的な上限は置かない。
        // float の範囲外と、移動不能になる 0 以下だけは保存しない。
        if (!(move_speed > 0.0f) || !std::isfinite(move_speed))
        {
            error = "編集カメラ移動速度が不正です。";
            return false;
        }

        const std::filesystem::path path = MoveSpeedPreferencePath();
        std::error_code filesystem_error;
        if (!path.parent_path().empty())
        {
            std::filesystem::create_directories(path.parent_path(), filesystem_error);
            if (filesystem_error)
            {
                error = "編集カメラ設定の保存先フォルダーを作成できません。";
                return false;
            }
        }

        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        if (!stream)
        {
            error = "編集カメラ設定を書き出せません。";
            return false;
        }
        stream.imbue(std::locale::classic());
        stream << std::setprecision(std::numeric_limits<float>::max_digits10);
        stream << preference_magic_token << ' ' << preference_version << '\n';
        stream << "MOVE_SPEED " << move_speed << '\n';
        if (!stream)
        {
            error = "編集カメラ設定の書き込みに失敗しました。";
            return false;
        }
        return true;
    }

    bool EditorCameraStateStore::LoadMoveSpeedPreference(float& move_speed,
        std::string& error)
    {
        const std::filesystem::path path = MoveSpeedPreferencePath();
        std::error_code filesystem_error;
        if (!std::filesystem::exists(path, filesystem_error) || filesystem_error)
        {
            error = "保存された編集カメラ移動速度がありません。";
            return false;
        }

        std::ifstream stream(path, std::ios::binary);
        if (!stream)
        {
            error = "編集カメラ設定を開けません。";
            return false;
        }
        stream.imbue(std::locale::classic());

        std::string magic;
        int version = 0;
        if (!(stream >> magic >> version) || magic != preference_magic_token ||
            version <= 0 || version > preference_version)
        {
            error = "編集カメラ設定の形式が不正です。";
            return false;
        }

        std::string keyword;
        float loaded_speed = move_speed;
        bool found = false;
        while (stream >> keyword)
        {
            if (keyword == "MOVE_SPEED")
            {
                stream >> loaded_speed;
                found = !stream.fail();
            }
            else
            {
                std::string ignored;
                std::getline(stream, ignored);
            }
            if (stream.fail()) break;
        }

        if (!found || !(loaded_speed > 0.0f) || !std::isfinite(loaded_speed))
        {
            error = "保存された編集カメラ移動速度が不正です。";
            return false;
        }
        move_speed = loaded_speed;
        return true;
    }

}
