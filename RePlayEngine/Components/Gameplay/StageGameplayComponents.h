#pragma once

#include "../../Object/Component/Component.h"

#include <DirectXMath.h>

#include <string>
#include <vector>

namespace ReplayEngine::Components
{
    class SpawnPointComponent final : public Core::Component
    {
        REPLAY_COMPONENT_BODY(SpawnPointComponent)

    public:
        void OnStart() override;
        void OnEnable() override;

        int spawn_id = 0;
        int team = 0;
        int priority = 0;
        bool debug_draw = true;

    private:
        void RegisterPoint();
    };

    class CheckpointComponent final : public Core::Component
    {
        REPLAY_COMPONENT_BODY(CheckpointComponent)

    public:
        void OnTriggerEnter(const Core::TriggerContact& contact) override;

        int checkpoint_id = 0;
        DirectX::XMFLOAT3 respawn_position_offset{ 0.0f, 0.0f, 0.0f };
        DirectX::XMFLOAT3 respawn_rotation{ 0.0f, 0.0f, 0.0f };
        int target_mask = -1;
        bool one_shot = false;

    private:
        bool activated_ = false;
    };

    class GoalComponent final : public Core::Component
    {
        REPLAY_COMPONENT_BODY(GoalComponent)

    public:
        void OnTriggerEnter(const Core::TriggerContact& contact) override;

        int goal_id = 0;
        int target_mask = -1;
        bool one_shot = true;
        std::string completion_event{ "GoalReached" };

    private:
        bool activated_ = false;
    };

    class KillVolumeComponent final : public Core::Component
    {
        REPLAY_COMPONENT_BODY(KillVolumeComponent)

    public:
        void OnTriggerEnter(const Core::TriggerContact& contact) override;

        int target_mask = -1;
        bool respawn_at_checkpoint = true;
        int damage_amount = 100000;
    };

    class JumpPadComponent final : public Core::Component
    {
        REPLAY_COMPONENT_BODY(JumpPadComponent)

    public:
        void OnUpdate(float delta_time) override;
        void OnTriggerEnter(const Core::TriggerContact& contact) override;
        void OnTriggerStay(const Core::TriggerContact& contact) override;

        DirectX::XMFLOAT3 direction{ 0.0f, 1.0f, 0.0f };
        float force = 12.0f;
        int target_mask = -1;
        bool one_shot = false;
        float cooldown = 0.5f;
        bool debug_draw = true;

    private:
        struct CooldownEntry
        {
            Core::ObjectID object;
            float remaining = 0.0f;
        };

        void TryLaunch(const Core::TriggerContact& contact);
        std::vector<CooldownEntry> cooldowns_;
        bool activated_ = false;
    };

    class DamageAreaComponent final : public Core::Component
    {
        REPLAY_COMPONENT_BODY(DamageAreaComponent)

    public:
        void OnUpdate(float delta_time) override;
        void OnTriggerEnter(const Core::TriggerContact& contact) override;
        void OnTriggerStay(const Core::TriggerContact& contact) override;
        void OnTriggerExit(const Core::TriggerContact& contact) override;

        int damage = 10;
        float interval = 1.0f;
        int target_mask = -1;
        bool one_shot = false;

    private:
        struct ContactState
        {
            Core::ObjectID object;
            float remaining = 0.0f;
            bool applied = false;
        };

        void TryApply(const Core::TriggerContact& contact, bool entering);
        std::vector<ContactState> contacts_;
    };
}
