// GameObject / Component 基盤と既存 framework の接続部。
//
// この 1 ファイルへ新基盤との橋渡しをまとめている理由:
//   framework 側の既存ファイル（描画・入力・エディタ）への変更を最小限に抑え、
//   どこが新基盤との境界なのかを一目で分かるようにするため。
//
// 依存方向:
//   framework  ->  RePlayEngine (Scene / GameObject / Component / Editor)  の一方向。
//   RePlayEngine 側から framework を参照している箇所は無い。

#include "framework.h"

#include "gltf_model.h"
#include "skinned_mesh.h"

#include "../../RePlayEngine/Components/Camera/CameraTargetComponent.h"
#include "../../RePlayEngine/Components/Rendering/LightComponents.h"
#include "../../RePlayEngine/Object/Registry/BuiltInComponents.h"
#include "../../RePlayEngine/Project/ProjectSettingsSerializer.h"
#include "../../RePlayEngine/Rendering/Adapter/SceneRenderCollector.h"
#include "../../RePlayEngine/Scene/Serialization/PrefabSerializer.h"
#include "../../RePlayEngine/Scene/Serialization/SceneData.h"
#include "../../RePlayEngine/Scene/Serialization/SceneSerializer.h"

#include <commdlg.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <string>

namespace
{
    namespace SceneSerialization = ReplayEngine::Scene::Serialization;

    std::filesystem::path BrowseObjectSceneFile(HWND owner, bool save,
        const std::filesystem::path& current)
    {
        wchar_t filename[32768]{};
        if (!current.empty())
        {
            const std::wstring initial = std::filesystem::absolute(current).wstring();
            wcsncpy_s(filename, initial.c_str(), _TRUNCATE);
        }

        static const wchar_t filter[] =
            L"RePlayEngine Scene (*.replayscene)\0*.replayscene\0All Files (*.*)\0*.*\0\0";
        OPENFILENAMEW dialog{};
        dialog.lStructSize = sizeof(dialog);
        dialog.hwndOwner = owner;
        dialog.lpstrFile = filename;
        dialog.nMaxFile = static_cast<DWORD>(_countof(filename));
        dialog.lpstrFilter = filter;
        dialog.lpstrDefExt = L"replayscene";
        dialog.lpstrTitle = save ? L"Save RePlayEngine Scene" : L"Open RePlayEngine Scene";
        dialog.Flags = OFN_EXPLORER | OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST |
            (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
        const BOOL accepted = save ? GetSaveFileNameW(&dialog) : GetOpenFileNameW(&dialog);
        return accepted ? std::filesystem::path(filename) : std::filesystem::path{};
    }

    std::filesystem::path AutosavePathFor(const std::filesystem::path& scene_path)
    {
        const std::string source = scene_path.lexically_normal().generic_string();
        std::uint64_t hash = 1469598103934665603ull;
        for (const unsigned char byte : source)
        {
            hash ^= byte;
            hash *= 1099511628211ull;
        }
        const std::string stem = scene_path.stem().empty() ? "Untitled" : scene_path.stem().string();
        return std::filesystem::path("Saved") / "Autosave" /
            (stem + "_" + std::to_string(hash) + ".autosave.replayscene");
    }
}

// ---------------------------------------------------------------------------
// 初期化
// ---------------------------------------------------------------------------

// 起動時の流れはこの関数だけ。順序に意味がある。
//
//   1. Component 型を登録する
//   2. プロジェクト設定を読み込む
//   3. Scene v9 を読み込む（GameObject / Component / Property / Collider 参照 /
//      controlledObjectId がここで復元される）
//   4. Scene を開始する
//   5. 衝突世界を Scene へ Attach する
//
// ここで GameObject を作ることも、Prefab を配置することも、
// Component をコードから付け足すことも一切しない。
// Scene ファイルの内容がそのまま起動後の状態になる。
void framework::initialize_object_scene()
{
    // Component 型の登録。Scene を作る前・読む前に 1 回だけ。
    // 二重に呼んでも ComponentRegistry が重複を弾くので安全。
    ReplayEngine::Core::RegisterBuiltInComponents();

    // プロジェクト設定。Scene より先に読む。
    // ただしここで Prefab を配置することはない。設定を持っているだけ。
    load_project_settings();

    object_scene.SetName("TrainingStage");
    object_editor_context.AttachScene(&object_scene);
    object_editor_context.SetAssetDatabase(&asset_database);
    object_editor_context.SetScenePath(object_scene_path);

    // 既定の Scene があれば読み込む。無くても失敗扱いにしない（空シーンとして扱う）。
    // 「Scene が無いから既定のキャラクターを置く」ことはしない。
    std::error_code filesystem_error;
    if (std::filesystem::exists(object_scene_path, filesystem_error) && !filesystem_error)
    {
        load_object_scene(false);
    }
    else
    {
        object_editor_context.SetStatus(
            "シーンファイルがありません（空のシーンとして開始しました）");
    }
    check_object_scene_recovery();

    // 編集中も Component の OnStart を回したいので Scene は開始状態にしておく。
    // 実際にゲームロジックが走るかどうかは Edit Mode 判定で制御する。
    object_scene.Start();

    // 衝突世界を編集 Scene へつなぐ。
    // これ以降 Component が見る IPhysicsQueryService は衝突世界であり、
    // 旧 Stage は衝突世界の内側から必要なときだけ呼ばれる。
    initialize_collision_world();

    // Scene View の編集カメラを、この Scene 用に保存された状態から復元する。
    // 保存が無い / 壊れていても、既定位置になるだけで Scene の読み込みには影響しない。
    load_editor_camera_state();
}

// ---------------------------------------------------------------------------
// プロジェクト設定
// ---------------------------------------------------------------------------

void framework::load_project_settings()
{
    namespace Project = ReplayEngine::Project;

    std::string error;
    const auto path = Project::ProjectSettingsSerializer::DefaultPath();
    if (Project::ProjectSettingsSerializer::LoadFromFile(project_settings, path, error))
    {
        project_settings_status = "プロジェクト設定を読み込みました";
    }
    else
    {
        // 未作成・壊れているのどちらでも、既定値のまま続行する。
        // ここで assert も例外も出さない。
        project_settings_status = error;
    }
}

bool framework::save_project_settings()
{
    namespace Project = ReplayEngine::Project;

    std::string error;
    const auto path = Project::ProjectSettingsSerializer::DefaultPath();
    if (!Project::ProjectSettingsSerializer::SaveToFile(project_settings, path, error))
    {
        project_settings_status = "プロジェクト設定の保存に失敗しました: " + error;
        return false;
    }
    project_settings_status = "プロジェクト設定を保存しました";
    return true;
}

ReplayEngine::Project::PrefabReferenceStatus
    framework::resolve_default_character_prefab() const
{
    return project_settings.ResolveDefaultCharacterPrefab(asset_database);
}

ReplayEngine::Scene::Scene& framework::active_object_scene() noexcept
{
    return object_scene_play_mode ? object_scene_runtime : object_scene;
}

const ReplayEngine::Scene::Scene& framework::active_object_scene() const noexcept
{
    return object_scene_play_mode ? object_scene_runtime : object_scene;
}

// ---------------------------------------------------------------------------
// 更新
// ---------------------------------------------------------------------------

bool framework::object_runtime_active() const noexcept
{
    // ゲームロジック（入力・物理）を動かしてよいか。
    //
    // 【今回の不具合の原因】
    //   以前はここが object_scene_play_mode（F5）だけを見ていた。
    //   そのため Editor を開いて F5 を押すまで入力も物理も一切動かず、
    //   「PlayerController があるのに動かない」状態になっていた。
    //
    // 正しい条件は「編集中でないこと」。
    //   Editor 非表示（通常のゲーム実行） -> 動く
    //   Editor 表示 + Edit Mode           -> 止まる（GameObject を編集中）
    //   Editor 表示 + Play Mode (F5)      -> 動く
    const bool editing = editor_mode && edit_mode_active && !object_scene_play_mode;
    if (object_scene_play_mode && object_scene_paused) return false;
    return !editing;
}

void framework::refresh_object_scene_services()
{
    ReplayEngine::Scene::Scene& scene = active_object_scene();

    // カメラの橋渡しを毎フレーム張り直す。
    // GameScene が作り直された場合でも参照が古くならないようにするため。
    if (game_scene != nullptr) object_camera_bridge.Attach(&game_scene->Gameplay().GetCamera());
    else                       object_camera_bridge.Detach();

    ReplayEngine::Scene::SceneServices& services = scene.Services();
    services.SetCameraBasis(&object_camera_bridge);
    services.SetPlaying(object_runtime_active());

    // 地形の問い合わせ先は衝突世界。
    // 旧 Stage を直接 Physics サービスへ挿すことはもうしない。
    // 旧 Stage は衝突世界の内側で、未移行のときだけ使われる。
    refresh_collision_world();

    // 操作対象を確定する。
    //
    // Scene に保存されていた ID がそのまま操作対象になる。
    // その GameObject が消えていれば無効化するだけで、
    // 代わりの GameObject を探すことも、何かを生成することもしない。
    player_control_system.SetControlledObject(services.ControlledObject());
    const ReplayEngine::Core::ObjectID controlled = player_control_system.Resolve(scene);
    services.SetControlledObject(controlled);

    // 操作対象が居ないことは「異常」ではなく「そういう Scene」。
    // Editor へ知らせるためにフラグを立てるだけで、復旧処理は一切しない。
    object_missing_controlled_target = !controlled.Valid();
}

void framework::update_object_fixed_step(float elapsed_time)
{
    ReplayEngine::Scene::Scene& scene = active_object_scene();

    // 編集中は物理を進めない。実行中だけ固定時間で更新する。
    if (!object_runtime_active())
    {
        // 停止中に時間を貯め込まない。再開時にまとめて進むのを防ぐ。
        object_fixed_accumulator = 0.0f;
        return;
    }

    if (object_fixed_time_step <= 0.0f) return;

    // deltaTime の異常値を制限する。
    // ブレークポイントで止めた直後などに巨大な値が来て、物理が飛ぶのを防ぐ。
    const float maximum_frame_time = object_fixed_time_step *
        static_cast<float>(object_max_fixed_substeps);
    float frame_time = elapsed_time;
    if (frame_time < 0.0f) frame_time = 0.0f;
    if (frame_time > maximum_frame_time) frame_time = maximum_frame_time;

    object_fixed_accumulator += frame_time;

    // 最大サブステップ数で打ち切る。
    // フレーム落ちしたときに追いつこうとして、さらに重くなる悪循環を防ぐ。
    int steps = 0;
    while (object_fixed_accumulator >= object_fixed_time_step &&
        steps < object_max_fixed_substeps)
    {
        object_fixed_accumulator -= object_fixed_time_step;
        scene.FixedUpdate(object_fixed_time_step);
        ++steps;
    }

    // 打ち切った分の余りは捨てる。次フレームへ持ち越すと追いつき続けてしまう。
    if (steps >= object_max_fixed_substeps) object_fixed_accumulator = 0.0f;
}

void framework::update_object_scene(float elapsed_time)
{
    refresh_object_scene_services();

    ReplayEngine::Scene::Scene& scene = active_object_scene();

    // 編集中（F3 で停止中）は Component を更新しない。
    // Editor で置いた GameObject が編集中に勝手に動くのを防ぐ。
    if (object_runtime_active())
    {
        // 順序: Update（入力・意思決定）→ FixedUpdate（物理）→ LateUpdate → カメラ
        // 入力は Update で読み、FixedUpdate がその値を消費する。
        scene.Update(elapsed_time);
        update_object_fixed_step(elapsed_time);
        scene.LateUpdate(elapsed_time);

        // 位置が確定してから Trigger を判定する。
        // FixedUpdate の途中で判定すると、まだ押し戻されていない位置で
        // Enter が出てしまい、次のフレームですぐ Exit になる。
        dispatch_collision_triggers();

        // Transform が確定してからカメラを動かす。
        update_object_camera_follow(elapsed_time);
    }
    else
    {
        // 停止中でも生成・削除の予約だけは反映する。
        // Editor 操作の結果が次のフレームまで残らないようにするため。
        scene.ProcessPendingOperations();
    }

    // 選択が消えた GameObject を指し続けないようにする。
    object_editor_context.Selection().PruneMissing(scene);
    sync_object_lights();

    if (!object_scene_play_mode && object_editor_context.Dirty())
    {
        object_autosave_elapsed += (std::max)(0.0f, elapsed_time);
        if (object_autosave_elapsed >= 60.0f)
        {
            autosave_object_scene();
            object_autosave_elapsed = 0.0f;
        }
    }
    else
    {
        object_autosave_elapsed = 0.0f;
    }

    // 描画提出リストを作り直す。ここでは D3D に触れない。
    ReplayEngine::Rendering::SceneRenderCollector::Collect(scene, object_render_items);
}

void framework::sync_object_lights()
{
    using ReplayEngine::Components::DirectionalLightComponent;
    using ReplayEngine::Components::PointLightComponent;
    using ReplayEngine::Components::SpotLightComponent;
    using namespace DirectX;

    const ReplayEngine::Scene::Scene& scene = active_object_scene();
    lights.data.light_counts = { 0, 0, 0, 0 };
    bool directional_found = false;

    for (std::size_t index = 0; index < scene.GameObjectCount(); ++index)
    {
        const ReplayEngine::Core::GameObject* object = scene.GameObjectAt(index);
        if (object == nullptr || object->PendingDestroy() || !object->ActiveInHierarchy()) continue;

        if (!directional_found)
        {
            const DirectionalLightComponent* light = object->GetComponent<DirectionalLightComponent>();
            if (light != nullptr && light->ActiveInHierarchy())
            {
                const XMFLOAT4 rotation = object->GetTransform().WorldRotationQuaternion();
                XMVECTOR direction = XMVector3Rotate(XMVectorSet(0.0f, -1.0f, 0.0f, 0.0f),
                    XMLoadFloat4(&rotation));
                direction = XMVector3Normalize(direction);
                XMStoreFloat4(&light_direction, direction);
                light_direction.w = 0.0f;
                pbr.light.directional_color = {
                    light->color.x, light->color.y, light->color.z,
                    (std::max)(0.0f, light->intensity) };
                pbr.light.shadow_params.w = light->cast_shadows ? 1.0f : 0.0f;
                directional_found = true;
            }
        }

        if (lights.data.light_counts.x < lights_manager::POINT_LIGHT_MAX)
        {
            const PointLightComponent* light = object->GetComponent<PointLightComponent>();
            if (light != nullptr && light->ActiveInHierarchy())
            {
                const int slot = lights.data.light_counts.x++;
                const XMFLOAT3 position = object->GetTransform().WorldPosition();
                lights.data.point_lights[slot].position = {
                    position.x, position.y, position.z, (std::max)(0.01f, light->range) };
                lights.data.point_lights[slot].color = {
                    light->color.x, light->color.y, light->color.z,
                    (std::max)(0.0f, light->intensity) };
            }
        }

        if (lights.data.light_counts.y < lights_manager::SPOT_LIGHT_MAX)
        {
            const SpotLightComponent* light = object->GetComponent<SpotLightComponent>();
            if (light != nullptr && light->ActiveInHierarchy())
            {
                const int slot = lights.data.light_counts.y++;
                const XMFLOAT3 position = object->GetTransform().WorldPosition();
                const XMFLOAT4 rotation = object->GetTransform().WorldRotationQuaternion();
                XMVECTOR direction = XMVector3Rotate(XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f),
                    XMLoadFloat4(&rotation));
                direction = XMVector3Normalize(direction);
                XMFLOAT3 direction_value{};
                XMStoreFloat3(&direction_value, direction);
                const float outer = (std::max)(0.1f, (std::min)(179.0f, light->outer_angle_degrees));
                const float inner = (std::max)(0.1f, (std::min)(outer, light->inner_angle_degrees));
                lights.data.spot_lights[slot].position = {
                    position.x, position.y, position.z, (std::max)(0.01f, light->range) };
                lights.data.spot_lights[slot].direction = {
                    direction_value.x, direction_value.y, direction_value.z,
                    std::cos(XMConvertToRadians(inner)) };
                lights.data.spot_lights[slot].color = {
                    light->color.x, light->color.y, light->color.z,
                    std::cos(XMConvertToRadians(outer)) };
                lights.data.spot_lights[slot].params.x = (std::max)(0.0f, light->intensity);
            }
        }
    }

    if (!directional_found)
    {
        pbr.light.directional_color = { 0.0f, 0.0f, 0.0f, 0.0f };
        pbr.light.shadow_params.w = 0.0f;
    }
}

void framework::update_object_camera_follow(float elapsed_time)
{
    if (game_scene == nullptr) return;

    // 追従対象は「CameraTargetComponent を持つ操作対象 GameObject」。
    // カメラ側は操作対象の具象型を一切知らない。
    //
    // 解決の材料は次の 4 つだけ。
    //   controlledObjectId / CameraTargetComponent / Runtime Scene / ObjectID
    // GameObject 名も Prefab 名も見ない。
    const ReplayEngine::Scene::Scene& scene = active_object_scene();
    const ReplayEngine::Core::ObjectID controlled = scene.Services().ControlledObject();

    const ReplayEngine::Core::GameObject* target =
        controlled.Valid() ? scene.FindGameObjectByID(controlled) : nullptr;

    const ReplayEngine::Components::CameraTargetComponent* camera_target = target != nullptr
        ? target->GetComponent<ReplayEngine::Components::CameraTargetComponent>()
        : nullptr;

    // 対象が居ない / CameraTarget が外された / 無効化された。
    // どの場合も追従を止めて自由カメラへ戻す。
    // 「代わりに別のものを追う」ことはしない。
    if (target == nullptr || camera_target == nullptr || !camera_target->ActiveInHierarchy())
    {
        game_scene->Gameplay().UpdateFreeCamera(elapsed_time);
        return;
    }

    // 追従点は GameObject のワールド位置 + target_offset。
    const DirectX::XMFLOAT3 world = target->GetTransform().WorldPosition();
    const DirectX::XMFLOAT3 anchor{
        world.x + camera_target->target_offset.x,
        world.y + camera_target->target_offset.y,
        world.z + camera_target->target_offset.z };

    game_scene->Gameplay().FollowCameraTarget(
        anchor,
        camera_target->look_at_offset,
        camera_target->follow_distance,
        camera_target->follow_height,
        camera_target->follow_lag,
        elapsed_time);
}

// ---------------------------------------------------------------------------
// 保存・読み込み
// ---------------------------------------------------------------------------
//
// Editor パネルの描画はここでは行わない。
// 既存の「階層」「インスペクター」ウィンドウの中へ埋め込む形にしてあり、
// HierarchyPanel::DrawContents / InspectorPanel::DrawContents を
// framework_editor.cpp と framework_inspector.cpp から直接呼んでいる。
// 同じ内容のウィンドウを新旧で二重に出さないため、この配置にしている。

bool framework::save_object_scene(bool choose_path)
{
    if (object_scene_play_mode)
    {
        // 実行中の一時的な変化（HP の減少・移動後の位置）をファイルへ焼き込まない。
        object_editor_context.SetStatus("実行中はシーンを保存できません");
        return false;
    }

    std::filesystem::path destination = object_scene_path;
    if (choose_path || destination.empty())
    {
        destination = BrowseObjectSceneFile(hwnd, true, destination);
        if (destination.empty())
        {
            object_editor_context.SetStatus("保存をキャンセルしました");
            return false;
        }
        if (destination.extension().empty())
            destination += ReplayEngine::Scene::Serialization::SceneSerializer::file_extension;
    }

    // メインスレッドでスナップショットを取ってから書き出す。
    // Scene の実体をファイル処理へ渡さないことで、
    // 将来書き込みだけを別スレッドへ回せる形にしてある。
    SceneSerialization::SceneData data;
    SceneSerialization::CaptureScene(object_scene, data);

    std::string error;
    if (!SceneSerialization::SceneSerializer::SaveToFile(data, destination, error))
    {
        object_editor_context.SetStatus("保存に失敗しました: " + error);
        return false;
    }

    object_scene_path = std::move(destination);
    object_editor_context.SetScenePath(object_scene_path);
    object_editor_context.ClearDirty();
    object_autosave_elapsed = 0.0f;
    std::error_code remove_error;
    std::filesystem::remove(AutosavePathFor(object_scene_path), remove_error);
    object_recovery_available = false;
    object_editor_context.SetStatus("保存しました: " + object_scene_path.filename().string());
    return true;
}

bool framework::load_object_scene(bool choose_path)
{
    if (object_scene_play_mode) exit_object_play_mode();

    // Scene を切り替える前に、今の Scene の編集カメラ状態を残す。
    // 戻ってきたときに同じ視点から再開できる。
    if (!editor_camera_state_key.empty()) save_editor_camera_state();

    std::filesystem::path source = object_scene_path;
    if (choose_path)
    {
        source = BrowseObjectSceneFile(hwnd, false, source);
        if (source.empty())
        {
            object_editor_context.SetStatus("読み込みをキャンセルしました");
            return false;
        }
    }

    SceneSerialization::SceneData data;
    std::string error;
    if (!SceneSerialization::SceneSerializer::LoadFromFile(data, source, error))
    {
        // 旧形式・破損・未存在。いずれもクラッシュさせず、現在の Scene を維持する。
        object_editor_context.SetStatus("読み込めませんでした: " + error);
        return false;
    }

    // Scene の中身が総入れ替えになるので、先に衝突世界を切り離す。
    // 古い ObjectID / ColliderID を持ったまま新しい Scene を引くと、
    // まったく別の GameObject へ当たることになる。
    detach_collision_world();

    SceneSerialization::SceneLoadReport report;
    SceneSerialization::ApplySceneData(data, object_scene, report);
    object_editor_context.ResetSceneState();

    // 読み込み後に Scene を開始する。
    // ApplySceneData の中では OnStart / OnEnable を呼ばないので、
    // 途中まで構築された状態で Component が動くことはない。
    object_scene.Start();

    object_editor_context.AttachScene(&object_scene);
    object_scene_path = std::move(source);
    object_editor_context.SetScenePath(object_scene_path);
    object_editor_context.ClearDirty();

    // 新しい Scene の Backend Mode と移行済み集合でつなぎ直す。
    attach_collision_world(object_scene);

    std::string status = "読み込みました: " + object_scene_path.filename().string();
    if (!report.Clean())
    {
        status += "（警告 " + std::to_string(report.warnings.size()) + " 件）";
        for (const std::string& warning : report.warnings)
        {
            OutputDebugStringA(("[Scene] " + warning + "\n").c_str());
        }
    }
    object_editor_context.SetStatus(status);

    // 新しい Scene に対応する編集カメラ状態を読み込む。
    // 保存が無ければ既定位置になる。
    load_editor_camera_state();
    check_object_scene_recovery();
    return true;
}

bool framework::autosave_object_scene()
{
    if (object_scene_play_mode || !object_editor_context.Dirty()) return false;

    SceneSerialization::SceneData data;
    SceneSerialization::CaptureScene(object_scene, data);
    const std::filesystem::path path = AutosavePathFor(object_scene_path);
    std::string error;
    if (!SceneSerialization::SceneSerializer::SaveToFile(data, path, error))
    {
        object_autosave_status = "Autosave failed: " + error;
        return false;
    }
    object_autosave_status = "Autosaved: " + path.filename().string();
    return true;
}

void framework::check_object_scene_recovery()
{
    object_recovery_path = AutosavePathFor(object_scene_path);
    object_recovery_available = false;
    object_recovery_prompt_opened = false;

    std::error_code error;
    if (!std::filesystem::exists(object_recovery_path, error) || error) return;
    const auto autosave_time = std::filesystem::last_write_time(object_recovery_path, error);
    if (error) return;
    if (!std::filesystem::exists(object_scene_path, error) || error)
    {
        object_recovery_available = true;
        return;
    }
    const auto scene_time = std::filesystem::last_write_time(object_scene_path, error);
    object_recovery_available = !error && autosave_time > scene_time;
}

// ---------------------------------------------------------------------------
// Play Mode
// ---------------------------------------------------------------------------
//
// 【編集カメラと Runtime Camera の関係】
//   Play 開始時に編集カメラの値を Runtime Camera へ写さない。
//   Play 終了時に Runtime Camera の値を編集カメラへ取り込まない。
//   editor_camera は Play の出入りで一切書き換えられないので、
//   Play 前の視点がそのまま残る。切り替わるのは
//   「描画にどちらの行列を使うか」だけ（using_editor_camera）。

void framework::enter_object_play_mode()
{
    if (object_scene_play_mode) return;

    // 編集 Scene の内容を実行用 Scene へ複製する。
    // 直接コピーせず SceneData を経由するのは、
    // Scene が生ポインタで結ばれておりコピー不可なため。
    // これにより Play 中の変更が編集 Scene へ戻らないことが構造的に保証される。
    SceneSerialization::SceneData snapshot;
    SceneSerialization::CaptureScene(object_scene, snapshot);

    SceneSerialization::SceneLoadReport report;
    SceneSerialization::ApplySceneData(snapshot, object_scene_runtime, report);
    object_scene_runtime.Start();

    // 衝突世界を実行用 Scene へ差し替える。
    // 編集 Scene の ObjectID / ColliderID はここで完全に捨てられるので、
    // Play 中に編集 Scene の Collider へ当たることはない。
    attach_collision_world(object_scene_runtime);

    // Play 開始時に貯まっていた時間を捨てる。開始直後に物理が飛ぶのを防ぐ。
    object_fixed_accumulator = 0.0f;

    object_scene_play_mode = true;
    object_scene_paused = false;
    object_editor_context.SetPlayMode(true);
    object_editor_context.AttachScene(&object_scene_runtime);
    object_editor_context.SetStatus("実行中（編集シーンは保持されています）");
}

void framework::exit_object_play_mode()
{
    if (!object_scene_play_mode) return;

    // 先に衝突世界を切り離す。
    // Scene を消してから切り離すと、その間に問い合わせが来た場合に
    // 破棄済みの GameObject を引きに行ってしまう。
    detach_collision_world();

    // 実行用 Scene を捨てる。編集 Scene には一切触れていないので、
    // Play 前の状態がそのまま残っている。
    object_scene_runtime.Clear();

    object_fixed_accumulator = 0.0f;

    object_scene_play_mode = false;
    object_scene_paused = false;
    object_editor_context.SetPlayMode(false);
    object_editor_context.AttachScene(&object_scene);
    object_editor_context.SetStatus("編集モードへ戻りました");
}

// ---------------------------------------------------------------------------
// 描画
// ---------------------------------------------------------------------------

skinned_mesh* framework::resolve_object_mesh(const std::string& asset_guid)
{
    // 1) Asset 未指定。Editor で MeshRenderer を付けただけの状態はこれになる。
    //    正常な状態なので警告も出さず、静かに描画対象から外す。
    if (asset_guid.empty()) return nullptr;
    if (!device) return nullptr;

    // 2) 読み込み済みならそれを返す。キャッシュには有効なメッシュしか入らない。
    const auto cached = object_mesh_cache.find(asset_guid);
    if (cached != object_mesh_cache.end()) return cached->second.get();

    // 3) 一度失敗した Asset は再試行しない。ログも一度きりで済む。
    if (object_mesh_failures.find(asset_guid) != object_mesh_failures.end()) return nullptr;

    // 失敗を記録してログへ出す。以降このフレームでは何も返さない。
    const auto give_up = [this, &asset_guid](const std::string& reason) -> skinned_mesh*
    {
        object_mesh_failures.insert(asset_guid);
        const std::string message = "[Mesh] " + reason + " (GUID: " + asset_guid + ")";
        OutputDebugStringA((message + "\n").c_str());
        // Editor のステータス欄にも出して、原因が画面から分かるようにする。
        object_editor_context.SetStatus(message);
        return nullptr;
    };

    // 4) GUID が AssetDatabase で解決できるか。
    //    古い Scene ファイルや EditorSession に残った GUID はここで弾かれる。
    const ReplayEngine::Assets::AssetRecord* record = asset_database.FindByGuid(asset_guid);
    if (record == nullptr)
    {
        return give_up("Asset がプロジェクトに登録されていません");
    }

    // 5) 対応拡張子かどうか。
    //    skinned_mesh が読めるのは FBX と、その .cereal キャッシュだけ。
    //    .obj は static_mesh 用、.glb / .gltf は既存のステージ経路が扱うので、
    //    ここへ渡すと必ず失敗する。渡す前に弾く。
    const std::filesystem::path source = record->source_path;
    std::string extension = source.extension().string();
    for (char& character : extension)
    {
        character = static_cast<char>(::tolower(static_cast<unsigned char>(character)));
    }
    if (extension != ".fbx" && extension != ".cereal")
    {
        return give_up("この形式は GameObject の描画へ接続していません（" +
            (extension.empty() ? std::string("拡張子なし") : extension) + "）: " +
            source.generic_string());
    }

    // 6) 実ファイルが存在するか。
    //    skinned_mesh は .cereal キャッシュを読むので、そちらの有無を見る。
    std::filesystem::path cache = source;
    cache.replace_extension(L".cereal");

    std::error_code filesystem_error;
    if (!std::filesystem::exists(cache, filesystem_error) || filesystem_error)
    {
        return give_up("実行用の .cereal キャッシュが見つかりません: " + cache.generic_string());
    }

    // 7) ここまで通ってから構築する。
    std::unique_ptr<skinned_mesh> loaded;
    try
    {
        loaded = std::make_unique<skinned_mesh>(device.Get(), source.string().c_str());
    }
    catch (...)
    {
        // 既存プロジェクトは例外を前提にしていないため、ここで受け止めて
        // 「描けない Asset」として扱う。Scene 全体の描画は継続する。
        return give_up("メッシュの読み込みに失敗しました: " + source.generic_string());
    }

    if (!loaded)
    {
        return give_up("メッシュを構築できませんでした: " + source.generic_string());
    }

    // 8) 成功したものだけをキャッシュへ入れる。
    skinned_mesh* raw = loaded.get();
    object_mesh_cache.emplace(asset_guid, std::move(loaded));
    return raw;
}

const ReplayEngine::Rendering::MaterialAsset* framework::resolve_object_material(
    const std::string& asset_guid)
{
    using ReplayEngine::Assets::AssetKind;
    using ReplayEngine::Rendering::MaterialAsset;

    if (asset_guid.empty()) return nullptr;
    const ReplayEngine::Assets::AssetRecord* record = asset_database.FindByGuid(asset_guid);
    if (record == nullptr || record->kind != AssetKind::Material) return nullptr;

    std::error_code filesystem_error;
    const auto write_time = std::filesystem::last_write_time(
        record->source_path, filesystem_error);
    if (filesystem_error)
    {
        object_material_failures.insert(asset_guid);
        return nullptr;
    }

    const auto cached = object_material_cache.find(asset_guid);
    if (cached != object_material_cache.end() && cached->second.write_time == write_time)
        return &cached->second.material;

    MaterialAsset loaded;
    std::string error;
    if (!MaterialAsset::Load(record->source_path, loaded, error))
    {
        if (object_material_failures.insert(asset_guid).second)
            OutputDebugStringA(("[Material] " + error + " (GUID: " + asset_guid + ")\n").c_str());
        return nullptr;
    }

    object_material_failures.erase(asset_guid);
    cached_material_asset entry;
    entry.material = std::move(loaded);
    entry.write_time = write_time;
    auto inserted = object_material_cache.insert_or_assign(asset_guid, std::move(entry));
    return &inserted.first->second.material;
}

ReplayEngine::Rendering::RenderItem framework::resolve_render_item_material(
    const ReplayEngine::Rendering::RenderItem& source)
{
    ReplayEngine::Rendering::RenderItem item = source;
    const ReplayEngine::Rendering::MaterialAsset* material =
        resolve_object_material(source.material_asset);
    if (material == nullptr) return item;

    if (!source.material_override)
    {
        item.tint = material->base_color;
        item.shading_model = material->shading_model;
    }
    item.metallic = material->metallic;
    item.roughness = material->roughness;
    item.ambient_occlusion = material->ambient_occlusion;
    item.emissive_strength = (std::max)({ material->emissive.x,
        material->emissive.y, material->emissive.z }) * material->emissive_strength;
    item.double_sided = material->double_sided;
    return item;
}

void framework::draw_object_scene_meshes(ID3D11PixelShader* override_pixel_shader,
    bool gbuffer_pass, bool depth_only)
{
    if (object_render_items.Empty()) return;

    for (const ReplayEngine::Rendering::RenderItem& source_item : object_render_items.Items())
    {
        const ReplayEngine::Rendering::RenderItem item =
            resolve_render_item_material(source_item);
        // Asset 未指定・解決不可・読み込み失敗のいずれでも nullptr が返る。
        // その場合はこの GameObject を描かずに次へ進むだけで、実行は継続する。
        if (item.mesh_asset.empty()) continue;
        skinned_mesh* mesh = resolve_object_mesh(item.mesh_asset);
        if (mesh == nullptr) continue;

        // Animator が決めたクリップと時刻から姿勢を求める。
        // 静的メッシュ扱いの提出（skinned=false）ではバインドポーズのまま描く。
        // 深度プリパスでも同じ姿勢で描かないと、本描画と深度が一致しない。
        const skinned_mesh::animation::keyframe* keyframe = item.skinned
            ? resolve_object_keyframe(*mesh, item.clip_index, item.animation_time)
            : nullptr;

        if (depth_only)
        {
            // 深度プリパス。ピクセルシェーダーを外し、モーションベクターも書かない。
            //
            // 【ここを通さないと何も見えない】
            //   本描画は DepthFunc=EQUAL で走る。プリパスで深度を書いていない
            //   メッシュは深度比較に必ず失敗し、画面から丸ごと消える。
            //   GBuffer へ出すものは、例外なくここでも描くこと。
            if (item.double_sided)
                immediate_context->RSSetState(
                    rasterizer_states[(size_t)RASTER_STATE::CULL_NONE].Get());
            mesh->render(immediate_context.Get(), item.world, item.tint,
                keyframe, nullptr, nullptr, nullptr, false, false);
            if (item.double_sided)
                immediate_context->RSSetState(
                    rasterizer_states[(size_t)RASTER_STATE::SOLID].Get());
            continue;
        }

        // GBuffer パスでは Component が指定した描画方式を材質定数へ流す。
        if (gbuffer_pass) bind_gbuffer_material(deferred_shading_model(item.shading_model),
            false, 6.0f, 1.0f, item.metallic, item.roughness,
            item.ambient_occlusion, item.emissive_strength);

        // 最後の引数がモーションベクター出力。GBuffer パスだけで真にする
        // （複数回渡すと前フレーム姿勢が壊れる）。
        if (item.double_sided)
            immediate_context->RSSetState(
                rasterizer_states[(size_t)RASTER_STATE::CULL_NONE].Get());
        mesh->render(immediate_context.Get(), item.world, item.tint,
            keyframe, override_pixel_shader, nullptr, nullptr, true, gbuffer_pass);
        if (item.double_sided)
            immediate_context->RSSetState(
                rasterizer_states[(size_t)RASTER_STATE::SOLID].Get());
    }
}

void framework::clear_object_mesh_cache() noexcept
{
    object_mesh_cache.clear();
    object_mesh_failures.clear();
}

void framework::clear_object_material_cache() noexcept
{
    object_material_cache.clear();
    object_material_failures.clear();
}

const skinned_mesh::animation::keyframe* framework::resolve_object_keyframe(
    skinned_mesh& mesh, int clip_index, float animation_time) const
{
    if (clip_index < 0) return nullptr;
    if (mesh.animation_clips.empty()) return nullptr;
    if (clip_index >= static_cast<int>(mesh.animation_clips.size())) return nullptr;

    const skinned_mesh::animation& clip = mesh.animation_clips.at(
        static_cast<std::size_t>(clip_index));
    if (clip.sequence.empty()) return nullptr;

    const float sampling_rate = clip.sampling_rate > 0.0f ? clip.sampling_rate : 60.0f;
    const float duration = static_cast<float>(clip.sequence.size()) / sampling_rate;

    // ループは提出された時刻をここで畳んで解決する。
    // Animator はクリップ長を知らないので、長さを知っている側で処理する。
    float time = animation_time;
    if (duration > 0.0f)
    {
        time = std::fmod(time, duration);
        if (time < 0.0f) time += duration;
    }

    int frame = static_cast<int>(time * sampling_rate);
    if (frame < 0) frame = 0;
    if (frame >= static_cast<int>(clip.sequence.size()))
    {
        frame = static_cast<int>(clip.sequence.size()) - 1;
    }
    return &clip.sequence.at(static_cast<std::size_t>(frame));
}

// ---------------------------------------------------------------------------
// 新規 Scene 作成
// ---------------------------------------------------------------------------
//
// ここが Prefab を配置する唯一の場所。
//
// 【なぜ「唯一」と言い切れるか】
//   PrefabSerializer::Instantiate を呼ぶのは、この関数と load_prefab()
//   （ユーザーが Prefab ファイルを選んで配置する操作）の 2 か所だけ。
//   どちらもユーザーの明示操作からしか呼ばれない。
//   起動処理・Scene 読み込み・Component 不足の検出からは呼ばれない。

namespace
{
    // ファイル名として使えない文字を置き換える。
    std::string MakeSafeSceneFileName(std::string name)
    {
        for (char& character : name)
        {
            if (character == '<' || character == '>' || character == ':' ||
                character == '"' || character == '/' || character == '\\' ||
                character == '|' || character == '?' || character == '*') character = '_';
        }
        return name.empty() ? std::string("Scene") : name;
    }
}

bool framework::create_object_scene(const std::string& name, bool place_default_character)
{
    namespace Project = ReplayEngine::Project;

    // 実行中に作り直すと、実行用 Scene と編集用 Scene の対応が壊れる。
    if (object_scene_play_mode) exit_object_play_mode();

    // Scene の中身が総入れ替えになるので、先に衝突世界を切り離す。
    // 古い ObjectID / ColliderID を持ったまま新しい Scene を引かせない。
    detach_collision_world();

    const std::string scene_name = name.empty() ? std::string("新しいシーン") : name;

    // 1) 空の Scene を作る。GameObject は 1 つも作らない。
    object_scene.Clear();
    object_editor_context.ResetSceneState();
    object_scene.SetName(scene_name);
    object_scene.Services().SetControlledObject(ReplayEngine::Core::ObjectID::Invalid());
    player_control_system.Clear();

    // 選択と Undo 履歴を作り直す。前の Scene の ObjectID を指し続けさせない。
    object_editor_context.AttachScene(&object_scene);

    std::string status = "空のシーンを作成しました";

    // 2) Default を選んだときだけ Prefab を 1 体配置する。
    if (place_default_character)
    {
        // Default Sceneは起動直後から材質を確認できるよう、通常のLight Componentを置く。
        // グローバルな固定ライトへは戻さず、Hierarchy/Inspector/Scene保存の対象にする。
        if (ReplayEngine::Core::GameObject* sun = object_scene.CreateGameObject("Sun"))
        {
            sun->GetTransform().SetLocalRotationEuler({ -0.75f, 0.4f, 0.0f });
            if (auto* light = sun->AddComponent<
                ReplayEngine::Components::DirectionalLightComponent>())
            {
                light->color = { 1.0f, 0.96f, 0.88f, 1.0f };
                light->intensity = 3.5f;
                light->cast_shadows = true;
            }
        }

        const Project::PrefabReferenceStatus prefab = resolve_default_character_prefab();

        if (prefab.IsUnset())
        {
            // 未設定でもクラッシュさせない。空シーンとして成立させる。
            status = "既定の操作キャラクター Prefab が未設定のため、空のシーンを作成しました";
        }
        else if (prefab.IsMissing())
        {
            status = "既定の操作キャラクター Prefab が見つかりません（Missing Prefab）。"
                "空のシーンを作成しました";
        }
        else
        {
            std::string error;
            SceneSerialization::SceneLoadReport report;
            const ReplayEngine::Core::ObjectID root =
                SceneSerialization::PrefabSerializer::Instantiate(
                    object_scene, prefab.path, error, &report, prefab.guid);

            if (!root.Valid())
            {
                status = "既定の操作キャラクター Prefab を配置できませんでした: " + error;
            }
            else
            {
                // 3) 配置した Prefab のルートを操作対象にする。
                //    GameObject 名でも Prefab 名でもなく、配置結果の ObjectID で指す。
                object_scene.Services().SetControlledObject(root);
                player_control_system.SetControlledObject(root);
                object_editor_context.Selection().Select(root, false);

                status = "既定の操作キャラクターを 1 体配置しました: " +
                    prefab.DisplayLabel();
                if (!report.Clean())
                {
                    status += "（警告 " + std::to_string(report.warnings.size()) + " 件）";
                    for (const std::string& warning : report.warnings)
                    {
                        OutputDebugStringA(("[Prefab] " + warning + "\n").c_str());
                    }
                }
            }
        }
    }

    // 4) Scene を開始する。ここで初めて OnStart / OnEnable が走る。
    object_scene.Start();

    // 5) 保存先を決める。既存ファイルは上書きしない。
    const std::filesystem::path folder = std::filesystem::path("resources") / "Scenes";
    const std::string safe_name = MakeSafeSceneFileName(scene_name);
    const std::string extension = SceneSerialization::SceneSerializer::file_extension;
    std::filesystem::path scene_path = folder / (safe_name + extension);
    std::error_code filesystem_error;
    for (std::uint32_t number = 2;
        std::filesystem::exists(scene_path, filesystem_error) && !filesystem_error; ++number)
    {
        scene_path = folder / (safe_name + "_" + std::to_string(number) + extension);
    }
    object_scene_path = std::move(scene_path);
    object_editor_context.SetScenePath(object_scene_path);

    // 6) 衝突世界を新しい Scene へつなぎ直す。
    attach_collision_world(object_scene);

    // 7) 再起動しても同じ状態から始められるよう、その場で書き出す。
    save_object_scene(false);

    // 8) 編集カメラを安全な既定位置へ置く。
    //    Runtime Camera にも CameraTargetComponent にも触れない。
    editor_camera.ResetToDefault();
    editor_camera_state_key = make_editor_camera_state_key();

    // Default Scene で Prefab を配置できた場合だけ、その 1 体へ一度フォーカスする。
    // 既存 Scene の読み込みではこれをしない（勝手に視点を動かさない）。
    if (place_default_character && object_scene.Services().ControlledObject().Valid())
    {
        focus_editor_camera_on_selection();
    }
    save_editor_camera_state();

    object_editor_context.SetStatus(status);
    return true;
}
