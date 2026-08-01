// SceneCollisionWorld と framework の接続部。
//
// ここが唯一の「衝突世界をつなぎ替える場所」。
// Play 開始 / Play 終了 / Scene 読み込みのすべてがここを通るので、
// 「片方だけ付け替え忘れて古い Scene の ID を引き続ける」事故が起きない。
//
// 依存方向:
//   framework -> RePlayEngine の一方向。逆向きの参照は無い。

#include "framework.h"

#include "../../RePlayEngine/Components/Physics/MeshColliderComponent.h"
#include "../../RePlayEngine/Object/GameObject/GameObject.h"

namespace
{
    using BackendMode = ReplayEngine::Scene::SceneCollisionWorld::BackendMode;
}

void framework::initialize_collision_world()
{
    // Cook キャッシュと、三角形を取り出す手段をつなぐ。
    //
    // キャッシュは weak_ptr しか持たないので、ここで渡しても
    // Cook データの寿命は MeshColliderComponent 側にある。
    object_collision_world.AttachCookCache(&object_collision_cook_cache);

    object_collision_world.SetMeshLoader(
        [this](const ReplayEngine::Physics::CookKey& key,
            std::vector<ReplayEngine::Physics::Triangle>& out)
        {
            return load_collision_triangles(key, out);
        });

    // Asset の中身が変わったかを見分けるための文字列。
    // これがキーへ入るので、再インポートすると古い Cook 結果は使われなくなる。
    object_collision_world.SetRevisionProvider(
        [this](const std::string& asset_guid)
        {
            return resolve_asset_revision(asset_guid);
        });

    // 起動直後は編集 Scene につなぐ。Play へ入ると実行用 Scene へ差し替わる。
    attach_collision_world(object_scene);
}

void framework::attach_collision_world(ReplayEngine::Scene::Scene& scene)
{
    // AttachScene の中で登録表と接触ペアが捨てられる。
    // 古い Scene の ObjectID / ColliderID は 1 件も残らない。
    object_collision_world.AttachScene(&scene);

    // Backend Mode と移行済み集合は Scene の状態。
    // 衝突世界は「今つないでいる Scene の設定」で動く。
    ReplayEngine::Scene::SceneServices& services = scene.Services();
    object_collision_world.SetBackendMode(
        ReplayEngine::Scene::SceneCollisionWorld::BackendModeFromInt(
            services.CollisionBackendMode()));
    object_collision_world.LegacyMigration() = services.LegacyStageMigration();

    // Component からの問い合わせ先を衝突世界にする。
    // ここから先、Motor が見るのは LegacyStageCollisionBridge ではない。
    // 旧 Stage は衝突世界の内側から、必要なときだけ呼ばれる。
    services.SetPhysics(&object_collision_world);
}

void framework::detach_collision_world()
{
    object_collision_world.DetachScene();
    object_collision_world.DetachLegacy();

    // 参照が 0 になった Cook データを表から外す。
    // Scene を離れた時点で誰も使っていないものは、ここで消える。
    object_collision_cook_cache.Collect();
}

void framework::refresh_collision_world()
{
    ReplayEngine::Scene::Scene& scene = active_object_scene();

    // つなぎ先がずれていたら直す。
    // Play の出入りは enter/exit 側で処理しているが、
    // 万一取りこぼしても、ここで必ず正しい Scene へ戻る。
    if (object_collision_world.AttachedScene() != &scene)
    {
        attach_collision_world(scene);
    }

    // 旧 Stage をつなぐ。移行済みなら衝突世界の側が問い合わせを止める。
    //
    // 【二重衝突しない理由】
    //   ここで無条件に AttachLegacy しても、Hybrid で移行済みの移行元へは
    //   一切問い合わせが飛ばない。「同じ地形を新旧両方へ登録しない」という
    //   条件を、呼ぶ側ではなく衝突世界の中で守っている。
    if (game_scene != nullptr && stage_asset_placed)
    {
        object_collision_bridge.Attach(&game_scene->Gameplay().GetStage());
        object_collision_bridge.SetActive(true);
        object_collision_world.AttachLegacy(&object_collision_bridge,
            ReplayEngine::Scene::LegacyStageMigrationState::stage_source_id);
    }
    else
    {
        object_collision_bridge.Detach();
        object_collision_world.DetachLegacy();
    }

    // Scene 側の設定を毎フレーム反映する。
    // Inspector から Backend Mode を変えた直後のフレームから効く。
    ReplayEngine::Scene::SceneServices& services = scene.Services();
    object_collision_world.SetBackendMode(
        ReplayEngine::Scene::SceneCollisionWorld::BackendModeFromInt(
            services.CollisionBackendMode()));
    object_collision_world.LegacyMigration() = services.LegacyStageMigration();
    services.SetPhysics(&object_collision_world);

    // 登録表の突き合わせと、Cook / Transform の更新。
    // Scene の構成が変わっていないフレームでは全走査しない。
    object_collision_world.Refresh();
}

void framework::dispatch_collision_triggers()
{
    // 位置が確定してから呼ぶこと。FixedUpdate の途中で呼ぶと、
    // まだ押し戻されていない位置で Enter が出てしまう。
    if (!object_runtime_active()) return;
    object_collision_world.DispatchTriggerEvents();
}
