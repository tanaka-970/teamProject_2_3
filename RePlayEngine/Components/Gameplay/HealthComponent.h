#pragma once

#include "../../Object/Component/Component.h"

namespace ReplayEngine::Components
{
    // 体力を保持するだけの Component。
    //
    // 将来プレイヤーと敵の双方から共有する想定で、
    // 「誰の体力か」「どう減るか」には関与しない。減らす判断は外側の Component が行う。
    //
    // 現時点では Scene 保存と Inspector 編集の確認用としても使う。
    class HealthComponent final : public Core::Component
    {
        REPLAY_COMPONENT_BODY(HealthComponent)

    public:
        HealthComponent() = default;

        void OnStart() override;

        int MaxHealth() const noexcept { return max_health; }
        int CurrentHealth() const noexcept { return current_health; }
        bool Dead() const noexcept { return current_health <= 0; }

        void SetMaxHealth(int value) noexcept;

        // 負のダメージは無視する。回復は Heal を使うこと。
        void ApplyDamage(int amount) noexcept;
        void Heal(int amount) noexcept;
        void ResetToFull() noexcept { current_health = max_health; }

        // Inspector から編集された直後に整合性を取り直す。
        void OnPropertyChanged(const char* property_name) override;

        int max_health = 100;
        int current_health = 100;

        // true の間はダメージを受け付けない。
        bool invulnerable = false;
    };
}
