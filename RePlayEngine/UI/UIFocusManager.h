#pragma once

namespace ReplayEngine::Scene { class Scene; class IInputService; }
namespace ReplayEngine::Components { class UISelectableComponent; }

namespace ReplayEngine::UI
{
    class UIFocusManager final
    {
    public:
        UIFocusManager() = delete;
        enum class Direction { Up, Down, Left, Right };

        static Components::UISelectableComponent* Current(Scene::Scene& scene) noexcept;
        static void SetFocus(Scene::Scene& scene,
            Components::UISelectableComponent* target) noexcept;
        static Components::UISelectableComponent* FindInDirection(Scene::Scene& scene,
            const Components::UISelectableComponent& from, Direction direction) noexcept;
        static void Update(Scene::Scene& scene, const Scene::IInputService& input);
    };
}
