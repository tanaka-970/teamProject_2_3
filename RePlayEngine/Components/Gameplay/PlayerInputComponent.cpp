#include "PlayerInputComponent.h"

#include "../../Object/GameObject/GameObject.h"
#include "../../Scene/Runtime/Scene.h"
#include "../../Scene/Services/IInputService.h"

namespace ReplayEngine::Components
{
    void PlayerInputComponent::OnUpdate(float)
    {
        const Scene::Scene* scene = GetScene();
        const bool playing = scene != nullptr && scene->Services().Playing();
        const Scene::IInputService* input = scene != nullptr
            ? scene->Services().Input() : nullptr;

        if (!input_enabled || !playing || input == nullptr)
        {
            move_x_ = 0.0f;
            move_y_ = 0.0f;
            dash_held_ = false;
            // 停止中に押していた入力を再開時へ持ち越さない。
            jump_latched_ = false;
            return;
        }

        // OS は framework のフレーム先頭で 1 回だけ採取済み。
        // 同じ Action を別 Component が同時に読んでも Pressed は消費されない。
        move_x_ = input->Axis("MoveX", local_player_slot);
        move_y_ = input->Axis("MoveY", local_player_slot);
        dash_held_ = input->Held("Dash", local_player_slot);
        if (input->Pressed("Jump", local_player_slot)) jump_latched_ = true;
    }

    void PlayerInputComponent::OnDisable()
    {
        move_x_ = 0.0f;
        move_y_ = 0.0f;
        dash_held_ = false;
        jump_latched_ = false;
    }

    bool PlayerInputComponent::ConsumeJump() noexcept
    {
        const bool latched = jump_latched_;
        jump_latched_ = false;
        return latched;
    }
}
