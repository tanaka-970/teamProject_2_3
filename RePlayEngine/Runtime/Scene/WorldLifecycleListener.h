#pragma once

namespace ReplayEngine::Scene { class Scene; }

namespace ReplayEngine::Runtime
{
    // Runtime World の入れ替えに合わせて、World 単位の付随状態を作り直すための境界。
    //
    // ---------------------------------------------------------------------
    // 【なぜ必要か】
    //
    //   RuntimeSceneService::SwapWorlds() は、新しい World を差し替えたあと
    //   Scene::Start() を呼ぶ。Start() は全 Component の
    //   OnRuntimeAwake -> OnEnable -> OnStart を一気に流す。
    //
    //   World 単位の付随状態（Script の Play Session など）は、
    //   その OnRuntimeAwake より「前」に用意されていなければならない。
    //
    //   これを framework::enter_object_play_mode() へ書くと駄目な理由:
    //     enter_object_play_mode は Tick() を 2 回呼んだ「あと」に続きを実行する。
    //     その時点で Scene::Start() は走り終わっている。
    //     さらに SwapWorlds は SceneFlowService 経由のゲーム中 Scene 遷移でも通るため、
    //     Play Mode の開始処理だけへ書くと、遷移のたびに準備が漏れる。
    //     Editor の Play では動くのに、ゲーム中にシーンを切り替えた瞬間だけ
    //     動かなくなる、という最も気づきにくい壊れ方をする。
    //
    // ---------------------------------------------------------------------
    // 【依存の向き】
    //
    //   Runtime は実装を知らない。Scripting 層が実装し、framework が接続する。
    //   既存の ISceneAssetResolver / IPrefabInstantiator / ISceneFlow と同じ形。
    //
    //   スクリプト専用の仕掛けにはしない。将来 Behaviour が
    //   World 単位の状態を持ちたくなったときも同じフックへ乗れる。
    //
    // ---------------------------------------------------------------------
    // 【呼ばれる順序】
    //
    //   World 入れ替え:
    //     OnWorldUnloading(旧)   … 旧 World の Clear() の直前。全 Component が生きている
    //     （Scene::Clear() … OnDisable -> OnRuntimeDestroy -> OnDetach）
    //     OnWorldUnloaded(旧)    … Clear() の直後・実体解放の前
    //     （unique_ptr 差し替え / Rebind）
    //     OnWorldActivating(新)  … Scene::Start() の直前
    //     （Scene::Start() … OnRuntimeAwake -> OnEnable -> OnStart）
    //
    //   Play 停止 (ResetToEmptyWorld) でも同じ 3 つが同じ順で呼ばれる。
    //
    // 実装側は例外を投げないこと。ここは World 入れ替えの途中であり、
    // 巻き戻せる状態ではない。
    class IWorldLifecycleListener
    {
    public:
        virtual ~IWorldLifecycleListener() = default;

        virtual void OnWorldUnloading(Scene::Scene& world) = 0;
        virtual void OnWorldUnloaded(Scene::Scene& world) = 0;
        virtual void OnWorldActivating(Scene::Scene& world) = 0;
    };
}
