#include "RuntimeContext.h"

#include "../Events/EventBus.h"
#include "../../Components/Motion/MotionPlayerComponent.h"
#include "../../Object/Component/Component.h"
#include "../../Object/GameObject/GameObject.h"
#include "../../Object/Registry/ComponentRegistry.h"
#include "../../Scene/Runtime/Scene.h"

namespace ReplayEngine::Runtime
{
    using Core::Component;
    using Core::GameObject;
    using Core::ObjectID;
    namespace
    {
        Components::MotionPlayerComponent* ResolveMotionPlayer(
            const HandleResolver& resolver, const ComponentHandle& handle,
            RuntimeStatus& status) noexcept
        {
            Component* component = nullptr;
            status = resolver.TryResolve(handle, component);
            if (component == nullptr) return nullptr;

            auto* player = dynamic_cast<Components::MotionPlayerComponent*>(component);
            if (player == nullptr)
            {
                status = RuntimeStatus::TypeMismatch;
                return nullptr;
            }
            status = RuntimeStatus::Ok;
            return player;
        }
    }
    // ---- Motion Player -----------------------------------------------------

    RuntimeStatus RuntimeContext::FindMotionPlayer(const ObjectHandle& owner,
        const std::string& key, ComponentHandle& out) const
    {
        out = ComponentHandle::None();
        RuntimeStatus status = RuntimeStatus::Ok;
        const GameObject* object = ResolveObject(owner, status);
        if (object == nullptr) return status;

        const std::size_t count = object->ComponentCount();
        for (std::size_t index = 0; index < count; ++index)
        {
            Component* component = object->ComponentAt(index);
            if (component == nullptr || component->PendingDestroy()) continue;
            auto* player = dynamic_cast<Components::MotionPlayerComponent*>(component);
            if (player == nullptr) continue;
            if (!key.empty() && player->key != key) continue;

            out = resolver_.MakeHandle(player);
            return out.IsEmpty() ? RuntimeStatus::ComponentNotFound : RuntimeStatus::Ok;
        }
        return RuntimeStatus::ComponentNotFound;
    }

    RuntimeStatus RuntimeContext::MotionPlay(const ComponentHandle& player)
    {
        RuntimeStatus status = RuntimeStatus::Ok;
        Components::MotionPlayerComponent* resolved =
            ResolveMotionPlayer(resolver_, player, status);
        if (resolved == nullptr) return status;
        resolved->Play();
        return RuntimeStatus::Ok;
    }

    RuntimeStatus RuntimeContext::MotionPlayFrom(const ComponentHandle& player,
        float seconds)
    {
        RuntimeStatus status = RuntimeStatus::Ok;
        Components::MotionPlayerComponent* resolved =
            ResolveMotionPlayer(resolver_, player, status);
        if (resolved == nullptr) return status;
        resolved->PlayFrom(seconds);
        return RuntimeStatus::Ok;
    }

    RuntimeStatus RuntimeContext::MotionPause(const ComponentHandle& player)
    {
        RuntimeStatus status = RuntimeStatus::Ok;
        Components::MotionPlayerComponent* resolved =
            ResolveMotionPlayer(resolver_, player, status);
        if (resolved == nullptr) return status;
        resolved->Pause();
        return RuntimeStatus::Ok;
    }

    RuntimeStatus RuntimeContext::MotionResume(const ComponentHandle& player)
    {
        RuntimeStatus status = RuntimeStatus::Ok;
        Components::MotionPlayerComponent* resolved =
            ResolveMotionPlayer(resolver_, player, status);
        if (resolved == nullptr) return status;
        resolved->Resume();
        return RuntimeStatus::Ok;
    }

    RuntimeStatus RuntimeContext::MotionStop(const ComponentHandle& player)
    {
        RuntimeStatus status = RuntimeStatus::Ok;
        Components::MotionPlayerComponent* resolved =
            ResolveMotionPlayer(resolver_, player, status);
        if (resolved == nullptr) return status;
        resolved->Stop();
        return RuntimeStatus::Ok;
    }

    RuntimeStatus RuntimeContext::MotionReverse(const ComponentHandle& player)
    {
        RuntimeStatus status = RuntimeStatus::Ok;
        Components::MotionPlayerComponent* resolved =
            ResolveMotionPlayer(resolver_, player, status);
        if (resolved == nullptr) return status;
        resolved->Reverse();
        return RuntimeStatus::Ok;
    }

    RuntimeStatus RuntimeContext::SetMotionTime(const ComponentHandle& player,
        float seconds)
    {
        RuntimeStatus status = RuntimeStatus::Ok;
        Components::MotionPlayerComponent* resolved =
            ResolveMotionPlayer(resolver_, player, status);
        if (resolved == nullptr) return status;
        resolved->SetTime(seconds);
        return RuntimeStatus::Ok;
    }

    RuntimeStatus RuntimeContext::SetMotionSpeed(const ComponentHandle& player,
        float speed)
    {
        RuntimeStatus status = RuntimeStatus::Ok;
        Components::MotionPlayerComponent* resolved =
            ResolveMotionPlayer(resolver_, player, status);
        if (resolved == nullptr) return status;
        resolved->SetSpeed(speed);
        return RuntimeStatus::Ok;
    }

    RuntimeStatus RuntimeContext::SetMotionWeight(const ComponentHandle& player,
        float weight)
    {
        RuntimeStatus status = RuntimeStatus::Ok;
        Components::MotionPlayerComponent* resolved =
            ResolveMotionPlayer(resolver_, player, status);
        if (resolved == nullptr) return status;
        resolved->SetWeight(weight);
        return RuntimeStatus::Ok;
    }

    RuntimeStatus RuntimeContext::IsMotionPlaying(const ComponentHandle& player,
        bool& out) const
    {
        RuntimeStatus status = RuntimeStatus::Ok;
        Components::MotionPlayerComponent* resolved =
            ResolveMotionPlayer(resolver_, player, status);
        if (resolved == nullptr) return status;
        out = resolved->IsPlaying();
        return RuntimeStatus::Ok;
    }

    RuntimeStatus RuntimeContext::GetMotionTime(const ComponentHandle& player,
        float& out) const
    {
        RuntimeStatus status = RuntimeStatus::Ok;
        Components::MotionPlayerComponent* resolved =
            ResolveMotionPlayer(resolver_, player, status);
        if (resolved == nullptr) return status;
        out = resolved->Time();
        return RuntimeStatus::Ok;
    }

    RuntimeStatus RuntimeContext::GetMotionDuration(const ComponentHandle& player,
        float& out) const
    {
        RuntimeStatus status = RuntimeStatus::Ok;
        Components::MotionPlayerComponent* resolved =
            ResolveMotionPlayer(resolver_, player, status);
        if (resolved == nullptr) return status;
        out = resolved->Duration();
        return RuntimeStatus::Ok;
    }
}
