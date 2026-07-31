#pragma once

#include "ComponentTypeID.h"
#include "TriggerContact.h"
#include "../../Core/Threading/ThreadPolicy.h"

namespace ReplayEngine::Reflection { class PropertyBag; }

namespace ReplayEngine::Scene { class Scene; }

namespace ReplayEngine::Core
{
    class GameObject;

    // GameObject へ付けられる機能の基底クラス。
    //
    // 所有関係:
    //   GameObject が Component を unique_ptr で所有する。
    //   Component は GameObject を「非所有の生ポインタ」で参照するだけで、決して所有しない。
    //   Component 単体で Scene に存在することはない。
    //
    // ライフサイクル:
    //   AddComponent           -> OnAttach          （即時。GameObject へ結線された直後）
    //   最初に有効になった時    -> OnEnable -> OnStart（Scene の更新前の同期点で呼ばれる）
    //   毎フレーム             -> OnUpdate / OnFixedUpdate / OnLateUpdate
    //   無効化                 -> OnDisable
    //   削除                   -> (有効なら OnDisable) -> OnDetach -> 破棄
    //
    //   OnStart は「Scene が開始済み」かつ「自分と所有 GameObject が有効」になった最初の
    //   タイミングで一度だけ呼ばれる。以降は無効化・再有効化しても呼ばれない。
    //
    // 削除の安全性:
    //   Destroy() は即座に delete せず、Scene の遅延操作キューへ積むだけ。
    //   実際の破棄はフレーム末尾の同期点で行われるため、
    //   OnUpdate の中から自分自身を Destroy() してもその場でクラッシュしない。
    //
    // コピー:
    //   コピーもムーブも禁止する。Component は GameObject に結び付いた実体であり、
    //   所有者と切り離して複製すると owner_ の意味が壊れるため。
    //   複製が必要な場合は ComponentRegistry を通して生成し、
    //   PropertyRegistry でプロパティを写すこと（GameObject::CloneComponentsFrom を参照）。
    class Component
    {
    public:
        virtual ~Component() = default;

        Component(const Component&) = delete;
        Component& operator=(const Component&) = delete;
        Component(Component&&) = delete;
        Component& operator=(Component&&) = delete;

        // ---- 型情報 (REPLAY_COMPONENT_BODY が実装する) --------------------

        virtual ComponentTypeID TypeID() const noexcept = 0;
        virtual const char* TypeName() const noexcept = 0;

        // ---- ライフサイクル ----------------------------------------------

        virtual void OnAttach() {}
        virtual void OnStart() {}
        virtual void OnEnable() {}
        virtual void OnDisable() {}
        virtual void OnUpdate(float /*delta_time*/) {}
        virtual void OnFixedUpdate(float /*fixed_delta_time*/) {}
        virtual void OnLateUpdate(float /*delta_time*/) {}
        virtual void OnDetach() {}

        // ---- Trigger イベント ------------------------------------------------
        //
        // 同じ接触ペアに対して
        //   未接触 -> Enter、接触継続 -> Stay、接触終了 -> Exit
        // の順で 1 回ずつ届く。接触している間ずっと Enter が届くことはない。
        //
        // 引数はすべて ObjectID / ColliderID。生ポインタは渡らない。
        // 受け取った側は Scene から引き直して、生きているかを必ず確かめること。
        //
        // 呼ばれるのは Trigger 側と、Trigger へ入った側の「両方」の GameObject。
        // どちらの立場かは contact.trigger_object と Owner()->ID() を見て判断する。
        virtual void OnTriggerEnter(const TriggerContact& /*contact*/) {}
        virtual void OnTriggerStay(const TriggerContact& /*contact*/) {}
        virtual void OnTriggerExit(const TriggerContact& /*contact*/) {}

        // ---- スレッド区分 --------------------------------------------------
        // 既定は MainThreadOnly。現時点では Scene が区分によらずメインスレッドで
        // 順番に実行するため挙動は変わらないが、将来並列化する際の宣言として使う。

        virtual ThreadPolicy GetThreadPolicy() const noexcept
        {
            return ThreadPolicy::MainThreadOnly;
        }

        // ---- 保存 ----------------------------------------------------------
        // 既定の保存経路は PropertyRegistry。ここへ登録したプロパティは
        // Inspector の表示と Scene の保存の両方から同じ定義が使われる（二重定義しない）。
        //
        // 下の 2 つは「PropertyRegistry では表現しきれない値」だけを追加で扱うための逃げ道。
        // 例: 可変長の配列、外部でクックしたキャッシュのパスなど。
        // 通常の数値・文字列プロパティにはこれを使わないこと。

        virtual void OnSerialize(Reflection::PropertyBag& /*output*/) const {}
        virtual void OnDeserialize(const Reflection::PropertyBag& /*input*/) {}

        // Editor や読み込み処理がプロパティを書き換えた直後に呼ばれる。
        // 値の変更に応じて内部キャッシュを作り直したいときに使う。
        // property_name は PropertyRegistry の登録名。まとめて変わった場合は nullptr。
        virtual void OnPropertyChanged(const char* /*property_name*/) {}

        // ---- 所有者 --------------------------------------------------------

        GameObject* Owner() const noexcept { return owner_; }

        // 所有 GameObject が属する Scene。GameObject 未接続なら nullptr。
        Scene::Scene* GetScene() const noexcept;

        // ---- 有効・無効 ----------------------------------------------------

        // この Component 自身のフラグ。親や GameObject の状態は見ない。
        bool Enabled() const noexcept { return enabled_; }

        // 実際に更新されるかどうか。自身 && 所有 GameObject の階層有効状態。
        bool ActiveInHierarchy() const noexcept;

        // 有効・無効の切り替え。OnEnable / OnDisable は即時ではなく、
        // Scene の次の同期点で対称に呼ばれる（Editor から描画中に呼んでも安全）。
        void SetEnabled(bool enabled) noexcept;

        // ---- 状態 ----------------------------------------------------------

        bool Attached() const noexcept { return attached_; }
        bool Started() const noexcept { return started_; }
        bool PendingDestroy() const noexcept { return pending_destroy_; }

        // 削除を予約する。実際の破棄は Scene の同期点。二重に呼んでも安全。
        void Destroy() noexcept;

    protected:
        Component() = default;

    private:
        friend class GameObject;
        friend class ReplayEngine::Scene::Scene;

        // GameObject / Scene だけが触るライフサイクル駆動部。
        void AttachTo(GameObject* owner);
        void SyncEnableState();     // OnEnable / OnDisable / OnStart を必要に応じて呼ぶ
        void ForceDisable();        // 破棄前に有効状態を落とす
        void MarkPendingDestroy() noexcept { pending_destroy_ = true; }

        GameObject* owner_ = nullptr;
        bool enabled_ = true;
        bool attached_ = false;
        bool started_ = false;
        bool enable_state_applied_ = false;
        bool pending_destroy_ = false;
    };
}
