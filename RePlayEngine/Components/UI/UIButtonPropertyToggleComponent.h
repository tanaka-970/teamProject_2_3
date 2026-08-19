#pragma once

#include "../../Object/Component/Component.h"
#include "../../Reflection/Property/References.h"
#include "../../Runtime/Events/EventBus.h"

#include <string>

namespace ReplayEngine::Components
{
    // UIButton release を既存 EventBus から受け、対象 bool Property を反転する。
    // Demo だけの特別な callback を作らず、既存 Reflection / PropertyRegistry に乗せる。
    class UIButtonPropertyToggleComponent final : public Core::Component
    {
        REPLAY_COMPONENT_BODY(UIButtonPropertyToggleComponent)

    public:
        Reflection::ComponentReference target;
        std::string target_property = "enabled";

        void OnRuntimeAwake() override;
        void OnEnable() override;
        void OnDisable() override;
        void OnRuntimeDestroy() override;

    private:
        void EnsureSubscription();
        void ReleaseSubscription() noexcept;
        void HandleButtonStateChanged(const Runtime::EventRecord& record);

        Runtime::ScopedSubscription button_state_subscription_;
    };
}
