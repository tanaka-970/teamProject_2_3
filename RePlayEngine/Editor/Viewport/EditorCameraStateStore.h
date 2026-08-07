#pragma once

#include "EditorViewportCamera.h"

#include <filesystem>
#include <string>

namespace ReplayEngine::Editor
{
    // 編集カメラの状態を Scene ごとに保存・復元する。
    //
    // 【Scene v9 へ混ぜない理由】
    //   編集カメラはゲームのデータではない。Scene ファイルへ入れると、
    //     - Runtime が読む必要のない値がゲームデータへ混ざる
    //     - Prefab や Scene の差分に編集中の視点移動が乗る
    //     - 保存形式を上げる理由が「エディタの都合」になる
    //   ため、Scene ファイルとは完全に別のファイルへ置く。
    //   この機能のために Scene を v10 へ上げることはしない。
    //
    // 【保存先】
    //   Saved/Editor/SceneView/<key>.replaycam
    //
    //   key には Scene の AssetGUID を使うのが望ましい。AssetDatabase へ
    //   登録されていない Scene もあるため、登録が無い場合は Scene ファイルの
    //   パスから作った安定なハッシュを使う。どちらもファイル名の変更で
    //   直接壊れることはない（GUID の場合）か、既定位置へ戻るだけ（ハッシュの場合）。
    //
    // 【壊れていても Scene 読み込みを止めない】
    //   Load は失敗しても false を返すだけ。呼び出し側は既定位置を使えばよい。
    //   ここで例外を投げたり assert したりはしない。
    class EditorCameraStateStore final
    {
    public:
        EditorCameraStateStore() = delete;

        static constexpr const char* file_extension = ".replaycam";
        static constexpr int current_version = 1;

        // 保存されるのは次だけ。設定値のうち移動速度と視野角も含める。
        struct State
        {
            DirectX::XMFLOAT3 position{ 0.0f, 3.0f, -8.0f };
            float yaw = 0.0f;
            float pitch = 0.0f;
            DirectX::XMFLOAT3 orbit_pivot{ 0.0f, 1.0f, 0.0f };
            float orbit_distance = 8.0f;
            float move_speed = 5.0f;
            float field_of_view_degrees = 60.0f;
        };

        static State Capture(const EditorViewportCamera& camera);
        static void Apply(const State& state, EditorViewportCamera& camera);

        // Scene を一意に指すキーからファイルパスを作る。
        static std::filesystem::path PathForKey(const std::string& key);

        // Scene の AssetGUID が無い場合に使う、パス由来の安定キー。
        static std::string KeyFromScenePath(const std::filesystem::path& scene_path);

        static bool Save(const State& state, const std::filesystem::path& path,
            std::string& error);

        // 読めなければ false。state は既定値のまま。
        static bool Load(State& state, const std::filesystem::path& path,
            std::string& error);

        // Editor 全体で共有するデバッグカメラ移動速度。
        // Scene ごとの視点とは別ファイルに保存し、Scene 切替後も同じ速度を使う。
        // UI 上の上限は設けない。0 以下 / NaN / Inf だけを拒否する。
        // 保存先: Saved/Editor/CameraSettings.replaycamsettings
        static std::filesystem::path MoveSpeedPreferencePath();
        static bool SaveMoveSpeedPreference(float move_speed, std::string& error);
        static bool LoadMoveSpeedPreference(float& move_speed, std::string& error);
    };
}
