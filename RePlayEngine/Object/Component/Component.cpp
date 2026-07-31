#include "Component.h"

#include "../GameObject/GameObject.h"

namespace ReplayEngine::Core
{
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
}
