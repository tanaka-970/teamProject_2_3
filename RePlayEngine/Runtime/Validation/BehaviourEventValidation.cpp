#include "BehaviourValidationInternal.h"

namespace ReplayEngine::Runtime::Validation
{
    using namespace Detail::BehaviourValidation;

    // =====================================================================

    int RunEventValidation()
    {
        RegisterProbe();
        Checker check(290);

        Scene::Scene world("EventWorld");
        RuntimeContext runtime(world);
        world.Services().SetRuntime(&runtime);
        EventBus& bus = runtime.Events();

        const Reflection::TypeGUID event_a =
            Reflection::MakeTypeGUID("d0000000000000000000000000000001");
        const Reflection::TypeGUID event_b =
            Reflection::MakeTypeGUID("d0000000000000000000000000000002");

        int received_a = 0;
        int received_b = 0;

        {
            ScopedSubscription token_a = bus.Subscribe(event_a,
                [&received_a](const EventRecord&) { ++received_a; });
            ScopedSubscription token_b = bus.Subscribe(event_b,
                [&received_b](const EventRecord&) { ++received_b; });

            check.Expect(token_a.Valid() && token_b.Valid(), "購読トークンが有効");
            check.Expect(bus.SubscriberCount() == 2, "購読が 2 件登録される");

            EventRecord record;
            record.type = event_a;
            bus.Publish(record);

            check.Expect(received_a == 0,
                "Publish しただけでは配送されない（配送は必ず遅延）");
            check.Expect(bus.PendingEventCount() == 1, "待ち行列へ 1 件積まれる");

            bus.Dispatch(&runtime.Resolver());
            check.Expect(received_a == 1, "Dispatch で配送される");
            check.Expect(received_b == 0, "別の型の購読者へは配送されない");
            check.Expect(bus.PendingEventCount() == 0, "配送後は待ち行列が空になる");
        }

        // トークンが破棄されたので購読も外れているはず
        check.Expect(bus.SubscriberCount() == 0, "トークンの破棄で購読が解除される");

        {
            EventRecord record;
            record.type = event_a;
            bus.Publish(record);
            bus.Dispatch(&runtime.Resolver());
            check.Expect(received_a == 1, "解除後は配送されない");
        }

        // 配送中の Subscribe / Unsubscribe
        {
            int outer = 0;
            int inner = 0;
            ScopedSubscription inner_token;

            ScopedSubscription outer_token = bus.Subscribe(event_a,
                [&](const EventRecord&)
                {
                    ++outer;
                    // 配送中に新しい購読を足す。この回では呼ばれないこと。
                    if (!inner_token.Valid())
                    {
                        inner_token = bus.Subscribe(event_a,
                            [&inner](const EventRecord&) { ++inner; });
                    }
                });

            EventRecord record;
            record.type = event_a;
            bus.Publish(record);
            bus.Dispatch(&runtime.Resolver());

            check.Expect(outer == 1, "配送中に Subscribe しても外側は 1 回だけ");
            check.Expect(inner == 0, "配送中に足した購読はその回では呼ばれない");

            bus.Publish(record);
            bus.Dispatch(&runtime.Resolver());
            check.Expect(inner == 1, "次の配送からは新しい購読も呼ばれる");
        }

        // 購読者の GameObject が消えたら配送しない
        {
            Core::GameObject* owner_object = world.CreateGameObject("EventOwner");
            const ObjectHandle owner = runtime.Resolver().MakeHandle(owner_object);

            int owned_received = 0;
            ScopedSubscription owned = bus.Subscribe(event_a,
                [&owned_received](const EventRecord&) { ++owned_received; }, owner);

            EventRecord record;
            record.type = event_a;
            bus.Publish(record);
            bus.Dispatch(&runtime.Resolver());
            check.Expect(owned_received == 1, "生きている購読者へは配送される");

            world.DestroyGameObject(owner_object);
            world.ProcessPendingOperations();

            bus.Publish(record);
            bus.Dispatch(&runtime.Resolver());
            check.Expect(owned_received == 1,
                "購読者の GameObject が消えたら配送されない");
            check.Expect(bus.DroppedEventCount() > 0, "配送しなかった件数が記録される");
        }

        // 宛先が消えている場合
        {
            int target_received = 0;
            ScopedSubscription token = bus.Subscribe(event_b,
                [&target_received](const EventRecord&) { ++target_received; });

            Core::GameObject* target_object = world.CreateGameObject("EventTarget");
            EventRecord record;
            record.type = event_b;
            record.target = runtime.Resolver().MakeHandle(target_object);

            world.DestroyGameObject(target_object);
            world.ProcessPendingOperations();

            bus.Publish(record);
            bus.Dispatch(&runtime.Resolver());
            check.Expect(target_received == 0, "宛先が消えたイベントは配送されない");
        }

        // 配送順が発行順であること
        {
            std::vector<int> order;
            ScopedSubscription token = bus.Subscribe(event_a,
                [&order](const EventRecord& record)
                {
                    const Reflection::PropertyValue* value = record.payload.Find("index");
                    order.push_back(value != nullptr ? value->AsInt() : -1);
                });

            for (int index = 0; index < 5; ++index)
            {
                EventRecord record;
                record.type = event_a;
                record.payload.Set("index", Reflection::PropertyValue::MakeInt(index));
                bus.Publish(record);
            }
            bus.Dispatch(&runtime.Resolver());

            bool ordered = order.size() == 5;
            for (std::size_t index = 0; ordered && index < order.size(); ++index)
            {
                ordered = order[index] == static_cast<int>(index);
            }
            check.Expect(ordered, "同一フレームの配送順が発行順（決定的）");
        }

        // 無限再帰の打ち切り
        {
            ScopedSubscription token = bus.Subscribe(event_a,
                [&bus, event_a](const EventRecord&)
                {
                    EventRecord next;
                    next.type = event_a;
                    bus.Publish(next);
                });

            EventRecord record;
            record.type = event_a;
            bus.Publish(record);
            bus.Dispatch(&runtime.Resolver());

            check.Expect(bus.RecursionLimitHit(),
                "配送の中から発行し続けた場合に打ち切られる");
            check.Expect(bus.PendingEventCount() == 0,
                "打ち切り後に待ち行列が残らない");
        }

        // Global Bus は Scene とは別物
        check.Expect(&EventBus::Global() != &bus,
            "Global Bus と Scene Bus は別の実体");

        world.Services().SetRuntime(nullptr);
        return check.Report("Event validation");
    }
}
