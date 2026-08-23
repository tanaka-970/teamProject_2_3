#pragma once

#include "BehaviourEvents.h"
#include "../Handles/RuntimeHandles.h"
#include "../../Object/Component/Component.h"

#include <cstdint>

namespace ReplayEngine::Runtime
{
    class RuntimeContext;

    // ゲームロジックを書くための Component 基底。
    //
    // ---------------------------------------------------------------------
    // 【なぜ専用の Update 経路を作らないか】
    //
    //   Behaviour 専用の更新マネージャを別に持つと、
    //   「Scene が回す Component」と「マネージャが回す Behaviour」の 2 経路ができる。
    //   どちらが先か、削除予約はどちらが見るか、Play 停止でどちらが止まるか、
    //   といった食い違いがそのままバグになる。
    //
    //   BehaviourComponent は Core::Component をそのまま継承し、
    //   Update / FixedUpdate / LateUpdate / Enable / Disable / Start は
    //   基底クラスの仮想関数を「そのまま」使う。
    //   Scene の既存ループ以外に Behaviour を回す場所は存在しない。
    //
    //   つまり、派生 Behaviour は次をそのまま override すればよい。
    //     void OnStart() override;
    //     void OnUpdate(float delta_time) override;
    //     void OnFixedUpdate(float fixed_delta_time) override;
    //     void OnLateUpdate(float delta_time) override;
    //     void OnEnable() override;  /  void OnDisable() override;
    //
    // ---------------------------------------------------------------------
    // 【BehaviourComponent が足すもの】
    //
    //   OnAwake        … Property 反映・参照解決が済んだあと、一度だけ。無効でも呼ばれる。
    //   OnDestroy      … 実体の破棄直前に一度だけ。購読解除はここ。
    //   OnTriggerXxx   … TriggerEvent 版（Handle 付き）。TriggerContact 版は使わない。
    //   OnCollisionXxx … CollisionEvent 版。取得できる範囲は BehaviourEvents.h を参照。
    //   Self / SelfHandle / Runtime … 安全な参照と Runtime API への入口。
    //   execution_order … 同一フレーム内の呼び出し順の拡張余地。
    //
    // ---------------------------------------------------------------------
    // 【ライフサイクル順序】
    //
    //   AddComponent            -> OnAttach
    //   Scene::Start() の同期点 -> OnAwake                （無効でも 1 回）
    //   有効になった同期点      -> OnEnable -> OnStart    （OnStart は 1 回だけ）
    //   毎フレーム              -> OnFixedUpdate / OnUpdate / OnLateUpdate
    //   無効化                  -> OnDisable
    //   破棄                    -> OnDisable -> OnDestroy -> OnDetach
    //
    //   Trigger / Collision は OnAwake 後、かつ有効な間だけ届く。
    //   削除予約済みの Component へは届かない。
    class BehaviourComponent : public Core::Component
    {
    public:
        // 同一フレーム内での呼び出し順のヒント。小さいほど先。
        //
        // Scene の Update / FixedUpdate / LateUpdate 内で参照される。
        std::int32_t execution_order = 0;

        std::int32_t ExecutionOrder() const noexcept override
        {
            return execution_order;
        }

        // ---- 安全な参照 ----------------------------------------------------

        // 自分の所有 GameObject。World が入れ替わると自動的に無効になる。
        ObjectHandle SelfHandle() const noexcept;

        // 自分自身。Component を消して作り直しても古い Handle は無効のまま。
        ComponentHandle SelfComponentHandle() const noexcept;

        // Runtime API への入口。接続されていなければ nullptr。
        //
        // nullptr を返しうる理由:
        //   Editor で Scene を編集しているだけの状態では Runtime を接続しない。
        //   「置いただけで Runtime API を叩き始める」ことを構造的に防ぐ。
        RuntimeContext* Runtime() const noexcept;

        // Awake が済んだか。Diagnostics 表示用。
        bool AwakeCalled() const noexcept { return awake_called_; }

    protected:
        BehaviourComponent() = default;

        // ---- Behaviour 用ライフサイクル --------------------------------------

        virtual void OnAwake() {}
        virtual void OnDestroy() {}

        // ---- Trigger ---------------------------------------------------------
        //
        // Trigger を持つ側と、入った側の「両方」へ届く。
        // 自分がどちらかは event.self_is_trigger で判別する。

        virtual void OnTriggerEnter(const TriggerEvent& /*event*/) {}
        virtual void OnTriggerStay(const TriggerEvent& /*event*/) {}
        virtual void OnTriggerExit(const TriggerEvent& /*event*/) {}

        // ---- Collision --------------------------------------------------------
        //
        // CharacterMotor の接地・壁接触と Rigidbody の一般接触が届く。
        // 詳細は BehaviourEvents.h の CollisionHitKind を参照。

        virtual void OnCollisionEnter(const CollisionEvent& /*event*/) {}
        virtual void OnCollisionStay(const CollisionEvent& /*event*/) {}
        virtual void OnCollisionExit(const CollisionEvent& /*event*/) {}

    private:
        friend class BehaviourEventDispatch;

        // ---- 基底の仮想関数の受け口 -------------------------------------------
        //
        // final を付ける理由:
        //   派生 Behaviour が TriggerContact 版を override してしまうと、
        //   TriggerEvent 版と二重に処理が走る。入口を 1 つに固定する。

        void OnRuntimeAwake() final;
        void OnRuntimeDestroy() final;

        void OnTriggerEnter(const Core::TriggerContact& contact) final;
        void OnTriggerStay(const Core::TriggerContact& contact) final;
        void OnTriggerExit(const Core::TriggerContact& contact) final;

        // TriggerContact を TriggerEvent へ組み立て直す。
        // 自分がどちら側かの判別と Handle 化をここで行う。
        bool BuildTriggerEvent(const Core::TriggerContact& contact,
            ContactPhase phase, TriggerEvent& out) const;

        bool awake_called_ = false;
    };

    // Behaviour の protected なイベント受け口を外から呼ぶための唯一の入口。
    //
    // public にしない理由:
    //   OnCollisionEnter などを誰でも呼べるようにすると、
    //   「配送していないのに呼ばれた」経路がいくらでも作れてしまう。
    //   配送元を 1 か所（CollisionEventDispatcher）に固定するため、
    //   friend 経由の細い口だけを開ける。
    //
    // 生存確認と有効判定は呼び出し側の責任。ここでは呼ぶだけ。
    class BehaviourEventDispatch final
    {
    public:
        BehaviourEventDispatch() = delete;

        static void Collision(BehaviourComponent& behaviour, const CollisionEvent& event);
    };
}
