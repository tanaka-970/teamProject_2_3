#pragma once

#include "IComponent.h"

#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

namespace ReplayEngine::Core
{
    class GameObject final
    {
    public:
        GameObject() = default;
        ~GameObject()
        {
            for (auto& component : components_)
            {
                component->OnDetach();
                component->owner_ = nullptr;
            }
        }

        GameObject(const GameObject&) = delete;
        GameObject& operator=(const GameObject&) = delete;

        template<class T, class... Args>
        T& AddComponent(Args&&... args)
        {
            static_assert(std::is_base_of_v<IComponent, T>, "T must derive from IComponent");
            auto component = std::make_unique<T>(std::forward<Args>(args)...);
            T& result = *component;
            component->owner_ = this;
            components_.push_back(std::move(component));
            result.OnAttach();
            return result;
        }

        template<class T>
        T* GetComponent() noexcept
        {
            static_assert(std::is_base_of_v<IComponent, T>, "T must derive from IComponent");
            for (auto& component : components_)
            {
                if (auto* result = dynamic_cast<T*>(component.get())) return result;
            }
            return nullptr;
        }

        template<class T>
        const T* GetComponent() const noexcept
        {
            static_assert(std::is_base_of_v<IComponent, T>, "T must derive from IComponent");
            for (const auto& component : components_)
            {
                if (auto* result = dynamic_cast<const T*>(component.get())) return result;
            }
            return nullptr;
        }

        void Update(float elapsed_time)
        {
            if (!active_) return;
            for (auto& component : components_)
            {
                if (component->Enabled()) component->Update(elapsed_time);
            }
        }

        bool Active() const noexcept { return active_; }
        void SetActive(bool active) noexcept { active_ = active; }

    private:
        std::vector<std::unique_ptr<IComponent>> components_;
        bool active_ = true;
    };
}
