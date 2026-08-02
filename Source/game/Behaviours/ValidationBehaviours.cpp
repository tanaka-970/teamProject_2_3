#include "ValidationBehaviours.h"

#include "../../../RePlayEngine/Object/GameObject/GameObject.h"
#include "../../../RePlayEngine/Object/Registry/ComponentRegistry.h"
#include "../../../RePlayEngine/Reflection/Registry/PropertyRegistry.h"
#include "../../../RePlayEngine/Runtime/API/RuntimeContext.h"
#include "../../../RePlayEngine/Runtime/Behaviour/BehaviourRegistry.h"

#include <cmath>

namespace Game::Behaviours
{
    using ReplayEngine::Core::ComponentRegistry;
    using ReplayEngine::Core::ComponentTypeInfo;
    using ReplayEngine::Reflection::MakeProperty;
    using ReplayEngine::Reflection::PropertyRegistry;

    // ---- RotatorBehaviour ---------------------------------------------------

    void RotatorBehaviour::OnAwake()
    {
        // Awake の時点で Property は反映済み。ここで初期状態を整えてよい。
        accumulated_degrees = 0.0f;
    }

    void RotatorBehaviour::OnUpdate(float delta_time)
    {
        ReplayEngine::Core::GameObject* owner = Owner();
        if (owner == nullptr) return;

        float step = delta_time;
        if (use_unscaled_time)
        {
            if (const Runtime::RuntimeContext* runtime = Runtime())
            {
                step = runtime->UnscaledDeltaTime();
            }
        }

        const float degrees = degrees_per_second * step;
        accumulated_degrees += degrees;

        const float radians = degrees * 0.01745329252f;

        DirectX::XMFLOAT3 rotation = owner->GetTransform().LocalRotationEuler();
        rotation.x += axis.x * radians;
        rotation.y += axis.y * radians;
        rotation.z += axis.z * radians;
        owner->GetTransform().SetLocalRotationEuler(rotation);
    }

    // ---- TriggerCounterBehaviour --------------------------------------------

    bool TriggerCounterBehaviour::Accepts(const Runtime::TriggerEvent& event) const noexcept
    {
        if (count_trigger_side_only && !event.self_is_trigger) return false;

        // -1 は「Layer を問わない」。相手の Layer が不明 (-1) の場合も通す。
        // 判別できないことを理由に取りこぼすより、数えて診断へ残す方がよい。
        if (accepted_layer >= 0 && event.other_layer >= 0 &&
            event.other_layer != accepted_layer)
        {
            return false;
        }
        return true;
    }

    void TriggerCounterBehaviour::OnTriggerEnter(const Runtime::TriggerEvent& event)
    {
        if (!Accepts(event)) return;
        ++enter_count;
        last_other.object = event.other.object;
    }

    void TriggerCounterBehaviour::OnTriggerStay(const Runtime::TriggerEvent& event)
    {
        if (!Accepts(event)) return;
        ++stay_count;
    }

    void TriggerCounterBehaviour::OnTriggerExit(const Runtime::TriggerEvent& event)
    {
        if (!Accepts(event)) return;
        ++exit_count;
    }
}

namespace Game
{
    void RegisterGameBehaviours()
    {
        using namespace Game::Behaviours;
        using ReplayEngine::Runtime::BehaviourRegistry;

        // ---- RotatorBehaviour ------------------------------------------------

        ComponentRegistry::Register<RotatorBehaviour>(
            ComponentTypeInfo::Describe("Rotator Behaviour", "Behaviours")
                .WithTooltip("GameObject を回し続ける。Behaviour 基盤の動作確認用。")
                .WithTypeGUID(RotatorBehaviour::StaticTypeGUID())
                .InModule("Game.Behaviours"));

        PropertyRegistry::Register<RotatorBehaviour>(
            MakeProperty("axis", &RotatorBehaviour::axis)
                .Display("回転軸").Step(0.01));
        PropertyRegistry::Register<RotatorBehaviour>(
            MakeProperty("degrees_per_second", &RotatorBehaviour::degrees_per_second)
                .Display("回転速度").Unit("度/秒").Range(-1440.0, 1440.0).Step(1.0));
        PropertyRegistry::Register<RotatorBehaviour>(
            MakeProperty("use_unscaled_time", &RotatorBehaviour::use_unscaled_time)
                .Display("タイムスケールを無視").Advanced());
        PropertyRegistry::Register<RotatorBehaviour>(
            MakeProperty("accumulated_degrees", &RotatorBehaviour::accumulated_degrees)
                .Display("累積回転量").Unit("度").ReadOnly().RuntimeOnly().NotSerializable());

        BehaviourRegistry::Register(RotatorBehaviour::StaticTypeGUID(),
            BehaviourRegistry::Native());

        // ---- TriggerCounterBehaviour ------------------------------------------

        ComponentRegistry::Register<TriggerCounterBehaviour>(
            ComponentTypeInfo::Describe("Trigger Counter Behaviour", "Behaviours")
                .WithTooltip("Trigger の接触回数を数える。Trigger 配送の動作確認用。")
                .WithTypeGUID(TriggerCounterBehaviour::StaticTypeGUID())
                .InModule("Game.Behaviours"));

        PropertyRegistry::Register<TriggerCounterBehaviour>(
            MakeProperty("accepted_layer", &TriggerCounterBehaviour::accepted_layer)
                .Display("対象 Layer").AsCollisionLayer());
        PropertyRegistry::Register<TriggerCounterBehaviour>(
            MakeProperty("count_trigger_side_only",
                &TriggerCounterBehaviour::count_trigger_side_only)
                .Display("Trigger 側だけ数える").Advanced());
        PropertyRegistry::Register<TriggerCounterBehaviour>(
            MakeProperty("enter_count", &TriggerCounterBehaviour::enter_count)
                .Display("Enter 回数").ReadOnly().RuntimeOnly().NotSerializable());
        PropertyRegistry::Register<TriggerCounterBehaviour>(
            MakeProperty("stay_count", &TriggerCounterBehaviour::stay_count)
                .Display("Stay 回数").ReadOnly().RuntimeOnly().NotSerializable());
        PropertyRegistry::Register<TriggerCounterBehaviour>(
            MakeProperty("exit_count", &TriggerCounterBehaviour::exit_count)
                .Display("Exit 回数").ReadOnly().RuntimeOnly().NotSerializable());
        PropertyRegistry::Register<TriggerCounterBehaviour>(
            MakeProperty("last_other", &TriggerCounterBehaviour::last_other)
                .Display("最後の接触相手").ReadOnly().RuntimeOnly().NotSerializable());

        BehaviourRegistry::Register(TriggerCounterBehaviour::StaticTypeGUID(),
            BehaviourRegistry::Native());
    }
}
