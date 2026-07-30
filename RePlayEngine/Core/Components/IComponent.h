#pragma once

namespace ReplayEngine::Core
{
    class GameObject;

    class IComponent
    {
    public:
        virtual ~IComponent() = default;

        IComponent(const IComponent&) = delete;
        IComponent& operator=(const IComponent&) = delete;

        virtual void OnAttach() {}
        virtual void OnDetach() {}
        virtual void Update(float) {}

        GameObject* Owner() noexcept { return owner_; }
        const GameObject* Owner() const noexcept { return owner_; }
        bool Enabled() const noexcept { return enabled_; }
        void SetEnabled(bool enabled) noexcept { enabled_ = enabled; }

    protected:
        IComponent() = default;

    private:
        friend class GameObject;
        GameObject* owner_ = nullptr;
        bool enabled_ = true;
    };
}
