#pragma once

#include "../../Object/Component/Component.h"
#include "../../Reflection/Property/PropertyDesc.h"

#include <string>
#include <vector>

namespace ReplayEngine::Components
{
    // 名前付きの状態だけを持つ汎用 State Machine。
    // AnimatorComponent はスケルタルアニメーション専用なので流用しない。
    class StateComponent final : public Core::Component
    {
        REPLAY_COMPONENT_BODY(StateComponent)

    public:
        struct StateEntry final
        {
            std::string name;
        };

        StateComponent();
        void OnRuntimeAwake() override;

        int StateCount() const noexcept
        {
            return static_cast<int>(states.size());
        }
        void SetStateCount(int count);
        bool SetCurrentState(const std::string& name);
        const std::string& CurrentState() const noexcept { return current_state; }

        const std::vector<Reflection::PropertyDesc>*
            DynamicProperties() const noexcept override;

        int state_count = 2;
        std::string current_state = "State0";
        std::vector<StateEntry> states;

    private:
        void RebuildDynamicProperties() const;
        bool HasState(const std::string& name) const noexcept;
        void PublishStateChanged(const std::string& previous_state);

        mutable std::vector<Reflection::PropertyDesc> dynamic_properties_;
    };
}
