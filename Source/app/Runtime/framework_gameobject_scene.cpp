// GameObject / Component 基盤のうち「初期化・Scene access・固定更新・UI更新」を持つ。
//
//   framework_gameobject_scene.cpp                        … 初期化、設定、Scene access、固定更新（このファイル）
//   framework_gameobject_scene_motion.cpp                 … Motion Asset / Binding / Player 更新
//   framework_gameobject_scene_runtime.cpp                … Runtime Scene 更新、Light同期、Camera Follow
//   framework_gameobject_scene_play.cpp                   … Play Mode の開始・終了
//   framework_gameobject_scene_serialization.cpp          … 保存・復旧・Scene 操作
//   framework_gameobject_scene_serialization_creation.cpp … Default Ground と新規 Scene 生成
//   framework_gameobject_scene_rendering.cpp              … Mesh / Material 解決
//   framework_gameobject_scene_rendering_draw.cpp         … Object / Landscape 描画
//   framework_gameobject_scene_rendering_animation.cpp    … Mesh cache / Animation 解決
//
// 関数本体は分割前のまま移動し、処理順と分岐は変更しない。
#include "framework.h"

#include "../../RePlayEngine/Components/Camera/CameraComponent.h"
#include "../../RePlayEngine/Components/Camera/CameraTargetComponent.h"
#include "../../RePlayEngine/Components/Camera/FollowTargetComponent.h"
#include "../../RePlayEngine/Components/Motion/MotionPlayerComponent.h"
#include "../../RePlayEngine/Components/Core/PropertyLinkComponent.h"
#include "../../RePlayEngine/Components/UI/UIEffectStackComponent.h"
#include "../../RePlayEngine/Components/UI/UISpriteAnimatorComponent.h"
#include "../../RePlayEngine/Components/UI/UITextComponent.h"
#include "../../RePlayEngine/Components/Rendering/LightComponents.h"
#include "../../RePlayEngine/Components/Rendering/MeshRendererComponent.h"
#include "../../RePlayEngine/Components/Rendering/PrimitiveMeshRendererComponent.h"
#include "../../RePlayEngine/Components/Rendering/SkinnedMeshRendererComponent.h"
#include "../../RePlayEngine/Components/Landscape/LandscapeComponent.h"
#include "../../RePlayEngine/Components/Landscape/LandscapeRendererComponent.h"
#include "../../RePlayEngine/Object/Registry/BuiltInComponents.h"
#include "../../RePlayEngine/Project/ProjectSettingsSerializer.h"
#include "../../RePlayEngine/Localization/LocalizationService.h"
#include "../../RePlayEngine/Rendering/Adapter/SceneRenderCollector.h"
#include "../../RePlayEngine/Motion/MotionBindingResolver.h"
#include "../../RePlayEngine/Motion/MotionEvaluator.h"
#include "../../RePlayEngine/UI/UILayout.h"
#include "../../RePlayEngine/Runtime/Events/EventBus.h"
#include "../../RePlayEngine/Scene/Serialization/SceneData.h"
#include "../../RePlayEngine/Scene/Serialization/SceneSerializer.h"
#include "../../RePlayEngine/Scripting/CSharp/CSharpScriptBackend.h"
#include "../../RePlayEngine/Scripting/Core/ScriptComponent.h"
#include "../../RePlayEngine/Scripting/Core/ScriptRuntime.h"
#include "../../RePlayEngine/Scripting/Core/ScriptTypeCatalog.h"
#include "../../RePlayEngine/Scripting/Core/ScriptTypes.h"
#include "../../game/Behaviours/ValidationBehaviours.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace
{
    namespace SceneSerialization = ReplayEngine::Scene::Serialization;
}

// ---------------------------------------------------------------------------
// 初期化
// ---------------------------------------------------------------------------

// 起動時の流れはこの関数だけ。順序に意味がある。
//
//   1. Component 型を登録する
//   2. プロジェクト設定を読み込む
//   3. Sessionで指定された現行Sceneがあれば、その内容をそのまま読み込む
//   4. Sceneがまだ存在しない初回起動だけ、通常GameObject + Componentで
//      Landscape Ground + Sun の Basic Scene を作る
//   5. Scene を開始する
//   6. 衝突世界を Scene へ Attach する
//
// 既存Sceneをロードするときは自動GameObjectを追加しない。
// Basic Sceneの自動生成は「読み込むSceneが無い初回」だけに限定し、
// Empty Sceneはユーザー操作で引き続き完全な空Sceneとして作成できる。
void framework::initialize_object_scene()
{
    // Component 型の登録。Scene を作る前・読む前に 1 回だけ。
    // 二重に呼んでも ComponentRegistry が重複を弾くので安全。
    ReplayEngine::Core::RegisterBuiltInComponents();

    // ゲーム側 Behaviour の登録。Engine の型が入ったあとに呼ぶ。
    //
    // ここで呼ばないと、Scene に保存された Behaviour が
    // Editor でも Runtime でも Missing Component になってしまう。
    // 静的初期化に頼らず明示的に呼ぶので、初期化順序の問題が起きない。
    Game::RegisterGameBehaviours();

    // プロジェクト設定。Scene より先に読む。
    // ただしここで Prefab を配置することはない。設定を持っているだけ。
    load_project_settings();

    object_scene.SetName(u8"新しいシーン");
    object_editor_context.AttachScene(&object_scene);
    object_editor_context.SetAssetDatabase(&asset_database);
    object_editor_context.SetScenePath(object_scene_path);

    bool created_startup_basic_scene = false;

    // Sessionで復元されたSceneがあれば後段で読み込む。ここでは固定Sampleへ
    // 依存せず、明示されたSceneパスがある場合だけ読み込む。
    // 「Scene が無いから既定のキャラクターを置く」ことはしない。
    std::error_code filesystem_error;
    if (std::filesystem::exists(object_scene_path, filesystem_error) && !filesystem_error)
    {
        load_object_scene(false);
    }
    else
    {
        // 初回起動は「何もない空間」ではなく、すぐ Sculpt と衝突確認を始められる
        // Basic Scene にする。特殊な World Terrain は作らず、通常の GameObject +
        // Landscape / Renderer / Collider Component だけで構成する。
        ReplayEngine::Core::GameObject* ground = create_default_landscape_ground(object_scene);
        created_startup_basic_scene = ground != nullptr;
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
        if (ground != nullptr)
        {
            object_editor_context.Selection().Select(ground->ID(), false);
            selected_editor_object = editor_selection::game_object;
            object_editor_context.MarkDirty();
            object_editor_context.SetStatus(
                "新規 Basic Scene を作成しました（Landscape Ground + Sun）");
        }
        else
        {
            object_editor_context.SetStatus(
                "新規 Scene を作成しました（Landscape Ground の生成に失敗）");
        }
    }
    if (!standalone_game_mode) check_object_scene_recovery();

    // 編集中も Component の OnStart を回したいので Scene は開始状態にしておく。
    // 実際にゲームロジックが走るかどうかは Edit Mode 判定で制御する。
    object_scene.Start();

    // 衝突世界を編集 Scene へつなぐ。
    // これ以降 Component が見る IPhysicsQueryService は衝突世界であり、
    // 衝突問い合わせはSceneCollisionWorldだけを経由する。
    initialize_collision_world();

    // Scene View の編集カメラを、この Scene 用に保存された状態から復元する。
    // 保存が無い / 壊れていても、既定位置になるだけで Scene の読み込みには影響しない。
    if (!standalone_game_mode) load_editor_camera_state();

    // 新規 Basic Scene だけは保存済みの「未保存Sceneカメラ」を使い回さない。
    // Ground が見えない状態から始まると生成失敗に見えるため、64m四方を
    // 斜め上から一目で確認できる位置へ合わせる。既存Sceneのカメラは触らない。
    if (created_startup_basic_scene)
        editor_camera.LookAt({ 0.0f, 30.0f, -42.0f }, { 0.0f, 0.0f, 0.0f });

    // Runtime 側のサービスを組み立てる。
    // World の所有者はここで確定し、以降 framework が Scene を値で持つことはない。
    initialize_runtime_services();

    //   Editor として起動している間、編集対象は必ず object_scene。
    //   Runtime World が有効になるのは Play (F5) か、--game 起動のときだけ。
    if (object_boot_from_startup_scene) begin_startup_scene();
}
// ---------------------------------------------------------------------------
void framework::load_project_settings()
{
    namespace Project = ReplayEngine::Project;

    std::string error;
    const auto path = content_path(Project::ProjectSettingsSerializer::DefaultPath());
    if (Project::ProjectSettingsSerializer::LoadFromFile(project_settings, path, error))
    {
        project_settings_status = "プロジェクト設定を読み込みました";
        ReplayEngine::Localization::LocalizationService::Global().Configure(
            &asset_database, project_settings.LocalizationTableGuid(),
            project_settings.DefaultLanguage());
    }
    else
    {
        // 未作成・壊れているのどちらでも、既定値のまま続行する。
        // ここで assert も例外も出さない。
        project_settings_status = error;
        ReplayEngine::Localization::LocalizationService::Global().Configure(
            &asset_database, project_settings.LocalizationTableGuid(),
            project_settings.DefaultLanguage());
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
    ReplayEngine::Localization::LocalizationService::Global().Configure(
        &asset_database, project_settings.LocalizationTableGuid(),
        project_settings.DefaultLanguage());
    return true;
}

ReplayEngine::Project::PrefabReferenceStatus
    framework::resolve_default_character_prefab() const
{
    return project_settings.ResolveDefaultCharacterPrefab(asset_database);
}

// Runtime 中の World は RuntimeSceneService が所有する。
// ここでは所有せず、そのつど取り直すだけ。
// 戻り値の参照を呼び出し側が保存しないこと（次の切り替えで実体が変わる）。
ReplayEngine::Scene::Scene& framework::active_object_scene() noexcept
{
    return object_runtime_world_active ? object_runtime_scenes.ActiveWorld() : object_scene;
}

const ReplayEngine::Scene::Scene& framework::active_object_scene() const noexcept
{
    return object_runtime_world_active ? object_runtime_scenes.ActiveWorld() : object_scene;
}
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
    // Editor 内で Runtime World を動かす入口は F5 Play Session だけ。
    // F3 は Editor UI/input capture の切替であり、第2の Play Mode にはしない。
    if (object_scene_play_mode && object_scene_paused) return false;
    if (!editor_mode) return true;
    return object_scene_play_mode;
}

framework::object_ui_viewport framework::object_ui_viewport_target() const noexcept
{
    object_ui_viewport target{};
    target.width = (std::max)(1.0f, static_cast<float>(client_width));
    target.height = (std::max)(1.0f, static_cast<float>(client_height));
    target.logical_width = target.width;
    target.logical_height = target.height;

#ifdef USE_IMGUI
    if (editor_mode && !object_scene_play_mode && scene_view_overlay_valid)
    {
        // Editor では Scene View の矩形へ、実行時は従来どおりウィンドウ全体へ描く。
        target.left = scene_view_overlay_position.x;
        target.top = scene_view_overlay_position.y;
        target.width = (std::max)(1.0f, scene_view_overlay_size.x);
        target.height = (std::max)(1.0f, scene_view_overlay_size.y);
        target.logical_width = target.width;
        target.logical_height = target.height;

        if (active_editor_workspace == editor_workspace::ui)
        {
            int logical_width = 0;
            int logical_height = 0;
            ui_preview_resolution_size(logical_width, logical_height);
            target.logical_width = (std::max)(1.0f, static_cast<float>(logical_width));
            target.logical_height = (std::max)(1.0f, static_cast<float>(logical_height));
            const float zoom = (std::max)(0.10f, ui_preview_zoom);
            const float view_width = (std::max)(1.0f, target.logical_width * zoom);
            const float view_height = (std::max)(1.0f, target.logical_height * zoom);
            target.left += (target.width - view_width) * 0.5f;
            target.top += (target.height - view_height) * 0.5f;
            target.width = view_width;
            target.height = view_height;
        }
    }
#endif

    return target;
}

void framework::refresh_object_scene_services()
{
    ReplayEngine::Scene::Scene& scene = active_object_scene();

    // カメラの橋渡しを毎フレーム張り直す。
    // GameScene が作り直された場合でも参照が古くならないようにするため。
    const ReplayEngine::Components::CameraSelection camera_selection =
        ReplayEngine::Components::ResolveActiveCameraSelection(scene);
    if (camera_selection.Valid())
    {
        object_camera_bridge.AttachBasis(
            camera_selection.component->Forward(),
            camera_selection.component->Right());
    }
    else if (game_scene != nullptr)
    {
        object_camera_bridge.Attach(&game_scene->Gameplay().GetCamera());
    }
    else
    {
        object_camera_bridge.Detach();
    }

    ReplayEngine::Scene::SceneServices& services = scene.Services();
    services.SetCameraBasis(&object_camera_bridge);
    services.SetInput(&game_input);
    services.SetAudio(&object_audio_system);
    services.SetMotionMixer(&motion_mixer);
    services.SetPlaying(object_runtime_active());
    services.SetRuntimeScene(object_runtime_active() ? &object_runtime_scenes : nullptr);
    services.SetSceneFlow(object_runtime_active() ? object_scene_flow.get() : nullptr);

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
        // Component の FixedUpdate（入力・力の蓄積）の後に Solver を 1 回だけ進める。
        // Transform 同期は Solver の末尾で行うため、同じ刻み内の更新順が一定になる。
        object_collision_world.Refresh();
        object_physics_dynamics_world.Step(object_fixed_time_step);
        object_collision_world.Refresh();
        ++steps;
    }

    // 打ち切った分の余りは捨てる。次フレームへ持ち越すと追いつき続けてしまう。
    if (steps >= object_max_fixed_substeps) object_fixed_accumulator = 0.0f;
}

void framework::update_ui_sprite_animators(ReplayEngine::Scene::Scene& scene,
    float elapsed_time)
{
    using ReplayEngine::Components::UISpriteAnimatorComponent;

    for (std::size_t object_index = 0; object_index < scene.GameObjectCount();
        ++object_index)
    {
        ReplayEngine::Core::GameObject* object = scene.GameObjectAt(object_index);
        if (object == nullptr || object->PendingDestroy() ||
            !object->ActiveInHierarchy())
        {
            continue;
        }

        for (std::size_t component_index = 0;
            component_index < object->ComponentCount(); ++component_index)
        {
            ReplayEngine::Core::Component* component =
                object->ComponentAt(component_index);
            if (component == nullptr || component->PendingDestroy() ||
                !component->ActiveInHierarchy() ||
                component->TypeID() != UISpriteAnimatorComponent::StaticTypeID())
            {
                continue;
            }

            static_cast<UISpriteAnimatorComponent*>(component)->UpdateSprite(
                elapsed_time, &motion_mixer);
        }
    }
}

void framework::update_ui_number_displays(ReplayEngine::Scene::Scene& scene)
{
    using ReplayEngine::Components::UITextComponent;

    for (std::size_t object_index = 0;
        object_index < scene.GameObjectCount(); ++object_index)
    {
        ReplayEngine::Core::GameObject* object = scene.GameObjectAt(object_index);
        if (object == nullptr || object->PendingDestroy() ||
            !object->ActiveInHierarchy())
        {
            continue;
        }

        for (std::size_t component_index = 0;
            component_index < object->ComponentCount(); ++component_index)
        {
            ReplayEngine::Core::Component* component =
                object->ComponentAt(component_index);
            if (component == nullptr || component->PendingDestroy() ||
                !component->ActiveInHierarchy() ||
                component->TypeID() != UITextComponent::StaticTypeID())
            {
                continue;
            }

            static_cast<UITextComponent*>(component)->UpdateNumberDisplay(scene);
        }
    }
}
