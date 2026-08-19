#include "HealthComponent.h"

#include <algorithm>

namespace ReplayEngine::Components
{
    namespace
    {
        constexpr int minimum_max_health = 1;

        void Clamp(int& value, int low, int high) noexcept
        {
            value = (std::max)(low, (std::min)(value, high));
        }
    }

    void HealthComponent::OnStart()
    {
        // Scene 読み込みで復元された値がそのまま使えるよう、ここでは満タンに戻さない。
        // 範囲だけ整えておく。
        OnPropertyChanged(nullptr);
    }

    void HealthComponent::SetMaxHealth(int value) noexcept
    {
        max_health = (std::max)(minimum_max_health, value);
        Clamp(current_health, 0, max_health);
    }

    void HealthComponent::ApplyDamage(int amount) noexcept
    {
        if (invulnerable || amount <= 0) return;
        current_health = (std::max)(0, current_health - amount);
    }

    void HealthComponent::Heal(int amount) noexcept
    {
        if (amount <= 0) return;
        current_health = (std::min)(max_health, current_health + amount);
    }

    void HealthComponent::OnPropertyChanged(const char*)
    {
        // Inspector で max_health を下げた場合や、
        // 古い Scene ファイルに範囲外の値が入っていた場合の整合性を取る。
        max_health = (std::max)(minimum_max_health, max_health);
        Clamp(current_health, 0, max_health);
    }
}
