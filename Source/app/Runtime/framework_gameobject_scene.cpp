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

#include "../../RePlayEngine/Components/Camera/CameraComponent.h"
#include "../../RePlayEngine/Components/Camera/CameraTargetComponent.h"
#include "../../RePlayEngine/Components/Camera/FollowTargetComponent.h"
#include "../../RePlayEngine/Components/Motion/MotionPlayerComponent.h"
#include "../../RePlayEngine/Components/UI/UIEffectStackComponent.h"
#include "../../RePlayEngine/Components/UI/UISpriteAnimatorComponent.h"
#include "../../RePlayEngine/Components/Rendering/LightComponents.h"
#include "../../RePlayEngine/Components/Rendering/MeshRendererComponent.h"
#include "../../RePlayEngine/Components/Rendering/PrimitiveMeshRendererComponent.h"
#include "../../RePlayEngine/Components/Rendering/SkinnedMeshRendererComponent.h"
#include "../../RePlayEngine/Components/Landscape/LandscapeComponent.h"
#include "../../RePlayEngine/Components/Landscape/LandscapeRendererComponent.h"
#include "../../RePlayEngine/Components/Landscape/LandscapeColliderComponent.h"
#include "../../RePlayEngine/Rendering/Shaders/BuiltInShaders.h"
#include "../../RePlayEngine/Rendering/ShaderStack/BuiltInShaderLayers.h"
#include "../../RePlayEngine/Object/Registry/BuiltInComponents.h"
#include "../../RePlayEngine/Project/ProjectSettingsSerializer.h"
#include "../../RePlayEngine/Rendering/Adapter/SceneRenderCollector.h"
#include "../../RePlayEngine/Motion/MotionBindingResolver.h"
#include "../../RePlayEngine/Motion/MotionEvaluator.h"
#include "../../RePlayEngine/UI/UILayout.h"

// RuntimeContext.h は EventBus を前方宣言だけしている。
// Events() の戻り値へ Dispatch() を呼ぶには完全型が要るので、
// 使う .cpp 側でだけ include する。
// framework.h や RuntimeContext.h へ広げると、EventBus を使わない
// 翻訳単位まで巻き込むことになる。
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

    using PrimitiveVertex = static_mesh::vertex;

    void AppendPrimitiveQuad(std::vector<PrimitiveVertex>& vertices,
        std::vector<std::uint32_t>& indices,
        const DirectX::XMFLOAT3& a, const DirectX::XMFLOAT3& b,
        const DirectX::XMFLOAT3& c, const DirectX::XMFLOAT3& d,
        const DirectX::XMFLOAT3& normal)
    {
        const std::uint32_t base = static_cast<std::uint32_t>(vertices.size());
        vertices.push_back({ a, normal, { 0.0f, 1.0f } });
        vertices.push_back({ b, normal, { 0.0f, 0.0f } });
        vertices.push_back({ c, normal, { 1.0f, 0.0f } });
        vertices.push_back({ d, normal, { 1.0f, 1.0f } });
        indices.insert(indices.end(), { base, base + 1, base + 2, base, base + 2, base + 3 });
    }

    bool BuildBuiltinPrimitive(const std::string& id,
        std::vector<PrimitiveVertex>& vertices, std::vector<std::uint32_t>& indices)
    {
        using namespace DirectX;
        vertices.clear();
        indices.clear();

        if (id == "builtin:plane")
        {
            AppendPrimitiveQuad(vertices, indices,
                { -0.5f, 0.0f, -0.5f }, { -0.5f, 0.0f, 0.5f },
                { 0.5f, 0.0f, 0.5f }, { 0.5f, 0.0f, -0.5f },
                { 0.0f, 1.0f, 0.0f });
            return true;
        }
        if (id == "builtin:quad")
        {
            AppendPrimitiveQuad(vertices, indices,
                { -0.5f, -0.5f, 0.0f }, { -0.5f, 0.5f, 0.0f },
                { 0.5f, 0.5f, 0.0f }, { 0.5f, -0.5f, 0.0f },
                { 0.0f, 0.0f, -1.0f });
            return true;
        }
        if (id == "builtin:cube")
        {
            const float h = 0.5f;
            AppendPrimitiveQuad(vertices, indices, { -h,-h,-h }, { -h, h,-h }, { h, h,-h }, { h,-h,-h }, { 0,0,-1 });
            AppendPrimitiveQuad(vertices, indices, { h,-h, h }, { h, h, h }, { -h, h, h }, { -h,-h, h }, { 0,0,1 });
            AppendPrimitiveQuad(vertices, indices, { -h,-h, h }, { -h, h, h }, { -h, h,-h }, { -h,-h,-h }, { -1,0,0 });
            AppendPrimitiveQuad(vertices, indices, { h,-h,-h }, { h, h,-h }, { h, h, h }, { h,-h, h }, { 1,0,0 });
            AppendPrimitiveQuad(vertices, indices, { -h, h,-h }, { -h, h, h }, { h, h, h }, { h, h,-h }, { 0,1,0 });
            AppendPrimitiveQuad(vertices, indices, { -h,-h, h }, { -h,-h,-h }, { h,-h,-h }, { h,-h, h }, { 0,-1,0 });
            return true;
        }

        const bool sphere = id == "builtin:sphere";
        const bool capsule = id == "builtin:capsule";
        if (sphere || capsule)
        {
            constexpr int slices = 24;
            constexpr int stacks = 16;
            const float radius = 0.5f;
            const float capsule_half_cylinder = capsule ? 0.5f : 0.0f;
            for (int stack = 0; stack <= stacks; ++stack)
            {
                const float v = static_cast<float>(stack) / stacks;
                const float latitude = -XM_PIDIV2 + v * XM_PI;
                const float cos_lat = std::cos(latitude);
                const float sin_lat = std::sin(latitude);
                for (int slice = 0; slice <= slices; ++slice)
                {
                    const float u = static_cast<float>(slice) / slices;
                    const float longitude = u * XM_2PI;
                    XMFLOAT3 normal{ cos_lat * std::cos(longitude), sin_lat,
                        cos_lat * std::sin(longitude) };
                    XMFLOAT3 position{ normal.x * radius, normal.y * radius,
                        normal.z * radius };
                    if (capsule)
                        position.y += normal.y >= 0.0f ? capsule_half_cylinder : -capsule_half_cylinder;
                    vertices.push_back({ position, normal, { u, 1.0f - v } });
                }
            }
            const int stride = slices + 1;
            for (int stack = 0; stack < stacks; ++stack)
            {
                for (int slice = 0; slice < slices; ++slice)
                {
                    const std::uint32_t a = static_cast<std::uint32_t>(stack * stride + slice);
                    const std::uint32_t b = a + 1;
                    const std::uint32_t c = a + stride;
                    const std::uint32_t d = c + 1;
                    indices.insert(indices.end(), { a, c, b, b, c, d });
                }
            }
            return true;
        }

        if (id == "builtin:cylinder")
        {
            constexpr int slices = 24;
            const float radius = 0.5f;
            const float half_height = 0.5f;
            for (int slice = 0; slice <= slices; ++slice)
            {
                const float u = static_cast<float>(slice) / slices;
                const float angle = u * XM_2PI;
                const XMFLOAT3 normal{ std::cos(angle), 0.0f, std::sin(angle) };
                vertices.push_back({ { normal.x * radius, -half_height, normal.z * radius }, normal, { u,1 } });
                vertices.push_back({ { normal.x * radius, half_height, normal.z * radius }, normal, { u,0 } });
            }
            for (int slice = 0; slice < slices; ++slice)
            {
                const std::uint32_t a = static_cast<std::uint32_t>(slice * 2);
                const std::uint32_t b = a + 1;
                const std::uint32_t c = a + 2;
                const std::uint32_t d = a + 3;
                indices.insert(indices.end(), { a,b,c, c,b,d });
            }
            const std::uint32_t bottom_center = static_cast<std::uint32_t>(vertices.size());
            vertices.push_back({ {0,-half_height,0}, {0,-1,0}, {0.5f,0.5f} });
            const std::uint32_t top_center = static_cast<std::uint32_t>(vertices.size());
            vertices.push_back({ {0,half_height,0}, {0,1,0}, {0.5f,0.5f} });
            const std::uint32_t cap_start = static_cast<std::uint32_t>(vertices.size());
            for (int slice = 0; slice < slices; ++slice)
            {
                const float angle = static_cast<float>(slice) / slices * XM_2PI;
                const float x = std::cos(angle) * radius, z = std::sin(angle) * radius;
                vertices.push_back({ {x,-half_height,z},{0,-1,0},{x+0.5f,z+0.5f} });
                vertices.push_back({ {x, half_height,z},{0, 1,0},{x+0.5f,z+0.5f} });
            }
            for (int slice = 0; slice < slices; ++slice)
            {
                const int next = (slice + 1) % slices;
                const std::uint32_t b0 = cap_start + static_cast<std::uint32_t>(slice * 2);
                const std::uint32_t b1 = cap_start + static_cast<std::uint32_t>(next * 2);
                const std::uint32_t t0 = b0 + 1, t1 = b1 + 1;
                indices.insert(indices.end(), { bottom_center,b1,b0, top_center,t0,t1 });
            }
            return true;
        }
        return false;
    }
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
// プロジェクト設定
// ---------------------------------------------------------------------------

void framework::load_project_settings()
{
    namespace Project = ReplayEngine::Project;

    std::string error;
    const auto path = content_path(Project::ProjectSettingsSerializer::DefaultPath());
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

const ReplayEngine::Motion::MotionAsset* framework::resolve_motion_asset(
    const std::string& asset_guid)
{
    if (asset_guid.empty()) return nullptr;

    auto cached = motion_asset_cache.find(asset_guid);
    if (cached != motion_asset_cache.end()) return &cached->second;

    const ReplayEngine::Assets::AssetRecord* record =
        asset_database.FindByGuid(asset_guid);
    if (record == nullptr || record->kind != ReplayEngine::Assets::AssetKind::Motion)
    {
        if (motion_asset_load_failures.insert(asset_guid).second)
        {
            push_editor_log("Warning",
                "Motion Assetを解決できません: " + asset_guid);
        }
        return nullptr;
    }

    ReplayEngine::Motion::MotionAsset asset;
    std::string error;
    const std::filesystem::path motion_path = content_path(record->source_path);
    if (!ReplayEngine::Motion::MotionAsset::LoadFromFile(motion_path,
        asset, error))
    {
        if (motion_asset_load_failures.insert(asset_guid).second)
        {
            push_editor_log("Warning", error, motion_path);
        }
        return nullptr;
    }

    auto inserted = motion_asset_cache.emplace(asset_guid, std::move(asset));
    return &inserted.first->second;
}

void framework::prepare_material_motion_bindings(ReplayEngine::Scene::Scene& scene)
{
    using ReplayEngine::Components::MeshRendererComponent;
    using ReplayEngine::Components::PrimitiveMeshRendererComponent;
    using ReplayEngine::Components::SkinnedMeshRendererComponent;
    using ReplayEngine::Rendering::MaterialAsset;
    using ReplayEngine::Rendering::ShaderID;
    using ReplayEngine::Rendering::ShaderPropertySchema;

    auto resolve_schema = [this](const MaterialAsset* material)
        -> const ShaderPropertySchema*
    {
        if (material == nullptr || material->shader_guid.empty()) return nullptr;
        ShaderID shader_id;
        if (!ShaderID::TryParse(material->shader_guid, shader_id) || !shader_id.IsValid())
            return nullptr;
        const auto* entry = shader_library.Catalog().Find(shader_id);
        return entry != nullptr && entry->schema ? entry->schema.get() : nullptr;
    };

    for (std::size_t object_index = 0; object_index < scene.GameObjectCount(); ++object_index)
    {
        ReplayEngine::Core::GameObject* object = scene.GameObjectAt(object_index);
        if (object == nullptr || object->PendingDestroy()) continue;

        for (std::size_t component_index = 0;
            component_index < object->ComponentCount(); ++component_index)
        {
            ReplayEngine::Core::Component* component = object->ComponentAt(component_index);
            if (component == nullptr || component->PendingDestroy()) continue;

            if (component->TypeID() == MeshRendererComponent::StaticTypeID())
            {
                auto& renderer = static_cast<MeshRendererComponent&>(*component);
                const MaterialAsset* material = resolve_object_material(renderer.material_asset);
                renderer.PrepareMaterialMotion(material, resolve_schema(material));
            }
            else if (component->TypeID() == SkinnedMeshRendererComponent::StaticTypeID())
            {
                auto& renderer = static_cast<SkinnedMeshRendererComponent&>(*component);
                const MaterialAsset* material = resolve_object_material(renderer.material_asset);
                renderer.PrepareMaterialMotion(material, resolve_schema(material));
            }
            else if (component->TypeID() == PrimitiveMeshRendererComponent::StaticTypeID())
            {
                auto& renderer = static_cast<PrimitiveMeshRendererComponent&>(*component);
                const MaterialAsset* material = resolve_object_material(renderer.material_asset);
                renderer.PrepareMaterialMotion(material, resolve_schema(material));
            }
        }
    }
}

void framework::prepare_ui_effect_shader_schemas(ReplayEngine::Scene::Scene& scene)
{
    using ReplayEngine::Assets::AssetKind;
    using ReplayEngine::Components::UIEffectStackComponent;
    using ReplayEngine::Rendering::ShaderCatalog;
    using ReplayEngine::Rendering::ShaderDomain;

    const auto normalize = [](std::filesystem::path path)
    {
        std::error_code error;
        std::filesystem::path absolute = path.is_absolute()
            ? path : std::filesystem::absolute(path, error);
        if (error) absolute = path;
        error.clear();
        const std::filesystem::path canonical =
            std::filesystem::weakly_canonical(absolute, error);
        return error ? absolute.lexically_normal() : canonical.lexically_normal();
    };

    for (std::size_t object_index = 0; object_index < scene.GameObjectCount(); ++object_index)
    {
        ReplayEngine::Core::GameObject* object = scene.GameObjectAt(object_index);
        if (object == nullptr || object->PendingDestroy()) continue;
        auto* stack = object->GetComponent<UIEffectStackComponent>();
        if (stack == nullptr) continue;

        for (std::size_t effect_index = 0; effect_index < stack->effects.size(); ++effect_index)
        {
            const ReplayEngine::UI::UIEffect& effect = stack->effects[effect_index];
            ReplayEngine::Rendering::ShaderPropertySchemaRef schema;
            const ReplayEngine::Assets::AssetRecord* record =
                asset_database.FindByGuid(effect.custom_shader);
            if (record != nullptr && record->kind == AssetKind::Shader)
            {
                const std::filesystem::path source = normalize(content_path(record->source_path));
                for (const ShaderCatalog::Entry& entry : shader_library.Catalog().All())
                {
                    if (entry.info.domain != ShaderDomain::PostProcess) continue;
                    if (normalize(entry.info.source_path) != source) continue;
                    schema = entry.schema;
                    break;
                }
            }
            stack->SetCustomShaderSchema(effect_index, std::move(schema));
        }
    }
}

void framework::evaluate_motion_players(ReplayEngine::Scene::Scene& scene,
    float scaled_delta_time, float unscaled_delta_time)
{
    using ReplayEngine::Components::MotionPlayerComponent;
    using ReplayEngine::Motion::MotionBindingResolver;
    using ReplayEngine::Motion::MotionEvaluator;
    using ReplayEngine::Motion::MotionTrack;
    using ReplayEngine::Reflection::PropertyValue;

    auto capture_snapshot =
        [&](const ReplayEngine::Motion::MotionAsset& asset,
            MotionPlayerComponent& player)
    {
        std::vector<MotionPlayerComponent::SnapshotValue> values;
        values.reserve(asset.tracks.size());
        for (const MotionTrack& track : asset.tracks)
        {
            const ReplayEngine::Motion::ResolvedMotionBinding binding =
                MotionBindingResolver::Resolve(scene, track.binding, player.Owner());
            if (!binding.Valid()) continue;
            MotionPlayerComponent::SnapshotValue snapshot;
            snapshot.binding = track.binding;
            snapshot.value = binding.property->Capture(*binding.component);
            values.push_back(std::move(snapshot));
        }
        player.StoreSnapshot(std::move(values));
    };

    auto contribute_restore =
        [&](MotionPlayerComponent& player)
    {
        for (const MotionPlayerComponent::SnapshotValue& snapshot :
            player.SnapshotValues())
        {
            const ReplayEngine::Motion::ResolvedMotionBinding binding =
                MotionBindingResolver::Resolve(scene, snapshot.binding, player.Owner());
            motion_mixer.Contribute(binding, snapshot.value, 1.0f,
                ReplayEngine::Motion::MotionBlendMode::Override);
        }
        player.ConsumeStopRestoreRequest();
    };

    // Event は「current == event.time」で見ない。
    // フレーム間に通過した再生座標を展開し、その区間へ Event が何回入ったかを数える。
    // これにより 1 フレームで複数周しても、Loop/PingPong の端でも取りこぼさない。
    const auto publish_motion_event =
        [&](const ReplayEngine::Motion::MotionEventTrack& track,
            const ReplayEngine::Motion::MotionEvent& event,
            const MotionPlayerComponent& player)
    {
        if (object_runtime_context == nullptr || event.name.empty()) return;

        ReplayEngine::Runtime::ObjectHandle target =
            ReplayEngine::Runtime::ObjectHandle::None();
        if (track.object.Valid())
        {
            target = object_runtime_context->Resolver().FindByObjectID(track.object);
            if (target.IsEmpty()) return;
        }

        ReplayEngine::Runtime::EventRecord record;
        record.type = ReplayEngine::Runtime::EngineEvents::MotionEvent;
        record.type_name = "MotionEvent";
        record.source = object_runtime_context->Resolver().MakeHandle(player.Owner());
        record.target = target;
        record.frame_index = object_runtime_frame_index;
        record.payload.Set("name",
            ReplayEngine::Reflection::PropertyValue::MakeString(event.name));
        record.payload.Set("parameter",
            ReplayEngine::Reflection::PropertyValue::MakeString(event.parameter));
        record.payload.Set("time",
            ReplayEngine::Reflection::PropertyValue::MakeFloat(event.time));
        object_runtime_context->Events().Publish(std::move(record));
    };

    const auto publish_repeated = [](long long first, long long last,
        const auto& callback)
    {
        if (last < first) return;
        for (long long i = first; i <= last; ++i) callback(i);
    };

    auto publish_motion_events =
        [&](const ReplayEngine::Motion::MotionAsset& asset,
            const MotionPlayerComponent& player, float before, float delta_time,
            int direction_before)
    {
        if (object_runtime_context == nullptr || asset.event_tracks.empty() ||
            asset.duration <= 0.0f || delta_time <= 0.0f || player.speed == 0.0f)
        {
            return;
        }

        const double duration = static_cast<double>(asset.duration);
        const int wrap_mode = player.RuntimeWrapMode();
        const double epsilon = 1.0e-9;

        for (const ReplayEngine::Motion::MotionEventTrack& track : asset.event_tracks)
        {
            for (const ReplayEngine::Motion::MotionEvent& event : track.events)
            {
                if (event.name.empty() || event.time < 0.0f ||
                    event.time > asset.duration)
                {
                    continue;
                }

                const double event_time = static_cast<double>(event.time);
                auto emit = [&](long long) { publish_motion_event(track, event, player); };

                if (wrap_mode == MotionPlayerComponent::Loop)
                {
                    const double travel = static_cast<double>(delta_time) *
                        static_cast<double>(player.speed);
                    const double from = static_cast<double>(before);
                    const double to = from + travel;
                    if (travel > 0.0)
                    {
                        const long long first = static_cast<long long>(std::floor(
                            (from - event_time) / duration)) + 1;
                        const long long last = static_cast<long long>(std::floor(
                            (to - event_time + epsilon) / duration));
                        publish_repeated(first, last, emit);
                    }
                    else if (travel < 0.0)
                    {
                        const long long first = static_cast<long long>(std::ceil(
                            (to - event_time - epsilon) / duration));
                        const long long last = static_cast<long long>(std::ceil(
                            (from - event_time) / duration)) - 1;
                        publish_repeated(first, last, emit);
                    }
                    continue;
                }

                if (wrap_mode == MotionPlayerComponent::PingPong)
                {
                    if (direction_before == 0) continue;
                    const double period = duration * 2.0;
                    const double phase_from = direction_before > 0
                        ? static_cast<double>(before)
                        : period - static_cast<double>(before);
                    const double phase_to = phase_from +
                        static_cast<double>(delta_time) * std::fabs(
                            static_cast<double>(player.speed));

                    auto emit_phase_series = [&](double base)
                    {
                        const long long first = static_cast<long long>(std::floor(
                            (phase_from - base) / period)) + 1;
                        const long long last = static_cast<long long>(std::floor(
                            (phase_to - base + epsilon) / period));
                        publish_repeated(first, last, emit);
                    };

                    emit_phase_series(event_time);
                    // 端点は往路/復路が同じ位相になる。二重発火させない。
                    if (event_time > 0.0 && event_time < duration)
                        emit_phase_series(period - event_time);
                    continue;
                }

                // Once / ClampForever は端をまたがないので単一区間。
                const double travel = static_cast<double>(delta_time) *
                    static_cast<double>(player.speed);
                double after = static_cast<double>(before) + travel;
                after = (std::max)(0.0, (std::min)(duration, after));
                if (travel > 0.0)
                {
                    if (event_time > static_cast<double>(before) &&
                        event_time <= after + epsilon)
                    {
                        emit(0);
                    }
                }
                else if (travel < 0.0)
                {
                    if (event_time < static_cast<double>(before) &&
                        event_time + epsilon >= after)
                    {
                        emit(0);
                    }
                }
            }
        }
    };

    motion_mixer.BeginFrame();

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
                component->TypeID() != MotionPlayerComponent::StaticTypeID())
            {
                continue;
            }

            MotionPlayerComponent& player =
                static_cast<MotionPlayerComponent&>(*component);

            const ReplayEngine::Motion::MotionAsset* asset =
                resolve_motion_asset(player.motion.guid);
            if (asset == nullptr) continue;

            const float player_delta_time = player.ignore_time_scale
                ? unscaled_delta_time : scaled_delta_time;
            player.AdvanceTriggerDelay(player_delta_time);

            if (player.HasStopRestoreRequest())
            {
                contribute_restore(player);
                continue;
            }

            if (!player.ShouldContribute()) continue;

            if (player.NeedsSnapshot())
            {
                capture_snapshot(*asset, player);
            }

            const float previous_motion_time = player.time;
            const int previous_playback_direction = player.PlaybackDirection();
            player.Advance(asset->duration, player_delta_time);
            publish_motion_events(*asset, player, previous_motion_time, player_delta_time,
                previous_playback_direction);
            if (player.HasStopRestoreRequest())
            {
                contribute_restore(player);
                continue;
            }

            const float blend_alpha = player.BlendInAlpha();
            for (const MotionTrack& track : asset->tracks)
            {
                PropertyValue value;
                if (!MotionEvaluator::EvaluateTrack(track, player.time, value))
                    continue;

                const ReplayEngine::Motion::ResolvedMotionBinding binding =
                    MotionBindingResolver::Resolve(scene, track.binding, player.Owner());
                if (!binding.Valid()) continue;

                if (blend_alpha < 1.0f)
                {
                    if (const PropertyValue* base = player.SnapshotFor(track.binding))
                    {
                        value = PropertyValue::Lerp(*base, value, blend_alpha);
                    }
                }
                motion_mixer.Contribute(binding, value, player.weight,
                    track.blend_mode);
            }
        }
    }

    motion_mixer.Apply();
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

void framework::update_object_scene(float elapsed_time)
{
    // Scene 遷移はフレームの先頭で進める。
    //
    // ここが「World を入れ替えてよい安全点」。
    // Update / Trigger の最中に入れ替えると、走査中の配列と実体が同時に消える。
    // SceneTransitionBehaviour が OnTriggerEnter で出した要求も、
    // 次のフレームのこの位置で初めて実際の切り替えになる。
    tick_runtime_scene_flow();
    poll_csharp_script_changes(elapsed_time);

    // .hlsl の保存もここで拾う。
    //
    // C# と同じ位置に置く理由は同じ。描画の最中に
    // バイトコードを差し替えないための同期点がここだから。
    poll_shader_source_changes(elapsed_time);

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
        object_script_runtime->ApplyPendingSchemaSwaps(elapsed_time);

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
    prepare_material_motion_bindings(scene);
    prepare_ui_effect_shader_schemas(scene);

    // Editor では F5 Play Session 以外 Component を更新しない。
    // Editor で置いた GameObject が編集中に勝手に動くのを防ぐ。
    if (object_runtime_active())
    {
        // 順序: Update（入力・意思決定）→ FixedUpdate（物理）→ LateUpdate → カメラ
        // 入力は Update で読み、FixedUpdate がその値を消費する。
        scene.Update(scaled_delta_time);
        update_object_fixed_step(scaled_delta_time);
        scene.LateUpdate(scaled_delta_time);

        // 位置が確定してから Trigger を判定する。
        // FixedUpdate の途中で判定すると、まだ押し戻されていない位置で
        // Enter が出てしまい、次のフレームですぐ Exit になる。
        dispatch_collision_triggers();

        // Behaviour への Collision 配送。Trigger とは経路が完全に分かれている。
        object_collision_events.Dispatch(scene, object_runtime_frame_index);

        // Behaviour が積んだイベントと遅延生成を、この同期点で流し切る。
        if (object_runtime_context)
        {
            object_runtime_context->Events().Dispatch(&object_runtime_context->Resolver());
            object_runtime_context->FlushDeferredOperations();
        }

        // Transform が確定してからカメラを動かす。
        update_object_camera_follow(scaled_delta_time);
        object_audio_system.UpdateFromScene(scene);
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
    ReplayEngine::UI::UILayout::Resolve(scene,
        ui_logical_width, ui_logical_height);
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
    ReplayEngine::UI::UILayout::UpdateButtons(scene,
        ui_logical_width, ui_logical_height,
        mouse_x, mouse_y, mouse_down, mouse_pressed, mouse_released, input_captured,
        object_runtime_active());
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
        evaluate_motion_players(scene, scaled_delta_time, unscaled_delta_time);
        // UI sprite animation は Pause Menu / Loading 表示を止めないため実時間。
        update_ui_sprite_animators(scene, unscaled_delta_time);
        if (object_runtime_context)
        {
            object_runtime_context->Events().Dispatch(
                &object_runtime_context->Resolver());
            object_runtime_context->FlushDeferredOperations();
        }
    }

    // Motion が RectTransform を書いた後に最終レイアウトを確定する。
    // 先頭の Resolve は hit test 用、こちらが描画用の正本になる。
    ReplayEngine::UI::UILayout::Resolve(scene,
        ui_logical_width, ui_logical_height);
    sync_object_lights();

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
        // Scene View は配置・選択のための編集画面なので、Light がまだ無い
        // 空Sceneでも PBR Mesh の形が分かる最低限の補助光を使う。
        // Play/GameではSceneの照明設定を尊重し、Lightなしなら従来どおり暗闇にする。
        const bool editor_preview_light =
            editor_mode && edit_mode_active && !object_scene_play_mode;
        if (editor_preview_light)
        {
            light_direction = { 0.35f, -1.0f, 0.25f, 0.0f };
            pbr.light.directional_color = { 1.0f, 0.98f, 0.94f, 1.25f };
        }
        else
        {
            pbr.light.directional_color = { 0.0f, 0.0f, 0.0f, 0.0f };
        }
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

    const std::filesystem::path previous_path = object_scene_path;
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
    register_object_scene_asset();
    add_recent_object_scene(object_scene_path);
    std::error_code remove_error;
    if (!previous_path.empty())
        std::filesystem::remove(AutosavePathFor(previous_path), remove_error);
    remove_error.clear();
    std::filesystem::remove(AutosavePathFor(object_scene_path), remove_error);
    object_recovery_available = false;
    editor_camera_state_key = make_editor_camera_state_key();
    save_editor_camera_state();
    save_editor_session();
    object_editor_context.SetStatus("保存しました: " + object_scene_path.filename().string());
    return true;
}

bool framework::load_object_scene(bool choose_path)
{
    std::filesystem::path source = object_scene_path;
    if (choose_path)
    {
        source = BrowseObjectSceneFile(hwnd, false, source);
        if (source.empty())
        {
            object_editor_context.SetStatus("読み込みをキャンセルしました");
            return false;
        }
        if (object_editor_context.Dirty())
        {
            request_object_scene_action(object_scene_action::open_path, source);
            return false;
        }
    }

    return load_object_scene_from_path(source);
}

bool framework::load_object_scene_from_path(const std::filesystem::path& source)
{
    if (source.empty())
    {
        object_editor_context.SetStatus("読み込むシーンが指定されていません");
        return false;
    }
    if (object_scene_play_mode) exit_object_play_mode();
    reset_landscape_editor_state(true);

    // Scene を切り替える前に、今の Scene の編集カメラ状態を残す。
    // 戻ってきたときに同じ視点から再開できる。
    if (!editor_camera_state_key.empty()) save_editor_camera_state();

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
    object_scene_path = source;
    object_editor_context.SetScenePath(object_scene_path);
    object_editor_context.ClearDirty();
    register_object_scene_asset();
    add_recent_object_scene(object_scene_path);

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
    if (!standalone_game_mode) load_editor_camera_state();
    if (!standalone_game_mode) check_object_scene_recovery();
    save_editor_session();
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

void framework::register_object_scene_asset()
{
    object_scene_asset_guid.clear();
    if (object_scene_path.empty()) return;

    const ReplayEngine::Assets::AssetRecord& record = asset_database.Register(
        object_scene_path, ReplayEngine::Assets::AssetKind::Scene);
    object_scene_asset_guid = record.guid;
    std::string error;
    if (!asset_database.Save(error))
        OutputDebugStringA(("[Scene] AssetDatabase save failed: " + error + "\n").c_str());
}

void framework::add_recent_object_scene(const std::filesystem::path& path)
{
    if (path.empty()) return;
    const std::filesystem::path normalized =
        ReplayEngine::Assets::AssetDatabase::NormalizeProjectPath(path);
    std::string key = normalized.generic_u8string();
    std::transform(key.begin(), key.end(), key.begin(),
        [](unsigned char value) { return static_cast<char>(std::tolower(value)); });

    recent_scene_paths.erase(std::remove_if(recent_scene_paths.begin(), recent_scene_paths.end(),
        [&key](const std::filesystem::path& candidate)
        {
            std::string candidate_key = ReplayEngine::Assets::AssetDatabase::
                NormalizeProjectPath(candidate).generic_u8string();
            std::transform(candidate_key.begin(), candidate_key.end(), candidate_key.begin(),
                [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
            return candidate_key == key;
        }), recent_scene_paths.end());
    recent_scene_paths.insert(recent_scene_paths.begin(), normalized);
    constexpr std::size_t maximum_recent_scenes = 10;
    if (recent_scene_paths.size() > maximum_recent_scenes)
        recent_scene_paths.resize(maximum_recent_scenes);
}

void framework::discard_object_scene_autosave()
{
    std::error_code error;
    std::filesystem::remove(AutosavePathFor(object_scene_path), error);
    object_recovery_available = false;
    object_autosave_elapsed = 0.0f;
    object_autosave_status.clear();
}

void framework::request_object_scene_action(object_scene_action action,
    std::filesystem::path path)
{
    // 新規 / 開く / 終了 はどれも編集シーンに対する操作。
    // Play 中のまま進むと save_object_scene が
    // 「実行中はシーンを保存できません」で false を返し、
    // 「保存して終了」を押しても何も起きず終了できなくなる。
    // 先に実行を止めてから確認へ進む。
    if (object_scene_play_mode) exit_object_play_mode();

    pending_object_scene_action = action;
    pending_object_scene_path = std::move(path);
    if (object_editor_context.Dirty())
    {
        object_scene_unsaved_prompt_requested = true;
        editor_mode = true;
        set_edit_mode(true);
        return;
    }
    execute_pending_object_scene_action();
}

void framework::execute_pending_object_scene_action()
{
    const object_scene_action action = pending_object_scene_action;
    const std::filesystem::path path = pending_object_scene_path;
    pending_object_scene_action = object_scene_action::none;
    pending_object_scene_path.clear();

    switch (action)
    {
    case object_scene_action::new_empty:
        create_object_scene(u8"新しいシーン", false);
        break;
    case object_scene_action::new_default:
        create_object_scene(u8"新しいシーン", true);
        break;
    case object_scene_action::open_path:
        load_object_scene_from_path(path);
        break;
    case object_scene_action::exit_application:
        PostMessage(hwnd, WM_CLOSE, 0, 0);
        break;
    default:
        break;
    }
}

bool framework::confirm_object_scene_close()
{
    // ユーザーが既に「保存して終了 / 破棄して終了」を選んでいる。
    // ここで Dirty を見直すと、確認のあとに何かが Dirty を立て直しただけで
    // 終了できなくなる。選んだ結果を尊重してそのまま閉じる。
    if (object_exit_confirmed) return true;

    if (!object_editor_context.Dirty()) return true;
    request_object_scene_action(object_scene_action::exit_application);
    return false;
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
    // 呼ばれたこと自体を必ず残す。
    // 「Play を押しても何も起きない」ときに、ボタンが繋がっていないのか
    // 中で弾かれているのかを切り分けられないと追えない。
    push_editor_log("Info", "Play 開始要求を受けました");

    if (object_scene_play_mode)
    {
        push_editor_log("Info", "既に Play 中のため何もしません");
        return;
    }

    reset_landscape_editor_state(true);

    // 編集 Scene の内容を実行用 Scene へ複製する。
    // 直接コピーせず SceneData を経由するのは、
    // Scene が生ポインタで結ばれておりコピー不可なため。
    // これにより Play 中の変更が編集 Scene へ戻らないことが構造的に保証される。
    initialize_runtime_services();

    // 編集 Scene の内容を RuntimeSceneService へ渡す。
    //
    // framework が自分で Scene を組み立てて持たない理由:
    //   持つと Runtime World の所有者が 2 つになる。どちらが本物かが
    //   場所ごとに変わり、Scene 切り替えのたびに食い違う。
    //   組み立ても入れ替えもサービス側の 1 経路へ寄せる。
    //
    // SceneData を経由するのは Scene がコピー不可なため。
    // これにより Play 中の変更が編集 Scene へ戻らないことが構造的に保証される。
    SceneSerialization::SceneData snapshot;
    SceneSerialization::CaptureScene(object_scene, snapshot);

    // Play From Here は SceneData の実行用コピーだけへ焼き込む。
    // Runtime World の OnAwake / OnStart より前に位置が確定するため、
    // Start 側が初期座標を読むゲームでも期待どおりの場所から始まる。
    apply_play_spawn_override(snapshot);

    // 未保存の Scene には AssetGUID が無い。空のまま渡す。
    // 空の場合、この World に対する Reload は InvalidRequest になるだけで、
    // 別の Scene が代わりに読まれることはない。
    const ReplayEngine::Runtime::SceneRequestResult request =
        object_runtime_scenes.RequestAdopt(snapshot, object_scene_asset_guid);
    if (request != ReplayEngine::Runtime::SceneRequestResult::Accepted)
    {
        // status だけだとプロジェクトタブを開いていないと見えない。
        // Play に入れないのは重大なので Console へも Error として出す。
        const std::string reason =
            "Play を開始できません（Scene 遷移が進行中です）。SceneRequestResult=" +
            std::to_string(static_cast<int>(request));
        object_editor_context.SetStatus(reason);
        push_editor_log("Error", reason);
        play_spawn_override.active = false;
        return;
    }

    // 構築と入れ替えをその場で済ませる。
    // Play 開始はフレーム境界を挟む必要がないうえ、
    // ここで済ませないと 1 フレームだけ「Play 中なのに空の World」になる。
    object_runtime_scenes.Tick();   // Staging World の構築
    object_runtime_scenes.Tick();   // 入れ替えと Scene::Start()

    if (object_runtime_scenes.State() != ReplayEngine::Runtime::SceneLoadState::Completed)
    {
        // 失敗しても編集 Scene には一切触れていない。そのまま Edit Mode を続ける。
        //
        // ここで黙って戻ると「Play を押しても EDIT MODE のまま」に見え、
        // 原因がまったく分からなくなる。Console へ理由を必ず残す。
        const std::string reason =
            "実行用 Scene を構築できませんでした: " + object_runtime_scenes.LastError() +
            " / SceneLoadState=" +
            std::to_string(static_cast<int>(object_runtime_scenes.State()));
        object_editor_context.SetStatus(reason);
        push_editor_log("Error", reason);
        play_spawn_override.active = false;
        return;
    }

    ReplayEngine::Scene::Scene& runtime_world = object_runtime_scenes.ActiveWorld();

    // 衝突世界を Runtime World へ差し替える。
    // 編集 Scene の ObjectID / ColliderID はここで完全に捨てられるので、
    // Play 中に編集 Scene の Collider へ当たることはない。
    attach_collision_world(runtime_world);

    // Attach 直後に登録表を作る。Play From Here の座標は SceneData へ
    // 事前反映済みなので、OnAwake / OnStart からも正しい開始位置が見える。
    object_collision_world.Refresh();
    if (play_spawn_override.active)
    {
        push_editor_log("Info", play_spawn_override.label + " から Play を開始しました");
        play_spawn_override.active = false; // 一回限り。通常 Play へ持ち越さない。
    }

    // Play 開始時に貯まっていた時間を捨てる。開始直後に物理が飛ぶのを防ぐ。
    object_fixed_accumulator = 0.0f;
    object_time_scale = 1.0f;
    object_collision_events.Reset();

    object_scene_play_mode = true;
    object_scene_paused = false;
    object_runtime_world_active = true;
    object_bound_world_instance = object_runtime_scenes.ActiveWorldID();
    object_editor_context.SetPlayMode(true);
    object_editor_context.AttachScene(&runtime_world);
    object_editor_context.ResetSceneState();
    object_editor_context.SetStatus("実行中（編集シーンは保持されています）");

    // ---- Play 直後の全数診断 ------------------------------------------------
    //
    // Inspector が見ているのは編集 Scene の Component で、実際に動くのは
    // ここで作られた実行用 World の複製の方。複製側の状態は Inspector から
    // 一切見えないため、ここで洗いざらい出す。
    //
    // 「Play しても動かない」の原因になり得るものを全部並べる:
    //   ScriptRuntime が無い / Backend が無い / Backend 未初期化 /
    //   Assembly 未ロード / Play セッション未開始 / Catalog が空 /
    //   型が Catalog に無い / Schema 未解決 / インスタンス生成失敗
    {
        namespace Scripting = ReplayEngine::Scripting;

        push_editor_log("Info", "===== Play 診断 開始 =====");

        if (!object_script_runtime)
        {
            push_editor_log("Error", "ScriptRuntime がありません。C# は動きません");
        }
        else
        {
            push_editor_log("Info", std::string("Play セッション: ") +
                (object_script_runtime->PlaySessionActive() ? "有効" : "*** 無効 ***"));

            auto* backend = dynamic_cast<Scripting::CSharp::CSharpScriptBackend*>(
                object_script_runtime->Backend(Scripting::ScriptLanguage::CSharp));
            if (backend == nullptr)
            {
                push_editor_log("Error", "C# Backend が接続されていません");
            }
            else
            {
                push_editor_log(backend->Initialized() ? "Info" : "Error",
                    std::string("C# Backend 初期化: ") +
                    (backend->Initialized() ? "済" : "*** 未 ***"));
                push_editor_log(backend->AssemblyLoaded() ? "Info" : "Error",
                    std::string("C# Assembly ロード: ") +
                    (backend->AssemblyLoaded() ? "済" : "*** 未 ***"));
                push_editor_log("Info", "C# 生存インスタンス数: " +
                    std::to_string(backend->LiveInstanceCount()));
                if (!backend->LastErrorMessage().empty())
                {
                    push_editor_log("Error",
                        "C# Backend 直近エラー: " + backend->LastErrorMessage());
                }
            }

            const auto& all = object_script_runtime->Catalog().All();
            push_editor_log(all.empty() ? "Error" : "Info",
                "Catalog 登録数: " + std::to_string(all.size()));
            for (const Scripting::ScriptTypeDescriptor& descriptor : all)
            {
                const bool can = backend != nullptr &&
                    backend->CanInstantiate(descriptor.type_id);
                push_editor_log(can ? "Info" : "Warning",
                    "  Catalog: " + descriptor.DisplayName() +
                    " / class=" + descriptor.class_name +
                    " / typeid=" + descriptor.type_id.ToString() +
                    " / asset=" + descriptor.asset_guid +
                    " / 生成可否=" + (can ? "可" : "*** 不可 ***") +
                    (descriptor.last_error.empty()
                        ? std::string() : " / エラー=" + descriptor.last_error));
            }
        }

        std::size_t script_total = 0;
        std::size_t script_with_instance = 0;
        for (ReplayEngine::Core::GameObject* root : runtime_world.RootGameObjects())
        {
            if (root == nullptr) continue;
            count_runtime_script_instances(*root, script_total, script_with_instance);
        }
        push_editor_log(script_total == 0 ? "Error"
            : (script_with_instance == script_total ? "Info" : "Warning"),
            "実行用 World の Script Component: " + std::to_string(script_total) +
            " 個 / インスタンス生成済み " + std::to_string(script_with_instance) + " 個");

        if (script_total == 0)
        {
            push_editor_log("Error",
                "実行用 World に Script Component が 1 つもありません。"
                "編集 Scene から実行用 Scene への複製で落ちています");
        }

        push_editor_log("Info", "===== Play 診断 終了 =====");
    }
}

// 実行用 World の Script Component を数える。
// Play 直後の 1 回だけ呼ぶ診断用。
void framework::count_runtime_script_instances(
    ReplayEngine::Core::GameObject& object,
    std::size_t& total, std::size_t& with_instance)
{
    for (std::size_t index = 0; index < object.ComponentCount(); ++index)
    {
        ReplayEngine::Core::Component* component = object.ComponentAt(index);
        if (component == nullptr) continue;
        auto* script = dynamic_cast<ReplayEngine::Scripting::ScriptComponent*>(component);
        if (script == nullptr) continue;

        ++total;
        if (script->HasInstance()) ++with_instance;

        push_editor_log(script->HasInstance() ? "Info" : "Error",
            "  [" + object.Name() + "] 状態=" +
            ReplayEngine::Scripting::ToString(script->Status()) +
            " / インスタンス=" + (script->HasInstance() ? "あり" : "*** なし ***") +
            " / class=" + script->ClassName() +
            " / typeid=" + script->ScriptType().ToString() +
            " / asset=" + script->ScriptAssetGUID() +
            " / Schema=" + (script->Schema() ? "あり" : "*** なし ***") +
            " / enabled=" + (script->Enabled() ? "true" : "false") +
            (script->LastError().empty()
                ? std::string() : " / 理由=" + script->LastError()));
    }

    for (ReplayEngine::Core::GameObject* child : object.Children())
    {
        if (child != nullptr) count_runtime_script_instances(*child, total, with_instance);
    }
}

void framework::exit_object_play_mode()
{
    if (!object_scene_play_mode) return;

    object_audio_system.StopAll();

    // 先に衝突世界を切り離す。
    // Scene を消してから切り離すと、その間に問い合わせが来た場合に
    // 破棄済みの GameObject を引きに行ってしまう。
    detach_collision_world();

    // Runtime World を捨てる。
    //
    // 編集 Scene へ書き戻すことはしない。
    // Play 中の変更（生成された Prefab、動いた Transform、増えた Component）は
    // すべてここで消える。暗黙保存の経路そのものを置かない。
    object_runtime_scenes.ResetToEmptyWorld();
    object_collision_events.Reset();

    object_fixed_accumulator = 0.0f;
    object_time_scale = 1.0f;

    object_scene_play_mode = false;
    object_scene_paused = false;
    object_runtime_world_active = false;
    object_bound_world_instance = object_runtime_scenes.ActiveWorldID();

    // 編集 Scene へ戻す。Play 中の Selection と Undo 履歴はここで捨てる。
    // Runtime の操作が Edit Mode の Undo へ混ざらないのはこのため。
    object_editor_context.SetPlayMode(false);
    object_editor_context.AttachScene(&object_scene);
    object_editor_context.ResetSceneState();

    // 編集 Scene の衝突世界を張り直す。
    attach_collision_world(object_scene);
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
    const std::filesystem::path source = content_path(record->source_path);
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

static_mesh* framework::resolve_builtin_primitive_mesh(const std::string& builtin_id)
{
    if (!device || builtin_id.rfind("builtin:", 0) != 0) return nullptr;
    const auto cached = builtin_primitive_mesh_cache.find(builtin_id);
    if (cached != builtin_primitive_mesh_cache.end()) return cached->second.get();

    std::vector<static_mesh::vertex> vertices;
    std::vector<std::uint32_t> indices;
    if (!BuildBuiltinPrimitive(builtin_id, vertices, indices)) return nullptr;
    auto mesh = std::make_unique<static_mesh>(device.Get(), vertices, indices);
    if (!mesh || !mesh->is_loaded()) return nullptr;
    static_mesh* raw = mesh.get();
    builtin_primitive_mesh_cache.emplace(builtin_id, std::move(mesh));
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

    const std::filesystem::path material_path = content_path(record->source_path);
    std::error_code filesystem_error;
    const auto write_time = std::filesystem::last_write_time(
        material_path, filesystem_error);
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
    if (!MaterialAsset::Load(material_path, loaded, error))
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
    using namespace ReplayEngine::Rendering;

    RenderItem item = source;
    item.legacy_tint = source.tint;
    item.lighting_model = deferred_lighting_model(source.shading_model);

    const MaterialAsset* material = resolve_object_material(source.material_asset);
    const bool has_material_asset = material != nullptr;
    MaterialAsset fallback_material;
    if (!has_material_asset)
    {
        // Material Asset が無い Renderer でも、Renderer 側の描画方式と
        // material.* Motion は同じ解決経路へ流す。
        fallback_material.shading_model = source.shading_model;
        material = &fallback_material;
    }

    // 旧 .cso fallback では従来どおり Material の base_color を頂点 tint に使う。
    item.legacy_tint = has_material_asset
        ? (source.material_override ? source.tint : material->base_color)
        : source.tint;

    // Catalog shader では BaseColor は b9 から渡す。pin.color は Renderer 側の
    // 追加 tint にだけ使い、同じ色を二重に掛けない。
    item.tint = has_material_asset && !source.material_override
        ? DirectX::XMFLOAT4{ 1.0f, 1.0f, 1.0f, 1.0f } : source.tint;

    item.shading_model = material->shading_model; // fallback のためだけに保持
    // material_override は従来どおり「Renderer tint が Material BaseColor を置換」。
    // Catalog shader は BaseColor を b9 から読むため、置換時は b9 側を白にする。
    item.material_base_color = source.material_override
        ? source.override_material_base_color : material->base_color;
    item.metallic = source.material_override
        ? source.override_material_metallic : material->metallic;
    item.roughness = source.material_override
        ? source.override_material_roughness : material->roughness;
    item.ambient_occlusion = source.material_override
        ? source.override_material_ambient_occlusion : material->ambient_occlusion;
    item.emissive_color = source.material_override
        ? source.override_material_emissive_color : material->emissive;
    item.emissive_strength = source.material_override
        ? source.override_material_emissive_strength : material->emissive_strength;
    item.double_sided = source.double_sided || material->double_sided ||
        (source.material_override && source.override_material_double_sided);
    item.outline = has_material_asset
        ? material->layers.Contains(BuiltInShaderLayers::Outline) : source.outline;
    item.pixelate_size = material->pixelate_grid;
    item.pixelate_strength = material->pixelate_strength;

    // 現在の GameObject mesh は静的提出も skinned_mesh renderer を通る。
    // Vertex Shader の VS_OUT と一致させるため Catalog 側も Skinned 変種を使う。
    // 真の static_mesh 経路を RenderItem へ接続した時点で source.skinned 分岐へ戻す。
    const ShaderVariant variant = ShaderVariant::Skinned;
    MaterialAsset binding_material = *material;
    if (source.material_override)
    {
        binding_material.base_color = source.override_material_base_color;
        binding_material.metallic = source.override_material_metallic;
        binding_material.roughness = source.override_material_roughness;
        binding_material.ambient_occlusion = source.override_material_ambient_occlusion;
        binding_material.emissive = source.override_material_emissive_color;
        binding_material.emissive_strength = source.override_material_emissive_strength;
        binding_material.double_sided = source.override_material_double_sided;
        binding_material.properties.Set("prop.BaseColor",
            ReplayEngine::Reflection::PropertyValue::MakeColor(
                source.override_material_base_color));
        binding_material.properties.Set("prop.Metallic",
            ReplayEngine::Reflection::PropertyValue::MakeFloat(
                source.override_material_metallic));
        binding_material.properties.Set("prop.Roughness",
            ReplayEngine::Reflection::PropertyValue::MakeFloat(
                source.override_material_roughness));
        binding_material.properties.Set("prop.AmbientOcclusion",
            ReplayEngine::Reflection::PropertyValue::MakeFloat(
                source.override_material_ambient_occlusion));
        binding_material.properties.Set("prop.Emissive",
            ReplayEngine::Reflection::PropertyValue::MakeVector3(
                source.override_material_emissive_color));
        binding_material.properties.Set("prop.EmissiveStrength",
            ReplayEngine::Reflection::PropertyValue::MakeFloat(
                source.override_material_emissive_strength));
        binding_material.properties.Set("prop.DoubleSided",
            ReplayEngine::Reflection::PropertyValue::MakeBool(
                source.override_material_double_sided));
    }

    // Motion の material.* は Renderer の永続 material_override とは別物。
    // Asset を直接変更せず、この draw 用コピーにだけ重ねる。停止した次フレームには
    // PrepareMaterialMotionProperties が active mask / bag を消すため完全に元へ戻る。
    using namespace ReplayEngine::Components;
    const std::uint32_t motion_mask = source.material_motion_fixed_mask;
    if ((motion_mask & MaterialMotionBaseColor) != 0)
    {
        item.material_base_color = source.override_material_base_color;
        binding_material.base_color = source.override_material_base_color;
        binding_material.properties.Set("prop.BaseColor",
            ReplayEngine::Reflection::PropertyValue::MakeColor(
                source.override_material_base_color));
    }
    if ((motion_mask & MaterialMotionMetallic) != 0)
    {
        item.metallic = source.override_material_metallic;
        binding_material.metallic = source.override_material_metallic;
        binding_material.properties.Set("prop.Metallic",
            ReplayEngine::Reflection::PropertyValue::MakeFloat(
                source.override_material_metallic));
    }
    if ((motion_mask & MaterialMotionRoughness) != 0)
    {
        item.roughness = source.override_material_roughness;
        binding_material.roughness = source.override_material_roughness;
        binding_material.properties.Set("prop.Roughness",
            ReplayEngine::Reflection::PropertyValue::MakeFloat(
                source.override_material_roughness));
    }
    if ((motion_mask & MaterialMotionAmbientOcclusion) != 0)
    {
        item.ambient_occlusion = source.override_material_ambient_occlusion;
        binding_material.ambient_occlusion = source.override_material_ambient_occlusion;
        binding_material.properties.Set("prop.AmbientOcclusion",
            ReplayEngine::Reflection::PropertyValue::MakeFloat(
                source.override_material_ambient_occlusion));
    }
    if ((motion_mask & MaterialMotionEmissiveColor) != 0)
    {
        item.emissive_color = source.override_material_emissive_color;
        binding_material.emissive = source.override_material_emissive_color;
        binding_material.properties.Set("prop.Emissive",
            ReplayEngine::Reflection::PropertyValue::MakeVector3(
                source.override_material_emissive_color));
    }
    if ((motion_mask & MaterialMotionEmissiveStrength) != 0)
    {
        item.emissive_strength = source.override_material_emissive_strength;
        binding_material.emissive_strength = source.override_material_emissive_strength;
        binding_material.properties.Set("prop.EmissiveStrength",
            ReplayEngine::Reflection::PropertyValue::MakeFloat(
                source.override_material_emissive_strength));
    }
    if ((motion_mask & MaterialMotionDoubleSided) != 0)
    {
        item.double_sided = source.double_sided || source.override_material_double_sided;
        binding_material.double_sided = source.override_material_double_sided;
        binding_material.properties.Set("prop.DoubleSided",
            ReplayEngine::Reflection::PropertyValue::MakeBool(
                source.override_material_double_sided));
    }
    for (const ReplayEngine::Reflection::PropertyBag::Entry& entry :
        source.material_motion_properties.Entries())
    {
        binding_material.properties.Set(entry.name, entry.value);
    }

    const bool resolved = MaterialBindingResolver::Resolve(binding_material,
        shader_library.Catalog(), variant, item.material_binding);
    // binding_material は一時コピーなので、LayerStack の借用先だけ元Assetへ戻す。
    item.material_binding.layers = has_material_asset ? &material->layers : nullptr;

    if (resolved && item.material_binding.usable_shader)
    {
        item.lighting_model = item.material_binding.lighting_model;
        if (item.material_binding.requested_shader.IsValid())
            object_shader_lighting_failures.erase(
                item.material_binding.requested_shader.ToString());
    }
    else
    {
        item.lighting_model = deferred_lighting_model(material->shading_model);
    }

    if (item.material_binding.missing_shader)
    {
        // Deferred は generated b9 ではなく固定 GBuffer bridge を使うため、
        // Missing Shader のマゼンタをこちらにも明示的に反映する。
        item.material_base_color = DirectX::XMFLOAT4{ 1.0f, 0.0f, 1.0f, 1.0f };
        item.tint = DirectX::XMFLOAT4{ 1.0f, 1.0f, 1.0f, 1.0f };
        item.emissive_color = DirectX::XMFLOAT3{ 0.0f, 0.0f, 0.0f };
        item.emissive_strength = 0.0f;

        const std::string key = material->shader_guid.empty()
            ? std::string("legacy:") + std::to_string(material->shading_model)
            : material->shader_guid;
        if (object_shader_lighting_failures.insert(key).second)
        {
            push_editor_log("Error",
                "Material Shader を解決できないため Unlit/Magenta へフォールバック: " +
                key + " / " + item.material_binding.diagnostic);
        }
    }

    // Pixelate は surface shader と layer の両方から有効になる。
    item.pixelate_enabled = item.material_binding.shader == BuiltInShaders::Pixelate;
    if (!has_material_asset) return item;

    for (const ShaderLayer& layer : material->layers.Layers())
    {
        if (!layer.enabled) continue;
        if (layer.Is(BuiltInShaderLayers::Pixelate))
        {
            item.pixelate_enabled = true;
            item.pixelate_size = layer.parameter;
            item.pixelate_strength = layer.strength;
        }
        else if (layer.Is(BuiltInShaderLayers::Outline))
        {
            item.outline = true;
        }
    }
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

        // Engine 内蔵 Primitive も通常の MeshRendererComponent から提出される。
        // 特殊な Primitive GameObject は作らず、asset id だけ builtin:* を使う。
        if (item.mesh_asset.rfind("builtin:", 0) == 0)
        {
            static_mesh* primitive = resolve_builtin_primitive_mesh(item.mesh_asset);
            if (primitive == nullptr) continue;

            if (item.double_sided)
                immediate_context->RSSetState(
                    rasterizer_states[(size_t)RASTER_STATE::CULL_NONE].Get());

            if (depth_only)
            {
                primitive->render(immediate_context.Get(), item.world, item.tint,
                    nullptr, nullptr, nullptr, false, false);
            }
            else if (gbuffer_pass)
            {
                if (item.material_binding.usable_shader)
                {
                    material_gpu_binder.BindGBufferTextures(device.Get(), immediate_context.Get(),
                        asset_database, item.material_binding);
                }
                else
                {
                    material_gpu_binder.UnbindTextures(immediate_context.Get());
                }
                bind_gbuffer_material(item.lighting_model,
                    false, item.pixelate_enabled, item.pixelate_size,
                    item.pixelate_strength, item.metallic, item.roughness,
                    item.ambient_occlusion, item.emissive_strength,
                    item.material_base_color, item.emissive_color,
                    item.material_binding.usable_shader
                        ? item.material_binding.TextureSemanticMask() : 0u);
                primitive->render(immediate_context.Get(), item.world, item.tint,
                    static_mesh_gbuffer_ps.Get(), nullptr, nullptr, true, true);
                material_gpu_binder.UnbindTextures(immediate_context.Get());
            }
            else
            {
                primitive->render(immediate_context.Get(), item.world, item.tint,
                    static_forward_shader(item.shading_model));
            }

            if (item.double_sided)
                immediate_context->RSSetState(
                    rasterizer_states[(size_t)RASTER_STATE::SOLID].Get());
            continue;
        }

        skinned_mesh* mesh = resolve_object_mesh(item.mesh_asset);
        if (mesh == nullptr) continue;

        // Animator の current / previous clip と blend factor は RenderItem だけを
        // 介して Renderer へ渡す。Motion Runtime とは混ぜず、既存の
        // skinned_mesh::blend_animations() で姿勢を作る。
        skinned_mesh::animation::keyframe blended_keyframe;
        const skinned_mesh::animation::keyframe* keyframe =
            resolve_render_item_keyframe(*mesh, item, blended_keyframe);

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
        if (gbuffer_pass)
        {
            if (item.material_binding.usable_shader)
            {
                material_gpu_binder.BindGBufferTextures(device.Get(), immediate_context.Get(),
                    asset_database, item.material_binding);
            }
            else
            {
                material_gpu_binder.UnbindTextures(immediate_context.Get());
            }

            bind_gbuffer_material(item.lighting_model,
                false, item.pixelate_enabled, item.pixelate_size,
                item.pixelate_strength, item.metallic, item.roughness,
                item.ambient_occlusion, item.emissive_strength,
                item.material_base_color,
                item.emissive_color,
                item.material_binding.usable_shader
                    ? item.material_binding.TextureSemanticMask() : 0u);
        }

        // 最後の引数がモーションベクター出力。GBuffer パスだけで真にする
        // （複数回渡すと前フレーム姿勢が壊れる）。
        if (item.double_sided)
            immediate_context->RSSetState(
                rasterizer_states[(size_t)RASTER_STATE::CULL_NONE].Get());
        mesh->render(immediate_context.Get(), item.world, item.tint,
            keyframe, override_pixel_shader, nullptr, nullptr, true, gbuffer_pass);
        if (gbuffer_pass)
            material_gpu_binder.UnbindTextures(immediate_context.Get());
        if (item.double_sided)
            immediate_context->RSSetState(
                rasterizer_states[(size_t)RASTER_STATE::SOLID].Get());
    }
}


void framework::draw_landscape_scene_meshes(bool gbuffer_pass, bool depth_only)
{
    ReplayEngine::Scene::Scene& scene = active_object_scene();
    for (std::size_t object_index = 0; object_index < scene.GameObjectCount(); ++object_index)
    {
        ReplayEngine::Core::GameObject* object = scene.GameObjectAt(object_index);
        if (object == nullptr || object->PendingDestroy() || !object->ActiveInHierarchy()) continue;

        auto* landscape = object->GetComponent<ReplayEngine::Components::LandscapeComponent>();
        auto* renderer = object->GetComponent<ReplayEngine::Components::LandscapeRendererComponent>();
        if (landscape == nullptr || renderer == nullptr || !renderer->visible ||
            !renderer->ActiveInHierarchy() || !landscape->Data().Valid()) continue;

        const auto& data = landscape->Data();
        const std::uint64_t cache_key = object->ID().Value();
        landscape_gpu_cache_entry& cache = landscape_gpu_mesh_cache[cache_key];
        if (cache.revision != data.Revision() || cache.mesh == nullptr)
        {
            std::vector<static_mesh::vertex> vertices;
            vertices.reserve(data.VertexCount());
            for (const ReplayEngine::Landscape::LandscapeVertex& source : data.Vertices())
            {
                static_mesh::vertex vertex{};
                vertex.position = source.position;
                vertex.normal = source.normal;
                vertex.texcoord = source.uv;
                vertices.push_back(vertex);
            }

            bool gpu_ready = false;
            if (cache.mesh == nullptr)
            {
                cache.mesh = std::make_unique<static_mesh>(device.Get(), vertices, data.Indices());
                gpu_ready = cache.mesh != nullptr && cache.mesh->is_loaded();
            }
            else
            {
                // Sculpt / Topology edit では geometry だけが変わる。
                // static_mesh を丸ごと再構築すると CSO/Texture まで毎回作り直すため、
                // vertex/index buffer だけ更新する。
                gpu_ready = cache.mesh->update_procedural_geometry(
                    device.Get(), vertices, data.Indices());
            }

            if (gpu_ready) cache.revision = data.Revision();
        }
        if (cache.mesh == nullptr || !cache.mesh->is_loaded()) continue;

        if (renderer->double_sided)
            immediate_context->RSSetState(
                rasterizer_states[(size_t)RASTER_STATE::CULL_NONE].Get());

        const DirectX::XMFLOAT4X4 world = object->GetTransform().WorldMatrixFloat4x4();
        if (depth_only)
        {
            cache.mesh->render(immediate_context.Get(), world, renderer->tint,
                nullptr, nullptr, nullptr, false, false);
        }
        else if (gbuffer_pass)
        {
            // Landscape はまず標準PBR surfaceとしてGBufferへ出す。
            // Material Component連携はこの任意Mesh基盤の上へ後から追加できる。
            bind_gbuffer_material(
                ReplayEngine::Rendering::ShaderLightingModel::Pbr,
                false, false, 1.0f, 0.0f,
                0.0f, 0.75f, 1.0f, 0.0f,
                renderer->tint, { 0.0f, 0.0f, 0.0f }, 0u);
            cache.mesh->render(immediate_context.Get(), world, renderer->tint,
                static_mesh_gbuffer_ps.Get(), nullptr, nullptr, true, true);
        }
        else
        {
            cache.mesh->render(immediate_context.Get(), world, renderer->tint,
                static_forward_shader(SHADING_MODEL_PBR));
        }

        if (renderer->double_sided)
            immediate_context->RSSetState(
                rasterizer_states[(size_t)RASTER_STATE::SOLID].Get());
    }
}

void framework::clear_object_mesh_cache() noexcept
{
    object_mesh_cache.clear();
    builtin_primitive_mesh_cache.clear();
    landscape_gpu_mesh_cache.clear();
    object_mesh_failures.clear();
}

void framework::clear_object_material_cache() noexcept
{
    object_material_cache.clear();
    object_material_failures.clear();
    object_shader_lighting_failures.clear();
}

const skinned_mesh::animation::keyframe* framework::resolve_object_keyframe(
    skinned_mesh& mesh, int clip_index, float animation_time, bool loop) const
{
    if (clip_index < 0) return nullptr;
    if (mesh.animation_clips.empty()) return nullptr;
    if (clip_index >= static_cast<int>(mesh.animation_clips.size())) return nullptr;

    const skinned_mesh::animation& clip = mesh.animation_clips.at(
        static_cast<std::size_t>(clip_index));
    if (clip.sequence.empty()) return nullptr;

    const float sampling_rate = clip.sampling_rate > 0.0f ? clip.sampling_rate : 60.0f;
    const float duration = static_cast<float>(clip.sequence.size()) / sampling_rate;

    // クリップ長を知っている Renderer 側で Loop / Clamp を確定する。
    // Animator は clip index と時間だけを持ち、mesh の実データへ依存しない。
    float time = animation_time;
    if (duration > 0.0f)
    {
        if (loop)
        {
            time = std::fmod(time, duration);
            if (time < 0.0f) time += duration;
        }
        else
        {
            time = (std::max)(0.0f, (std::min)(duration, time));
        }
    }

    int frame = static_cast<int>(time * sampling_rate);
    if (frame < 0) frame = 0;
    if (frame >= static_cast<int>(clip.sequence.size()))
    {
        frame = static_cast<int>(clip.sequence.size()) - 1;
    }
    return &clip.sequence.at(static_cast<std::size_t>(frame));
}

const skinned_mesh::animation::keyframe* framework::resolve_render_item_keyframe(
    skinned_mesh& mesh, const ReplayEngine::Rendering::RenderItem& item,
    skinned_mesh::animation::keyframe& blended_keyframe) const
{
    if (!item.skinned) return nullptr;

    const skinned_mesh::animation::keyframe* current = resolve_object_keyframe(
        mesh, item.clip_index, item.animation_time, item.animation_loop);
    if (current == nullptr || item.previous_clip_index < 0 ||
        item.animation_blend_factor >= 1.0f)
    {
        return current;
    }

    const skinned_mesh::animation::keyframe* previous = resolve_object_keyframe(
        mesh, item.previous_clip_index, item.previous_animation_time,
        item.previous_animation_loop);
    if (previous == nullptr || previous->nodes.size() != current->nodes.size())
        return current;

    const skinned_mesh::animation::keyframe* sources[2]{ previous, current };
    mesh.blend_animations(sources,
        (std::max)(0.0f, (std::min)(1.0f, item.animation_blend_factor)),
        blended_keyframe);
    mesh.update_animation(blended_keyframe);
    return &blended_keyframe;
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
