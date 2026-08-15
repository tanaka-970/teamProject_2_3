#pragma once

#include "ComponentTypeID.h"
#include "TriggerContact.h"
#include "../../Core/ObjectID/RuntimeIdentity.h"
#include "../../Core/Threading/ThreadPolicy.h"

#include <cstdint>
#include <memory>
#include <vector>

// PropertyDesc.h はこのヘッダを include するため、こちらからは前方宣言だけにする。
// DynamicProperties() が返すのはポインタなので、不完全型のままで足りる。
namespace ReplayEngine::Reflection { class PropertyBag; class PropertyDesc; }

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
        // 定義は Component.cpp。未知プロパティ保持用の unique_ptr が
        // 不完全型 (PropertyBag) を指すため、ヘッダで暗黙生成させない。
        virtual ~Component();

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

        // ---- Runtime 用の追加ライフサイクル ---------------------------------
        //
        // OnAttach / OnStart との違い:
        //
        //   OnAttach        … GameObject へ結線された直後。Editor で Component を
        //                     追加しただけでも呼ばれる。プロパティはまだ入っていない。
        //   OnRuntimeAwake  … Scene が動き始めた最初の同期点で一度だけ。
        //                     プロパティ反映・親子復元・参照付け替えがすべて済んでいる。
        //                     無効な Component でも必ず一度呼ばれる。
        //   OnStart         … 初めて実際に有効になったときに一度だけ。
        //                     無効のままなら有効化されるまで呼ばれない。
        //
        // なぜ OnAttach では足りないか:
        //   OnAttach の時点ではプロパティが未反映で、参照先の GameObject も
        //   まだ生成されていない。そこでゲーム処理を始めると、
        //   「Editor で置いただけで動き出す」「参照が null」の両方が起きる。
        //
        // 呼び出しは Scene の同期点から。Component 側からは呼ばない。
        virtual void OnRuntimeAwake() {}

        // 実体が破棄される直前に一度だけ。順序は OnDisable -> OnRuntimeDestroy -> OnDetach。
        //
        // OnDetach と役割を分けている理由:
        //   OnDetach は「GameObject との内部接続を外す」ための後始末で、
        //   Scene からの取り外しでも呼ばれる。
        //   OnRuntimeDestroy は「このインスタンスの一生が終わる」ことを表す。
        //   購読の解除やイベント発行はこちらで行う。
        //   この時点ではまだ Owner とプロパティへ安全に触れる。
        virtual void OnRuntimeDestroy() {}

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

        // Update / FixedUpdate / LateUpdate の各フェーズ内だけで使う順序キー。
        // 既定 0 は保存済み Scene の従来順を変えない。
        virtual std::int32_t ExecutionOrder() const noexcept { return 0; }

        // ---- 保存 ----------------------------------------------------------
        // 既定の保存経路は PropertyRegistry。ここへ登録したプロパティは
        // Inspector の表示と Scene の保存の両方から同じ定義が使われる（二重定義しない）。
        //
        // 下の 2 つは「PropertyRegistry では表現しきれない値」だけを追加で扱うための逃げ道。
        // 例: 可変長の配列、外部でクックしたキャッシュのパスなど。
        // 通常の数値・文字列プロパティにはこれを使わないこと。

        virtual void OnSerialize(Reflection::PropertyBag& /*output*/) const {}
        virtual void OnDeserialize(const Reflection::PropertyBag& /*input*/) {}

        // ---- 未知プロパティの保持 ------------------------------------------
        //
        // 読み込んだ Scene に、この型が知らないプロパティが含まれていた場合、
        // それを捨てずにここへ丸ごと預かる。
        //
        // なぜ必要か:
        //   新しいビルドで足したプロパティを含む Scene を、古いビルドで開いて
        //   保存すると、知らないぶんが消える。C# Script を載せると
        //   「Compile が通っていない間に Scene を保存した」だけで
        //   Field が全部飛ぶことになり、実害が大きい。
        //
        // 扱いの約束:
        //   - 保存時に必ずそのまま書き戻す（PropertyRegistry::Capture が合流させる）
        //   - 同名のプロパティが後から登録されたら、そちらへ復元して預かりを解く
        //     （Rehydrate。PropertyRegistry::Apply が行う）
        //   - Component の実処理はここを一切見ない。あくまで通過させるだけ。
        //
        // 何も預かっていない場合は nullptr。Component 1 個あたりの
        // 追加コストをポインタ 1 本に抑えるため、必要になってから確保する。
        const Reflection::PropertyBag* UnknownProperties() const noexcept
        {
            return unknown_properties_.get();
        }

        // 預かり内容を差し替える。読み込み処理だけが呼ぶ。
        //
        // どちらも定義は Component.cpp。unique_ptr の解放には PropertyBag の
        // 完全な型が要るため、前方宣言しかないこのヘッダでは実装を書けない。
        void RetainUnknownProperties(const Reflection::PropertyBag& properties);
        void ClearUnknownProperties() noexcept;

        // Editor や読み込み処理がプロパティを書き換えた直後に呼ばれる。
        // 値の変更に応じて内部キャッシュを作り直したいときに使う。
        // property_name は PropertyRegistry の登録名。まとめて変わった場合は nullptr。
        virtual void OnPropertyChanged(const char* /*property_name*/) {}

        // Motion Mixer がこの Component の Property を実際に駆動した直後だけ呼ばれる。
        // Scene 読み込みや Inspector の通常編集と区別したい一時 Override 用の入口。
        // 既定は何もしないため、既存 Component の挙動は変わらない。
        virtual void OnMotionPropertyApplied(const char* /*property_name*/) {}

      
        // 【名前の衝突】
        //   静的な登録名と同じ名前を返さないこと。
        //   PropertyRegistry は静的側を先に見るため、重なると動的側が無視される。
        //   Script は "field." 接頭辞を付けて構造的に分けている。
        virtual const std::vector<Reflection::PropertyDesc>*
            DynamicProperties() const noexcept
        {
            return nullptr;
        }

       
        // ScriptComponent だけが true を返す。
        // ユーザーが書くスクリプトでは「無効なオブジェクトの初期化が走らない」方が
        // 予測しやすく、Unity と同じ挙動になるためと一旦は仮定
        //
        // 【注意】
        //   true にすると「Awake が一度も走っていない Component」が存在しうる。
        //   その状態で破棄されると OnRuntimeDestroy だけが呼ばれるので、
        //   実装側は「初期化済みかどうか」を自分で確かめること。
        virtual bool DeferAwakeUntilObjectActive() const noexcept { return false; }

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

        // ---- 識別 ----------------------------------------------------------
        //
        // 2 種類ある。役割が違うので混同しないこと。
        //
        //   StableID   … 保存する。所有 GameObject の中で安定した番号。
        //                Scene / Prefab をまたいでも同じ値。Component の並びを
        //                入れ替えても変わらない。ComponentReference の保存に使う。
        //
        //   InstanceID … 保存しない。World の中で実体へ一度だけ振られる通し番号。
        //                再利用しないので、消して作り直すと必ず別の値になる。
        //                ComponentHandle が古い実体を掴まないための照合に使う。
        //
        // どちらも GameObject へ結線された時点で割り当てられる。
        // 結線前 (Registry が作っただけの状態) は両方 0。

        ComponentStableID StableID() const noexcept { return stable_id_; }
        ComponentInstanceID InstanceID() const noexcept { return instance_id_; }

        // ---- 状態 ----------------------------------------------------------

        bool Attached() const noexcept { return attached_; }
        bool Started() const noexcept { return started_; }
        bool PendingDestroy() const noexcept { return pending_destroy_; }

        // 削除を予約する。実際の破棄は Scene の同期点。二重に呼んでも安全。
        void Destroy() noexcept;

    protected:
        // 定義は Component.cpp。
        //
        // ここで = default にできない理由:
        //   未知プロパティ保持用の unique_ptr<PropertyBag> をメンバに持っており、
        //   PropertyBag はこのヘッダでは前方宣言しかない。
        //   コンストラクタを暗黙生成させると、途中で例外が出た場合の巻き戻し用に
        //   メンバのデストラクタが必要になり、不完全型のまま実体化しようとして失敗する。
        //   派生クラスを生成する翻訳単位すべてでこれが起きるため、外へ出してある。
        Component();

    private:
        friend class GameObject;
        friend class ReplayEngine::Scene::Scene;

        // GameObject / Scene だけが触るライフサイクル駆動部。
        void AttachTo(GameObject* owner);
        void SyncEnableState();     // OnRuntimeAwake / OnEnable / OnDisable / OnStart を呼ぶ
        void ForceDisable();        // 破棄前に有効状態を落とす
        void RaiseRuntimeDestroy(); // OnRuntimeDestroy を一度だけ呼ぶ
        void MarkPendingDestroy() noexcept { pending_destroy_ = true; }

        // 識別番号の割り当て。GameObject::AttachComponent だけが呼ぶ。
        // 一度割り当てた値は上書きしない（保存済みの参照が指す先を変えないため）。
        void AssignIdentity(ComponentStableID stable_id,
            ComponentInstanceID instance_id) noexcept
        {
            if (stable_id_ == invalid_component_stable_id)
            {
                stable_id_ = stable_id;
            }
            if (instance_id_ == invalid_component_instance_id)
            {
                instance_id_ = instance_id;
            }
        }

        // 読み込み・複製が「保存されていた StableID」を復元するための入口。
        // 結線前にだけ意味がある。結線後に呼んでも無視される。
        void RestoreStableIDBeforeAttach(ComponentStableID stable_id) noexcept
        {
            if (!attached_) stable_id_ = stable_id;
        }

        GameObject* owner_ = nullptr;

        // 型が知らないまま預かっているプロパティ。無ければ nullptr。
        std::unique_ptr<Reflection::PropertyBag> unknown_properties_;

        ComponentStableID stable_id_ = invalid_component_stable_id;
        ComponentInstanceID instance_id_ = invalid_component_instance_id;

        bool enabled_ = true;
        bool attached_ = false;
        bool started_ = false;
        bool enable_state_applied_ = false;
        bool pending_destroy_ = false;
        bool runtime_awake_called_ = false;
        bool runtime_destroy_called_ = false;
    };
}
