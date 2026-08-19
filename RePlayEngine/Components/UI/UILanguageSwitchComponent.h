#pragma once

#include "../../Object/Component/Component.h"
#include "../../Runtime/Events/EventBus.h"

#include <string>

namespace ReplayEngine::Components
{
    // UIButton の release を既存 EventBus から受け、LocalizationService の言語だけを切り替える。
    // Button 固有の callback 機構は増やさない。
    class UILanguageSwitchComponent final : public Core::Component
    {
        REPLAY_COMPONENT_BODY(UILanguageSwitchComponent)

    public:
        std::string language = "ja";

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
