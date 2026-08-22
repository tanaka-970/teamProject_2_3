// GameObject / Component 基盤のうち「Runtime Scene 更新・Light同期・Camera Follow」を持つ。
// 関数本体は分割前のまま移動し、フレーム更新の順序と分岐は変更しない。
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

void framework::update_object_scene(float elapsed_time)
{
    // Scene 遷移はフレームの先頭で進める。
    //
    // ここが「World を入れ替えてよい安全点」。
    // Update / Trigger の最中に入れ替えると、走査中の配列と実体が同時に消える。
    // SceneTransitionBehaviour が OnTriggerEnter で出した要求も、
    // 次のフレームのこの位置で初めて実際の切り替えになる。
    {
        REPLAY_PROFILE_SCOPE("SceneFlow");
        tick_runtime_scene_flow();
    }
    {
        REPLAY_PROFILE_SCOPE("HotReload/CSharp");
        poll_csharp_script_changes(elapsed_time);
    }

    // .hlsl の保存もここで拾う。
    //
    // C# と同じ位置に置く理由は同じ。描画の最中に
    // バイトコードを差し替えないための同期点がここだから。
    {
        REPLAY_PROFILE_SCOPE("HotReload/Shader");
        poll_shader_source_changes(elapsed_time);
    }

    // スクリプトの同期点。
    //
    // ここは World を入れ替えてよい安全点と同じ位置で、
    // Update / Trigger のどれも走っていない。
    // Schema の差し替え（ホットリロード）はこの 1 か所だけで行う。
    //
    // 【これは第二の更新経路ではない】
    //   ライフサイクル Callback を 1 つも呼ばない。
    //   Component の有効・無効も削除予約も見ない。
    //   やるのは Schema の差し替えと Field 値の移送だけ。
    //   スクリプトの Update は Scene::Update -> ScriptComponent::OnUpdate の
    //   1 本しか存在しない。
    if (object_script_runtime)
    {
        {
            REPLAY_PROFILE_SCOPE("Script/SchemaSwap");
            object_script_runtime->ApplyPendingSchemaSwaps(elapsed_time);
        }

        // 抑制済みのぶんだけがここへ来る。同じエラーが毎フレーム出ても
        // ログは埋まらない（最初の 5 回 -> 以降 1 秒ごとの集約）。
        for (const std::string& line : object_script_runtime->DrainPendingLogLines())
        {
            log_shutdown_reason(line.c_str());
        }
    }

    // ゲーム時間と実時間をここで一度だけ分ける。
    // C# hot reload / Shader監視 / Editor は実時間、Scene と既定 Motion はゲーム時間。
    const float unscaled_delta_time = (std::max)(0.0f, elapsed_time);
    const float safe_time_scale = (std::max)(0.0f, (std::min)(100.0f, object_time_scale));
    const float scaled_delta_time = unscaled_delta_time * safe_time_scale;

    // Runtime API 側へ時間を渡す。World が入れ替わっても接続は残る。
    if (object_runtime_context)
    {
        ReplayEngine::Runtime::RuntimeTime runtime_time;
        runtime_time.delta_time = scaled_delta_time;
        runtime_time.unscaled_delta_time = unscaled_delta_time;
        runtime_time.fixed_delta_time = object_fixed_time_step;
        runtime_time.time_scale = safe_time_scale;
        runtime_time.frame_index = object_runtime_frame_index;
        object_runtime_context->SetTime(runtime_time);
    }
    ++object_runtime_frame_index;

    refresh_object_scene_services();

    ReplayEngine::Scene::Scene& scene = active_object_scene();

    // Motion Editor は Play 中でなくても Property 一覧を使う。
    // Material / Custom Effect の動的 Schema は毎フレームここで同期する。
    {
        REPLAY_PROFILE_SCOPE("Motion/PrepareBindings");
        prepare_material_motion_bindings(scene);
    }
    {
        REPLAY_PROFILE_SCOPE("UI/EffectSchemas");
        prepare_ui_effect_shader_schemas(scene);
    }

    // Editor では F5 Play Session 以外 Component を更新しない。
    // Editor で置いた GameObject が編集中に勝手に動くのを防ぐ。
    if (object_runtime_active())
    {
        // 順序: Update（入力・意思決定）→ FixedUpdate（物理）→ LateUpdate → カメラ
        // 入力は Update で読み、FixedUpdate がその値を消費する。
        {
            REPLAY_PROFILE_SCOPE("Components/Update");
            scene.Update(scaled_delta_time);
        }
        {
            REPLAY_PROFILE_SCOPE("Physics/FixedStep");
            update_object_fixed_step(scaled_delta_time);
        }
        {
            REPLAY_PROFILE_SCOPE("Components/LateUpdate");
            scene.LateUpdate(scaled_delta_time);
        }

        // 位置が確定してから Trigger を判定する。
        // FixedUpdate の途中で判定すると、まだ押し戻されていない位置で
        // Enter が出てしまい、次のフレームですぐ Exit になる。
        {
            REPLAY_PROFILE_SCOPE("Physics/Triggers");
            dispatch_collision_triggers();
        }

        // Behaviour への Collision 配送。Trigger とは経路が完全に分かれている。
        {
            REPLAY_PROFILE_SCOPE("Physics/CollisionEvents");
            object_collision_events.Dispatch(scene, object_runtime_frame_index);
        }

        // Behaviour が積んだイベントと遅延生成を、この同期点で流し切る。
        if (object_runtime_context)
        {
            object_runtime_context->Events().Dispatch(&object_runtime_context->Resolver());
            object_runtime_context->FlushDeferredOperations();
        }

        // Transform が確定してからカメラを動かす。
        {
            REPLAY_PROFILE_SCOPE("CameraFollow");
            update_object_camera_follow(scaled_delta_time);
        }
        {
            REPLAY_PROFILE_SCOPE("Audio");
            object_audio_system.UpdateFromScene(scene);
        }
    }
    else
    {
        object_audio_system.StopAll();
        // 停止中でも生成・削除の予約だけは反映する。
        // Editor 操作の結果が次のフレームまで残らないようにするため。
        scene.ProcessPendingOperations();
    }

    // 選択が消えた GameObject を指し続けないようにする。
    object_editor_context.Selection().PruneMissing(scene);

    // UI の状態変化は Motion 評価より前に確定する。
    // UpdateButtons は ButtonStateChanged を EventBus へ積むため、ここで
    // 一度配送してから Motion を評価すると、押下／ホバーが同じフレームに反映される。
    const object_ui_viewport ui_viewport = object_ui_viewport_target();
    const float ui_logical_width = (std::max)(1.0f, ui_viewport.logical_width);
    const float ui_logical_height = (std::max)(1.0f, ui_viewport.logical_height);
    {
        REPLAY_PROFILE_SCOPE("UI/LayoutHitTest");
        ReplayEngine::UI::UILayout::Resolve(scene,
            ui_logical_width, ui_logical_height);
    }
    POINT mouse{ game_input.PointerScreenX(), game_input.PointerScreenY() };
    ScreenToClient(hwnd, &mouse);
    const float viewport_width = (std::max)(1.0f, ui_viewport.width);
    const float viewport_height = (std::max)(1.0f, ui_viewport.height);
    const float mouse_x = (static_cast<float>(mouse.x) - ui_viewport.left) *
        (ui_logical_width / viewport_width);
    const float mouse_y = ui_logical_height -
        ((static_cast<float>(mouse.y) - ui_viewport.top) *
            (ui_logical_height / viewport_height));
    const bool mouse_down = game_input.Held("PrimaryClick");
    const bool mouse_pressed = mouse_down && !ui_pointer_down_last;
    const bool mouse_released = !mouse_down && ui_pointer_down_last;
    bool input_captured = false;
#ifdef USE_IMGUI
    if (editor_mode && ImGui::GetCurrentContext())
        input_captured = ImGui::GetIO().WantCaptureMouse && !scene_view_hovered;
#endif
    {
        REPLAY_PROFILE_SCOPE("UI/InputNavigation");
        ReplayEngine::UI::UILayout::UpdateButtons(scene,
            ui_logical_width, ui_logical_height,
            mouse_x, mouse_y, mouse_down, mouse_pressed, mouse_released,
            ui_mouse_wheel_delta, input_captured, object_runtime_active());
    }
    ui_mouse_wheel_delta = 0.0f;
    ui_pointer_down_last = mouse_down;

    if (object_runtime_active())
    {
        if (object_runtime_context)
        {
            object_runtime_context->Events().Dispatch(
                &object_runtime_context->Resolver());
        }

        // 順序: Scene::Update -> UI状態／トリガー -> Motion Mixer -> UI Layout -> Render。
        // Motion は Component::OnUpdate からは評価しない。全 Player の寄与を先に集め、
        // 同じ property へ setter を 1 フレーム 1 回だけ呼ぶため、この外部フェーズで扱う。
        {
            REPLAY_PROFILE_SCOPE("Motion/Evaluate");
            evaluate_motion_players(scene, scaled_delta_time, unscaled_delta_time);
        }
        {
            REPLAY_PROFILE_SCOPE("PropertyLink");
            ReplayEngine::Components::PropertyLinkComponent::EvaluateAll(
                scene, unscaled_delta_time);
        }
        {
            REPLAY_PROFILE_SCOPE("UI/NumberDisplay");
            update_ui_number_displays(scene);
        }
        // UI sprite animation は Pause Menu / Loading 表示を止めないため実時間。
        {
            REPLAY_PROFILE_SCOPE("UI/SpriteAnimation");
            update_ui_sprite_animators(scene, unscaled_delta_time);
        }
        if (object_runtime_context)
        {
            object_runtime_context->Events().Dispatch(
                &object_runtime_context->Resolver());
            object_runtime_context->FlushDeferredOperations();
        }
    }

    // Motion が RectTransform を書いた後に最終レイアウトを確定する。
    // 先頭の Resolve は hit test 用、こちらが描画用の正本になる。
    {
        REPLAY_PROFILE_SCOPE("UI/LayoutFinal");
        ReplayEngine::UI::UILayout::Resolve(scene,
            ui_logical_width, ui_logical_height);
    }
    {
        REPLAY_PROFILE_SCOPE("LightSync");
        sync_object_lights();
    }

    // 削除済み Landscape の GPU メッシュをフレーム更新時に 1 回だけ解放する。
    // 非表示なだけの Landscape は再表示時の再生成を避けるためキャッシュへ残す。
    for (auto it = landscape_gpu_mesh_cache.begin(); it != landscape_gpu_mesh_cache.end();)
    {
        const ReplayEngine::Core::GameObject* object = scene.FindGameObjectByID(
            ReplayEngine::Core::ObjectID{ it->first });
        const bool still_owned = object != nullptr && !object->PendingDestroy() &&
            object->GetComponent<ReplayEngine::Components::LandscapeComponent>() != nullptr &&
            object->GetComponent<ReplayEngine::Components::LandscapeRendererComponent>() != nullptr;

        if (!still_owned)
            it = landscape_gpu_mesh_cache.erase(it);
        else
            ++it;
    }

    if (!standalone_game_mode && !object_scene_play_mode &&
        object_editor_context.Dirty())
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
    directional_light_present = false;
    directional_light_is_preview = false;
    directional_shadow_enabled = false;

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
                // 影を出すかはこの Light の設定が正本。
                // 全体設定 (enable_dynamic_shadows / csm_enabled_setting) は
                // 上限としてだけ効き、Light 側の意思をここで上書きしない。
                directional_shadow_enabled = light->cast_shadows;
                pbr.light.shadow_params.w = light->cast_shadows ? 1.0f : 0.0f;
                directional_light_present = true;
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
        // Scene View は配置・選択のための編集画面なので、Light がまだ無い
        // 空Sceneでも PBR Mesh の形が分かる最低限の補助光を使う。
        // Play/GameではSceneの照明設定を尊重し、Lightなしなら従来どおり暗闇にする。
        const bool editor_preview_light =
            editor_mode && edit_mode_active && !object_scene_play_mode;
        if (editor_preview_light)
        {
            light_direction = { 0.35f, -1.0f, 0.25f, 0.0f };
            pbr.light.directional_color = { 1.0f, 0.98f, 0.94f, 1.25f };
            directional_light_is_preview = true;
            // Light をまだ置いていない Scene でも、物を動かせば影が付いてくる。
            // 編集中の手応えのための表示専用の光で、Scene には保存されない。
            // Play / Standalone はこの分岐へ来ないので、実行時の見た目は
            // 従来どおり Scene の照明設定だけで決まる。
            directional_shadow_enabled = editor_preview_light_casts_shadows;
        }
        else
        {
            pbr.light.directional_color = { 0.0f, 0.0f, 0.0f, 0.0f };
        }
        // 旧 PBR 単一シャドウマップはプレビュー光では使わない。
        // プレビュー光の影は CSM 側だけで出す。
        pbr.light.shadow_params.w = 0.0f;
    }
}

void framework::update_object_camera_follow(float elapsed_time)
{
    if (game_scene == nullptr) return;

    const ReplayEngine::Scene::Scene& scene = active_object_scene();
    if (ReplayEngine::Components::ResolveActiveCameraSelection(scene).Valid())
    {
        return;
    }

    // CameraComponent を持たない旧シーンは保存データを書き換えず、既存の
    // SceneGame カメラ経路へ戻す。新しい追従制御は FollowTargetComponent が担当する。
    const ReplayEngine::Core::ObjectID controlled = scene.Services().ControlledObject();

    const ReplayEngine::Components::CameraTargetSelection selection =
        ReplayEngine::Components::ResolveCameraTargetSelection(scene, controlled);

    if (!selection.Valid())
    {
        game_scene->Gameplay().UpdateFreeCamera(elapsed_time, game_input);
        return;
    }

    const DirectX::XMFLOAT3 world = selection.object->GetTransform().WorldPosition();
    const ReplayEngine::Components::CameraTargetComponent& camera_target = *selection.component;
    const DirectX::XMFLOAT3 anchor{
        world.x + camera_target.target_offset.x,
        world.y + camera_target.target_offset.y,
        world.z + camera_target.target_offset.z };

    game_scene->Gameplay().FollowCameraTarget(
        anchor,
        camera_target.look_at_offset,
        6.5f,
        2.25f,
        12.0f,
        50.0f,
        0.1f,
        10000.0f,
        elapsed_time,
        game_input);
}
// ---------------------------------------------------------------------------
//
// 【編集カメラと Runtime Camera の関係】
//   Play 開始時に編集カメラの値を Runtime Camera へ写さない。
//   Play 終了時に Runtime Camera の値を編集カメラへ取り込まない。
//   editor_camera は Play の出入りで一切書き換えられないので、
//   Play 前の視点がそのまま残る。切り替わるのは
//   「描画にどちらの行列を使うか」だけ（using_editor_camera）。

