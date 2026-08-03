#include "Component.h"

#include "../GameObject/GameObject.h"
#include "../../Reflection/Property/PropertyBag.h"

namespace ReplayEngine::Core
{
    // ここで定義する。unknown_properties_ が指す PropertyBag は
    // Component.h では不完全型なので、完全型が見えるこの位置で実体化させる。
    Component::Component() = default;
    Component::~Component() = default;

    void Component::RetainUnknownProperties(const Reflection::PropertyBag& properties)
    {
        if (properties.Empty())
        {
            unknown_properties_.reset();
            return;
        }

        if (!unknown_properties_)
        {
            unknown_properties_ = std::make_unique<Reflection::PropertyBag>();
        }
        *unknown_properties_ = properties;
    }

    void Component::ClearUnknownProperties() noexcept
    {
        unknown_properties_.reset();
    }

    Scene::Scene* Component::GetScene() const noexcept
    {
        return owner_ != nullptr ? owner_->GetScene() : nullptr;
    }

    bool Component::ActiveInHierarchy() const noexcept
    {
        if (!enabled_ || pending_destroy_) return false;
        return owner_ != nullptr && owner_->ActiveInHierarchy();
    }

    void Component::SetEnabled(bool enabled) noexcept
    {
        // ここではフラグを立てるだけにする。
        // OnEnable / OnDisable は Scene の同期点で対称に呼ばれるため、
        // Inspector の描画中にこれを呼んでも、その場で Component の内部状態が
        // 作り替えられて無効ポインタが生まれることはない。
        enabled_ = enabled;
    }

    void Component::Destroy() noexcept
    {
        // 即座に破棄しない。OnUpdate の中から自分自身を消しても安全にするため、
        // 予約だけ立てて実際の破棄は Scene の同期点へ委ねる。
        pending_destroy_ = true;
    }

    void Component::AttachTo(GameObject* owner)
    {
        owner_ = owner;
        if (!attached_)
        {
            attached_ = true;
            OnAttach();
        }
    }

    void Component::SyncEnableState()
    {
        // Awake は「有効かどうか」に関係なく、Scene が動き出した最初の同期点で一度だけ。
        //
        // ここへ置く理由:
        //   Scene::Start() -> SynchronizeStates() は全 Component を必ず 1 回通る。
        //   有効・無効で分岐する前に呼べば、無効な Component にも確実に届く。
        //   ApplySceneData はこの経路を通らないので、
        //   「読み込んだだけ」では Awake が走らない（Editor で置いただけで動き出さない）。
        if (!runtime_awake_called_)
        {
            runtime_awake_called_ = true;
            OnRuntimeAwake();
        }

        const bool desired = ActiveInHierarchy();

        // OnEnable と OnDisable が必ず対になるよう、適用済みの状態と比べてから呼ぶ。
        if (desired != enable_state_applied_)
        {
            enable_state_applied_ = desired;
            if (desired) OnEnable();
            else OnDisable();
        }

        // OnStart は「初めて実際に有効になった」ときだけ一度呼ぶ。
        // 無効化して再度有効化しても二度目は呼ばれない。
        if (desired && !started_)
        {
            started_ = true;
            OnStart();
        }
    }

    void Component::ForceDisable()
    {
        if (enable_state_applied_)
        {
            enable_state_applied_ = false;
            OnDisable();
        }
    }

    void Component::RaiseRuntimeDestroy()
    {
        // 二度呼ばれても一度しか通さない。
        // CompactComponents と DetachAllComponents の両方から到達しうるため。
        if (runtime_destroy_called_) return;
        runtime_destroy_called_ = true;
        OnRuntimeDestroy();
    }
}
