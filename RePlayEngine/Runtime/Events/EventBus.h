#pragma once

#include "../Handles/RuntimeHandles.h"
#include "../../Reflection/Property/PropertyBag.h"
#include "../../Reflection/Registry/TypeGUID.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace ReplayEngine::Runtime
{
    class EventBus;

    // 配送されるイベント 1 件分。
    //
    // 型付きの構造体をそのまま配らず、この形へ寄せている理由:
    //   将来 C# 側の購読者へ渡すとき、C++ のテンプレート実体をそのまま
    //   渡すことはできない。最初から「型 GUID + 名前付き値の袋」という
    //   型消去された形にしておけば、Native と Managed の購読者が
    //   同じ配送経路に並べる。
    //
    //   Gameplay Event（GoalReached など）を Engine Core へ 1 つずつ
    //   ハードコードしないための形でもある。イベント種別は登録された
    //   TypeGUID でしかなく、Engine 側に専用のフィールドは増えない。
    struct EventRecord final
    {
        // イベント種別。Engine Event も Gameplay Event も同じ表現。
        Reflection::TypeGUID type;

        // 表示・ログ用の名前。識別には使わない（識別は必ず type）。
        std::string type_name;

        // 発生元。無くてもよい。
        ObjectHandle source;

        // 宛先。指定されている場合、その Object が生きていなければ配送しない。
        ObjectHandle target;

        // 付随データ。Scene の Property と同じ型体系なので、
        // そのまま保存も Inspector 表示もできる。
        Reflection::PropertyBag payload;

        // 発行されたフレーム。
        std::uint64_t frame_index = 0;
    };

    // 購読 1 件の識別子。
    using SubscriptionID = std::uint64_t;
    inline constexpr SubscriptionID invalid_subscription_id = 0;

    // 購読の寿命を持つトークン。
    //
    // 破棄すると自動的に購読解除される（RAII）。
    // Behaviour がメンバとして持てば、Behaviour の破棄で確実に解除される。
    //
    // 生ポインタで購読者を識別しない理由:
    //   購読者が先に消えたことを Bus 側から知る手段が無く、
    //   配送のたびに解放済みメモリを呼ぶ危険が残る。
    //   トークンで寿命を縛るか、Handle で生存を確かめるかのどちらかに限定する。
    class ScopedSubscription final
    {
    public:
        ScopedSubscription() noexcept = default;
        ScopedSubscription(EventBus* bus, SubscriptionID id) noexcept
            : bus_(bus), id_(id) {}

        ~ScopedSubscription();

        ScopedSubscription(const ScopedSubscription&) = delete;
        ScopedSubscription& operator=(const ScopedSubscription&) = delete;

        ScopedSubscription(ScopedSubscription&& other) noexcept
            : bus_(other.bus_), id_(other.id_)
        {
            other.bus_ = nullptr;
            other.id_ = invalid_subscription_id;
        }
        ScopedSubscription& operator=(ScopedSubscription&& other) noexcept;

        bool Valid() const noexcept
        {
            return bus_ != nullptr && id_ != invalid_subscription_id;
        }
        SubscriptionID ID() const noexcept { return id_; }

        void Release() noexcept;

    private:
        EventBus* bus_ = nullptr;
        SubscriptionID id_ = invalid_subscription_id;
    };

    // イベントの配送先。
    //
    // Scene 単位と Application 単位を分けている理由:
    //   Scene を切り替えると、その World の購読はすべて意味を失う。
    //   まとめて捨てられる単位に分けておけば、解除漏れが構造的に起きない。
    //   一方「アプリ終了要求」のような通知は Scene をまたいで生き続ける必要がある。
    enum class EventScope : std::int32_t
    {
        Scene = 0,   // World の破棄で自動的に全解除
        Global = 1,  // 明示的に解除するまで残る
    };

    // Engine が発行する組み込みイベントの型 GUID。
    //
    // Gameplay Event はここへ足さない。ゲーム側が自分の GUID を決めて発行する。
    namespace EngineEvents
    {
        // Scene 遷移まわり。発行は Phase 6/7 の RuntimeSceneService が行う。
        // GUID だけ先に確定させておき、購読側を先に書けるようにしてある。
        inline constexpr Reflection::TypeGUID BeforeSceneUnload =
            Reflection::MakeTypeGUID("a1000000000000000000000000000001");
        inline constexpr Reflection::TypeGUID SceneLoadRequested =
            Reflection::MakeTypeGUID("a1000000000000000000000000000002");
        inline constexpr Reflection::TypeGUID SceneLoadStarted =
            Reflection::MakeTypeGUID("a1000000000000000000000000000003");
        inline constexpr Reflection::TypeGUID SceneLoaded =
            Reflection::MakeTypeGUID("a1000000000000000000000000000004");
        inline constexpr Reflection::TypeGUID SceneLoadFailed =
            Reflection::MakeTypeGUID("a1000000000000000000000000000005");
        inline constexpr Reflection::TypeGUID WorldChanged =
            Reflection::MakeTypeGUID("a1000000000000000000000000000006");
        inline constexpr Reflection::TypeGUID ApplicationQuitRequested =
            Reflection::MakeTypeGUID("a1000000000000000000000000000007");
        inline constexpr Reflection::TypeGUID MotionEvent =
            Reflection::MakeTypeGUID("a1000000000000000000000000000008");
        inline constexpr Reflection::TypeGUID ButtonStateChanged =
            Reflection::MakeTypeGUID("a1000000000000000000000000000009");
        inline constexpr Reflection::TypeGUID StateChanged =
            Reflection::MakeTypeGUID("a1000000000000000000000000000010");
        inline constexpr Reflection::TypeGUID InputFieldSubmitted =
            Reflection::MakeTypeGUID("a1000000000000000000000000000011");
        inline constexpr Reflection::TypeGUID InputFieldCanceled =
            Reflection::MakeTypeGUID("a1000000000000000000000000000012");
    }

    // イベントの発行と購読。
    //
    // ---------------------------------------------------------------------
    // 【配送の約束】
    //
    //   - 配送は必ず遅延。Publish は待ち行列へ積むだけで、その場では呼ばない。
    //     配送中に別のイベントが発行されても、走査中の配列が伸びて壊れることがない。
    //   - 同一フレームの配送順は「発行順」で決まる。決定的。
    //   - 配送中の Subscribe は次回から有効。配送中の Unsubscribe は即時有効。
    //   - source / target が無効な Handle になっていたら配送しない（数だけ記録する）。
    //   - 同じイベントが再帰的に発行され続けた場合は上限で打ち切り、診断へ残す。
    //
    // ---------------------------------------------------------------------
    // 所有関係:
    //   Scene 単位の Bus は RuntimeContext が所有する。World と一緒に消える。
    //   Global 単位の Bus はプロセスに 1 つ。World の入れ替えでは消えない。
    class EventBus final
    {
    public:
        using Handler = std::function<void(const EventRecord&)>;

        EventBus() = default;
        ~EventBus() = default;

        EventBus(const EventBus&) = delete;
        EventBus& operator=(const EventBus&) = delete;

        // プロセス全体で 1 つの Global Bus。
        static EventBus& Global() noexcept;

        // ---- 購読 --------------------------------------------------------------

        // owner を渡すと、その Object が生きている間だけ配送される。
        // 空 Handle を渡した場合は生存確認を行わない（World 全体の監視用）。
        ScopedSubscription Subscribe(Reflection::TypeGUID type, Handler handler,
            const ObjectHandle& owner = ObjectHandle::None());

        // トークンを使わずに解除したい場合。通常は ScopedSubscription を使うこと。
        bool Unsubscribe(SubscriptionID id) noexcept;

        // ある Object に属する購読をまとめて解除する。
        // Behaviour が破棄されたときの取りこぼしを防ぐ保険。
        std::size_t UnsubscribeOwner(const ObjectHandle& owner) noexcept;

        // ---- 発行 --------------------------------------------------------------

        // 待ち行列へ積む。その場では配送しない。
        void Publish(EventRecord record);

        // 待ち行列を配送する。Scene の同期点で 1 回だけ呼ぶ。
        //
        // resolver を渡すと、source / target の生存確認を行う。
        // nullptr なら生存確認を省く（Global Bus 用）。
        void Dispatch(const class HandleResolver* resolver);

        // ---- 状態 --------------------------------------------------------------

        std::size_t SubscriberCount() const noexcept { return subscriptions_.size(); }
        std::size_t PendingEventCount() const noexcept { return queue_.size(); }
        std::uint64_t DispatchedEventCount() const noexcept { return dispatched_count_; }
        std::uint64_t DroppedEventCount() const noexcept { return dropped_count_; }
        bool RecursionLimitHit() const noexcept { return recursion_limit_hit_; }

        // World の破棄時に呼ぶ。購読も待ち行列もすべて捨てる。
        void Clear() noexcept;

    private:
        struct Subscription
        {
            SubscriptionID id = invalid_subscription_id;
            Reflection::TypeGUID type;
            ObjectHandle owner;
            Handler handler;
            bool alive = true;
        };

        // alive=false の購読を実際に取り除く。配送中は呼ばない。
        void CompactSubscriptions() noexcept;

        // 1 回の Dispatch で配送し直す回数の上限。
        // 配送の中から発行されたイベントを同じフレームで配り続けると、
        // 相互に発行し合う 2 つの Behaviour で止まらなくなる。
        static constexpr int maximum_dispatch_rounds = 8;

        std::vector<Subscription> subscriptions_;
        std::vector<EventRecord> queue_;
        std::vector<EventRecord> dispatching_;

        SubscriptionID next_id_ = 1;
        std::uint64_t dispatched_count_ = 0;
        std::uint64_t dropped_count_ = 0;
        bool recursion_limit_hit_ = false;
        bool dispatching_now_ = false;
    };
}
