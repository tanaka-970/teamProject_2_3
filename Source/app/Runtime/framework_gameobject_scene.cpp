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
#include "../../RePlayEngine/Components/Gameplay/CharacterMotorComponent.h"
#include "../../RePlayEngine/Components/Gameplay/HealthComponent.h"
#include "../../RePlayEngine/Components/Gameplay/PlayerControllerComponent.h"
#include "../../RePlayEngine/Components/Gameplay/PlayerInputComponent.h"
#include "../../RePlayEngine/Components/Physics/SphereColliderComponent.h"
#include "../../RePlayEngine/Components/Rendering/AnimatorComponent.h"
#include "../../RePlayEngine/Components/Rendering/MeshRendererComponent.h"
#include "../../RePlayEngine/Components/Rendering/SkinnedMeshRendererComponent.h"
#include "../../RePlayEngine/Object/Registry/BuiltInComponents.h"
#include "../../RePlayEngine/Rendering/Adapter/SceneRenderCollector.h"
#include "../../RePlayEngine/Scene/Serialization/SceneData.h"
#include "../../RePlayEngine/Scene/Serialization/SceneSerializer.h"

#include <cctype>
#include <cmath>
#include <filesystem>

namespace
{
    namespace SceneSerialization = ReplayEngine::Scene::Serialization;
}

// ---------------------------------------------------------------------------
// 初期化
// ---------------------------------------------------------------------------

void framework::initialize_object_scene()
{
    // Component 型の登録。Scene を作る前・読む前に 1 回だけ。
    // 二重に呼んでも ComponentRegistry が重複を弾くので安全。
    ReplayEngine::Core::RegisterBuiltInComponents();

    object_scene.SetName("TrainingStage");
    object_editor_context.AttachScene(&object_scene);
    object_editor_context.SetAssetDatabase(&asset_database);
    object_editor_context.SetScenePath(object_scene_path);

    // 既定の Scene があれば読み込む。無くても失敗扱いにしない（新規シーンとして扱う）。
    std::error_code filesystem_error;
    if (std::filesystem::exists(object_scene_path, filesystem_error) && !filesystem_error)
    {
        load_object_scene(false);
    }
    else
    {
        object_editor_context.SetStatus("新規シーン");
    }

    // 編集中も Component の OnStart を回したいので Scene は開始状態にしておく。
    // 実際にゲームロジックが走るかどうかは Edit Mode 判定で制御する。
    object_scene.Start();

    // 衝突世界を編集 Scene へつなぐ。
    // これ以降 Component が見る IPhysicsQueryService は衝突世界であり、
    // 旧 Stage は衝突世界の内側から必要なときだけ呼ばれる。
    initialize_collision_world();
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
    // 保存されていた ID を優先し、それが使えない場合だけ自動選出する。
    player_control_system.SetControlledObject(services.ControlledObject());
    const ReplayEngine::Core::ObjectID controlled = player_control_system.Resolve(scene);
    services.SetControlledObject(controlled);

    // 【重要】旧 Player を出すかどうかは Component の有無では決めない。
    //
    //   以前は「PlayerController があるか」で切り替えていたため、
    //   Controller を削除した瞬間に旧 Player が復活し、二体表示になっていた。
    //
    //   移行状態（Scene に保存される）だけで判定する。
    //   Component を消しても移行状態は取り消されないので、旧 Player は二度と出ない。
    object_player_active = services.PlayerMigration().Migrated();
    if (game_scene != nullptr)
    {
        game_scene->Gameplay().SetLegacyPlayerActive(!object_player_active);
    }
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

    // 描画提出リストを作り直す。ここでは D3D に触れない。
    ReplayEngine::Rendering::SceneRenderCollector::Collect(scene, object_render_items);
}

void framework::update_object_camera_follow(float elapsed_time)
{
    if (game_scene == nullptr) return;

    // 追従対象は「CameraTargetComponent を持つ操作対象 GameObject」。
    // カメラ側は Player 具象型を一切知らない。
    const ReplayEngine::Scene::Scene& scene = active_object_scene();
    const ReplayEngine::Core::ObjectID controlled = scene.Services().ControlledObject();
    if (!controlled.Valid()) return;

    const ReplayEngine::Core::GameObject* target = scene.FindGameObjectByID(controlled);
    if (target == nullptr) return;

    const auto* camera_target = target->GetComponent<ReplayEngine::Components::CameraTargetComponent>();
    if (camera_target == nullptr || !camera_target->ActiveInHierarchy()) return;

    game_scene->Gameplay().FollowCameraTarget(
        target->GetTransform().WorldPosition(),
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

    if (choose_path || object_scene_path.empty())
    {
        // ファイルダイアログは既存の実装方針に合わせて後段で差し替えられるよう、
        // ここでは既定パスをそのまま使う。
        object_scene_path = std::filesystem::path("resources") / "Scenes" /
            (object_scene.Name() + ReplayEngine::Scene::Serialization::SceneSerializer::file_extension);
    }

    // メインスレッドでスナップショットを取ってから書き出す。
    // Scene の実体をファイル処理へ渡さないことで、
    // 将来書き込みだけを別スレッドへ回せる形にしてある。
    SceneSerialization::SceneData data;
    SceneSerialization::CaptureScene(object_scene, data);

    std::string error;
    if (!SceneSerialization::SceneSerializer::SaveToFile(data, object_scene_path, error))
    {
        object_editor_context.SetStatus("保存に失敗しました: " + error);
        return false;
    }

    object_editor_context.SetScenePath(object_scene_path);
    object_editor_context.ClearDirty();
    object_editor_context.SetStatus("保存しました: " + object_scene_path.filename().string());
    return true;
}

bool framework::load_object_scene(bool choose_path)
{
    if (object_scene_play_mode) exit_object_play_mode();

    if (choose_path)
    {
        object_scene_path = std::filesystem::path("resources") / "Scenes" /
            (object_scene.Name() + ReplayEngine::Scene::Serialization::SceneSerializer::file_extension);
    }

    SceneSerialization::SceneData data;
    std::string error;
    if (!SceneSerialization::SceneSerializer::LoadFromFile(data, object_scene_path, error))
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

    // 読み込み後に Scene を開始する。
    // ApplySceneData の中では OnStart / OnEnable を呼ばないので、
    // 途中まで構築された状態で Component が動くことはない。
    object_scene.Start();

    object_editor_context.AttachScene(&object_scene);
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
    return true;
}

// ---------------------------------------------------------------------------
// Play Mode
// ---------------------------------------------------------------------------

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

void framework::draw_object_scene_meshes(ID3D11PixelShader* override_pixel_shader,
    bool gbuffer_pass)
{
    if (object_render_items.Empty()) return;

    for (const ReplayEngine::Rendering::RenderItem& item : object_render_items.Items())
    {
        // Asset 未指定・解決不可・読み込み失敗のいずれでも nullptr が返る。
        // その場合はこの GameObject を描かずに次へ進むだけで、実行は継続する。
        if (item.mesh_asset.empty()) continue;
        skinned_mesh* mesh = resolve_object_mesh(item.mesh_asset);
        if (mesh == nullptr) continue;

        // GBuffer パスでは Component が指定した描画方式を材質定数へ流す。
        if (gbuffer_pass) bind_gbuffer_material(deferred_shading_model(item.shading_model));

        // Animator が決めたクリップと時刻から姿勢を求める。
        // 静的メッシュ扱いの提出（skinned=false）ではバインドポーズのまま描く。
        const skinned_mesh::animation::keyframe* keyframe = item.skinned
            ? resolve_object_keyframe(*mesh, item.clip_index, item.animation_time)
            : nullptr;

        mesh->render(immediate_context.Get(), item.world, item.tint,
            keyframe, override_pixel_shader);
    }
}

void framework::clear_object_mesh_cache() noexcept
{
    object_mesh_cache.clear();
    object_mesh_failures.clear();
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
// 旧 Player から Player GameObject への変換
// ---------------------------------------------------------------------------
//
// 一度だけ実行して v7 Scene へ保存する。
// 毎回起動時にコードから Component を付け直す方式にはしない。
// 変換後は Scene ファイルが正式な構成元になる。

bool framework::convert_legacy_player_to_gameobject()
{
    namespace Components = ReplayEngine::Components;

    if (object_scene_play_mode)
    {
        object_editor_context.SetStatus("実行中は変換できません");
        return false;
    }

    // 移行済みなら二度と変換しない。
    // Component を消していても移行状態は残るので、ここで確実に弾かれる。
    if (object_scene.Services().PlayerMigration().Migrated())
    {
        object_editor_context.SetStatus("旧 Player は変換済みです（Player ObjectID: " +
            object_scene.Services().PlayerMigration().MigratedObject().ToString() + "）");
        return false;
    }

    object_editor_context.BeginEdit("旧 Player を GameObject へ変換");

    ReplayEngine::Core::GameObject* player = object_scene.CreateGameObject("Player");
    if (player == nullptr)
    {
        object_editor_context.CancelEdit();
        return false;
    }

    // 旧 Player の現在値を初期値として引き継ぐ。
    const Player* legacy = game_scene != nullptr ? &game_scene->Gameplay().GetPlayer() : nullptr;

    // GameObject の Scale は 1.0 のままにする。
    // モデル固有の縮小（旧 Player の 0.01）は描画 Component 側の補正へ移す。
    // こうすると Collider の半径やギズモが実寸の単位になり、扱いやすい。
    ReplayEngine::Core::Transform& transform = player->GetTransform();
    const DirectX::XMFLOAT3 model_scale = legacy != nullptr
        ? legacy->GetScale() : DirectX::XMFLOAT3{ 0.01f, 0.01f, 0.01f };
    transform.SetLocal(
        legacy != nullptr ? legacy->GetPosition() : DirectX::XMFLOAT3{ 0.0f, 0.0f, 0.0f },
        legacy != nullptr ? legacy->GetAngle() : DirectX::XMFLOAT3{ 0.0f, 0.0f, 0.0f },
        DirectX::XMFLOAT3{ 1.0f, 1.0f, 1.0f });

    auto* renderer = player->AddComponent<Components::SkinnedMeshRendererComponent>();
    if (renderer != nullptr && legacy != nullptr)
    {
        renderer->visual_rotation_offset = DirectX::XMFLOAT3{
            legacy->GetVisualPitchDeg(),
            legacy->GetVisualYawOffsetDeg(),
            legacy->GetVisualRollDeg() };
        // 旧 Player が GameObject の Scale として使っていた縮小を、
        // モデル座標系の補正としてこちらへ移す。見た目は完全に同じになる。
        renderer->local_scale_multiplier = model_scale;
        renderer->shading_model = shading_per_skinned[0];
        renderer->outline = outline_per_skinned[0];
        renderer->tint = material_color;

        // プレイヤーモデルの Asset を AssetDatabase から探す。
        // 見つからなくても変換は成功させ、Inspector から後で指定できるようにする。
        if (const auto* record = asset_database.FindByPath(
            std::filesystem::path("resources") / "AnimationModel" / "AllAnimation1.cereal"))
        {
            renderer->mesh_asset = record->guid;
        }
    }

    auto* animator = player->AddComponent<Components::AnimatorComponent>();
    if (animator != nullptr && legacy != nullptr)
    {
        animator->idle_clip = legacy->clip_idle;
        animator->walk_clip = legacy->clip_walk;
        animator->jump_clip = legacy->clip_jump;
    }

    auto* collider = player->AddComponent<Components::SphereColliderComponent>();
    if (collider != nullptr && legacy != nullptr)
    {
        const auto& source = legacy->Collider();
        collider->radius = source.radius;
        collider->center_offset = source.center_offset;
        collider->skin_width = source.skin_width;
        collider->walkable_normal_y = source.walkable_normal_y;
    }

    auto* motor = player->AddComponent<Components::CharacterMotorComponent>();

    // 旧 Player 変換のときだけ、既存の Sphere Collider を移動用として自動設定する。
    // 通常は Inspector から明示的に選ぶ決まりだが、変換直後に
    // 「Collider はあるのに動かない」状態で渡すのは不親切なため。
    if (motor != nullptr && collider != nullptr) motor->SetPrimaryCollider(*collider);

    if (motor != nullptr && legacy != nullptr)
    {
        motor->move_speed = legacy->GetMoveSpeed();
        motor->acceleration = legacy->GetAcceleration();
        motor->deceleration = legacy->GetDeceleration();
        motor->gravity = legacy->GetGravity();
        motor->jump_power = legacy->GetJumpSpeed();
        motor->fallback_ground_y = legacy->GetGroundY();
        motor->vertical_physics = legacy->IsVerticalPhysicsEnabled();
    }

    player->AddComponent<Components::PlayerInputComponent>();

    auto* controller = player->AddComponent<Components::PlayerControllerComponent>();
    if (controller != nullptr && legacy != nullptr)
    {
        controller->turn_speed_degrees = DirectX::XMConvertToDegrees(legacy->GetTurnSpeed());
    }

    player->AddComponent<Components::HealthComponent>();

    auto* camera_target = player->AddComponent<Components::CameraTargetComponent>();
    if (camera_target != nullptr && game_scene != nullptr)
    {
        const SceneGame& gameplay = game_scene->Gameplay();
        camera_target->follow_distance = gameplay.follow_distance;
        camera_target->follow_height = gameplay.follow_height;
        camera_target->follow_lag = gameplay.follow_lag;
    }

    // 移行完了を Scene 単位の状態として記録する。
    // これ以降、Component を削除しても旧 Player は復活しない。
    object_scene.Services().PlayerMigration().MarkMigrated(player->ID());
    object_scene.Services().SetControlledObject(player->ID());
    player_control_system.SetControlledObject(player->ID());

    object_editor_context.CommitEdit();
    object_editor_context.Selection().Select(player->ID(), false);
    object_editor_context.SetStatus(
        "旧 Player を GameObject へ変換しました。Ctrl+S で保存してください。");
    return true;
}
