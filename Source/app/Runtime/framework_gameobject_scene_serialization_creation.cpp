// GameObject / Component 基盤のうち「Default Ground と新規 Scene 生成」を持つ。
// Prefab / Landscape の組み立てと初期状態設定の関数本体はそのまま移動している。
#include "framework.h"

#include "gltf_model.h"
#include "skinned_mesh.h"

#include "../../RePlayEngine/Components/Camera/CameraComponent.h"
#include "../../RePlayEngine/Components/Camera/CameraTargetComponent.h"
#include "../../RePlayEngine/Components/Camera/FollowTargetComponent.h"
#include "../../RePlayEngine/Components/Motion/MotionPlayerComponent.h"
#include "../../RePlayEngine/Components/Core/PropertyLinkComponent.h"
#include "../../RePlayEngine/Components/UI/UIEffectStackComponent.h"
#include "../../RePlayEngine/Components/UI/UISpriteAnimatorComponent.h"
#include "../../RePlayEngine/Components/UI/UITextComponent.h"
#include "../../RePlayEngine/Components/Landscape/LandscapeColliderComponent.h"
#include "../../RePlayEngine/Components/Landscape/LandscapeComponent.h"
#include "../../RePlayEngine/Components/Landscape/LandscapeRendererComponent.h"
#include "../../RePlayEngine/Components/Rendering/LightComponents.h"
#include "../../RePlayEngine/Components/Rendering/MeshRendererComponent.h"
#include "../../RePlayEngine/Components/Rendering/PrimitiveMeshRendererComponent.h"
#include "../../RePlayEngine/Components/Rendering/SkinnedMeshRendererComponent.h"
#include "../../RePlayEngine/Rendering/Shaders/BuiltInShaders.h"
#include "../../RePlayEngine/Rendering/ShaderStack/BuiltInShaderLayers.h"
#include "../../RePlayEngine/Object/Registry/BuiltInComponents.h"
#include "../../RePlayEngine/Project/ProjectSettingsSerializer.h"
#include "../../RePlayEngine/Rendering/Adapter/SceneRenderCollector.h"
#include "../../RePlayEngine/Motion/MotionBindingResolver.h"
#include "../../RePlayEngine/Motion/MotionEvaluator.h"
#include "../../RePlayEngine/UI/UILayout.h"
#include "../../RePlayEngine/Runtime/Events/EventBus.h"
#include "../../RePlayEngine/Scene/Serialization/PrefabSerializer.h"
#include "../../RePlayEngine/Scene/Serialization/SceneData.h"
#include "../../RePlayEngine/Scene/Serialization/SceneSerializer.h"
#include "../../RePlayEngine/Scripting/CSharp/CSharpScriptBackend.h"
#include "../../RePlayEngine/Scripting/Core/ScriptComponent.h"
#include "../../RePlayEngine/Scripting/Core/ScriptRuntime.h"
#include "../../RePlayEngine/Scripting/Core/ScriptTypeCatalog.h"
#include "../../RePlayEngine/Scripting/Core/ScriptTypes.h"
#include "../../game/Behaviours/ValidationBehaviours.h"

#include <commdlg.h>
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

ReplayEngine::Core::GameObject* framework::create_default_landscape_ground(
    ReplayEngine::Scene::Scene& scene)
{
    ReplayEngine::Core::GameObject* ground = scene.CreateGameObject("Ground");
    if (ground == nullptr) return nullptr;

    auto* landscape = ground->AddComponent<ReplayEngine::Components::LandscapeComponent>();
    auto* renderer = ground->AddComponent<ReplayEngine::Components::LandscapeRendererComponent>();
    auto* collider = ground->AddComponent<ReplayEngine::Components::LandscapeColliderComponent>();
    if (landscape == nullptr || renderer == nullptr || collider == nullptr)
    {
        ground->Destroy();
        return nullptr;
    }

    // GenerateFlat 側で geometry を Pivot 中心に生成する。
    landscape->GenerateFlat(33, 33, 2.0f, 0.0f);
    renderer->tint = { 0.36f, 0.48f, 0.31f, 1.0f };
    collider->double_sided = true;
    return ground;
}

bool framework::create_object_scene(const std::string& name, bool place_default_character)
{
    namespace Project = ReplayEngine::Project;

    if (object_editor_context.Dirty())
    {
        request_object_scene_action(place_default_character
            ? object_scene_action::new_default
            : object_scene_action::new_empty);
        return false;
    }

    // 実行中に作り直すと、実行用 Scene と編集用 Scene の対応が壊れる。
    if (object_scene_play_mode) exit_object_play_mode();
    reset_landscape_editor_state(true);

    if (!editor_camera_state_key.empty()) save_editor_camera_state();

    // Scene の中身が総入れ替えになるので、先に衝突世界を切り離す。
    // 古い ObjectID / ColliderID を持ったまま新しい Scene を引かせない。
    detach_collision_world();

    const std::string scene_name = name.empty() ? std::string("新しいシーン") : name;

    // 新規 Scene の既定視点を先に作り、Default Scene だけ後段の LookAt で上書きする。
    editor_camera.ResetToDefault();

    // 1) 空の Scene を作る。GameObject は 1 つも作らない。
    object_scene.Clear();
    object_editor_context.ResetSceneState();
    object_scene.SetName(scene_name);
    object_scene.Services().SetControlledObject(ReplayEngine::Core::ObjectID::Invalid());
    player_control_system.Clear();

    // 選択と Undo 履歴を作り直す。前の Scene の ObjectID を指し続けさせない。
    object_editor_context.AttachScene(&object_scene);

    std::string status = "空のシーンを作成しました";

    // 2) Default Scene は、まず普通の GameObject + Component で Ground を作る。
    //    Empty Scene には何も自動追加しない。
    if (place_default_character)
    {
        ReplayEngine::Core::GameObject* default_ground =
            create_default_landscape_ground(object_scene);
        if (default_ground != nullptr)
        {
            object_editor_context.Selection().Select(default_ground->ID(), false);
            status = "Landscape Ground を含む既定シーンを作成しました";
        }

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
            status = "Landscape Ground を作成しました（既定キャラクター Prefab は未設定）";
        }
        else if (prefab.IsMissing())
        {
            status = "Landscape Ground を作成しました（既定キャラクター Prefab は Missing）";
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

                status = "Landscape Ground + 既定の操作キャラクターを配置しました: " +
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

        // Default Scene の主役は編集可能な Ground。Character Prefab が設定済みでも
        // ControlledObject にするだけで、Inspector/Scene View の選択は Ground に戻す。
        // 起動直後からそのまま Sculpt/Topology を確認できるようにする。
        if (default_ground != nullptr)
        {
            object_editor_context.Selection().Select(default_ground->ID(), false);
            // New Default Scene は新しい作業空間なので Ground が確実に見える視点から始める。
            editor_camera.LookAt({ 0.0f, 30.0f, -42.0f }, { 0.0f, 0.0f, 0.0f });
        }
    }

    // 4) Scene を開始する。ここで初めて OnStart / OnEnable が走る。
    object_scene.Start();

    // 5) 新規 Scene は未保存として開始する。ユーザーが Save / Save As を選ぶまで
    //    既存 Asset を上書きせず、ファイルも自動生成しない。
    object_scene_path.clear();
    object_scene_asset_guid.clear();
    object_editor_context.SetScenePath(object_scene_path);
    object_editor_context.MarkDirty();
    object_recovery_available = false;
    object_autosave_elapsed = 0.0f;

    // 6) 衝突世界を新しい Scene へつなぎ直す。
    attach_collision_world(object_scene);

    // 7) Runtime Camera / CameraTargetComponent には触れない。
    //    Default Scene の Ground 用 LookAt は上書きせず維持する。
    editor_camera_state_key = make_editor_camera_state_key();

    // Default Scene は Ground を選択したまま既定カメラを維持する。
    // 巨大な Ground 全体へ Focus すると遠ざかりすぎ、Character へ Focus すると
    // Sculpt の導線が切れるため、自動 Focus はしない。
    save_editor_camera_state();

    object_editor_context.SetStatus(status + "（未保存）");
    return true;
}
