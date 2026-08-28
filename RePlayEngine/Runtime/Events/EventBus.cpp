#include "EventBus.h"

#include "../Handles/HandleResolver.h"

#include <algorithm>

namespace ReplayEngine::Runtime
{
    // ---- ScopedSubscription ------------------------------------------------

    ScopedSubscription::~ScopedSubscription()
    {
        Release();
    }

    ScopedSubscription& ScopedSubscription::operator=(ScopedSubscription&& other) noexcept
    {
        if (this == &other) return *this;

        Release();
        bus_ = other.bus_;
        id_ = other.id_;
        other.bus_ = nullptr;
        other.id_ = invalid_subscription_id;
        return *this;
    }

    void ScopedSubscription::Release() noexcept
    {
        if (bus_ != nullptr && id_ != invalid_subscription_id)
        {
            bus_->Unsubscribe(id_);
        }
        bus_ = nullptr;
        id_ = invalid_subscription_id;
    }

    // ---- EventBus ----------------------------------------------------------

    EventBus& EventBus::Global() noexcept
    {
        // 関数ローカル static。翻訳単位をまたぐ静的初期化順序に依存しない。
        static EventBus bus;
        return bus;
    }

    ScopedSubscription EventBus::Subscribe(Reflection::TypeGUID type, Handler handler,
        const ObjectHandle& owner)
    {
        if (!type.IsValid() || !handler) return ScopedSubscription{};

        Subscription subscription;
        subscription.id = next_id_++;
        subscription.type = type;
        subscription.owner = owner;
        subscription.handler = std::move(handler);

        // 配送中に追加された購読も、この回の配送では呼ばれない。
        // Dispatch 側が「開始時点の件数」ではなく alive フラグで判断するため、
        // 追加された購読は次の回から有効になる。
        subscriptions_.push_back(std::move(subscription));
        return ScopedSubscription{ this, subscriptions_.back().id };
    }

    bool EventBus::Unsubscribe(SubscriptionID id) noexcept
    {
        if (id == invalid_subscription_id) return false;

        for (Subscription& subscription : subscriptions_)
        {
            if (subscription.id != id || !subscription.alive) continue;

            // 配送の最中でも安全に消せるよう、その場では詰めずに印だけ付ける。
            // 実際の削除は配送が終わってから行う。
            subscription.alive = false;
            if (!dispatching_now_) CompactSubscriptions();
            return true;
        }
        return false;
    }

    std::size_t EventBus::UnsubscribeOwner(const ObjectHandle& owner) noexcept
    {
        if (owner.IsEmpty()) return 0;

        std::size_t removed = 0;
        for (Subscription& subscription : subscriptions_)
        {
            if (!subscription.alive || subscription.owner != owner) continue;
            subscription.alive = false;
            ++removed;
        }
        if (removed != 0 && !dispatching_now_) CompactSubscriptions();
        return removed;
    }

    void EventBus::CompactSubscriptions() noexcept
    {
        subscriptions_.erase(
            std::remove_if(subscriptions_.begin(), subscriptions_.end(),
                [](const Subscription& subscription) { return !subscription.alive; }),
            subscriptions_.end());
    }

    bool EventBus::HasSubscribers(Reflection::TypeGUID type) const noexcept
    {
        if (!type.IsValid()) return false;
        for (const Subscription& subscription : subscriptions_)
        {
            if (subscription.alive && subscription.type == type) return true;
        }
        return false;
    }

    void EventBus::Publish(EventRecord record)
    {
        if (!record.type.IsValid()) return;
        if (queue_.size() >= maximum_pending_events)
        {
            ++dropped_count_;
            return;
        }
        queue_.push_back(std::move(record));
    }

    void EventBus::Dispatch(const HandleResolver* resolver)
    {
        if (dispatching_now_) return;   // 再入防止
        dispatching_now_ = true;
        recursion_limit_hit_ = false;

        for (int round = 0; round < maximum_dispatch_rounds; ++round)
        {
            if (queue_.empty()) break;

            // 待ち行列を丸ごと引き取ってから配る。
            // 配送中に Publish されたぶんは queue_ へ積まれ、次の回で配られる。
            // これにより、走査中の配列が伸びて反復子が壊れることがない。
            dispatching_.clear();
            dispatching_.swap(queue_);

            for (const EventRecord& record : dispatching_)
            {
                // 宛先が指定されていて、既に消えている場合は配送しない。
                if (resolver != nullptr && !record.target.IsEmpty() &&
                    !resolver->IsValid(record.target))
                {
                    ++dropped_count_;
                    continue;
                }

                // 添字で回す。ハンドラの中から Subscribe されても
                // 実体のアドレスが動く可能性があるため、参照を保持しない。
                const std::size_t count = subscriptions_.size();
                for (std::size_t index = 0; index < count && index < subscriptions_.size();
                    ++index)
                {
                    if (!subscriptions_[index].alive) continue;
                    if (subscriptions_[index].type != record.type) continue;

                    // 購読者が属する Object が消えていれば配送しない。
                    const ObjectHandle owner = subscriptions_[index].owner;
                    if (resolver != nullptr && !owner.IsEmpty() && !resolver->IsValid(owner))
                    {
                        subscriptions_[index].alive = false;
                        ++dropped_count_;
                        continue;
                    }

                    // handler をコピーしてから呼ぶ。
                    // ハンドラの中で自分自身を Unsubscribe しても、
                    // 実行中の std::function が消えないようにするため。
                    Handler handler = subscriptions_[index].handler;
                    if (handler) handler(record);
                }
                ++dispatched_count_;
            }

            if (!queue_.empty() && round == maximum_dispatch_rounds - 1)
            {
                // 配送の中から発行され続けている。無限ループを避けて打ち切る。
                recursion_limit_hit_ = true;
                dropped_count_ += queue_.size();
                queue_.clear();
            }
        }

        dispatching_.clear();
        dispatching_now_ = false;
        CompactSubscriptions();
    }

    void EventBus::Clear() noexcept
    {
        subscriptions_.clear();
        queue_.clear();
        dispatching_.clear();
        dispatched_count_ = 0;
        dropped_count_ = 0;
        recursion_limit_hit_ = false;
        dispatching_now_ = false;
    }
}
