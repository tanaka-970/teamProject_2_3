#pragma once

#include "../Core/Components/IComponent.h"
#include "../Rendering/DX12/D3D12DeviceContext.h"

namespace ReplayEngine::Presentation
{
    class BootLogoComponent final : public Core::IComponent
    {
    public:
        BootLogoComponent();
        ~BootLogoComponent() override;

        bool Initialize();
        void Reset() noexcept;
        void Update(float elapsed_time) override;
        bool BuildRuntimeUI(Rendering::DX12::D3D12UIFrame& frame,
            float screen_width, float screen_height) const;

        void RequestSkip() noexcept { skip_requested_ = true; }
        bool IsActive() const noexcept { return initialized_ && time_ < kDuration; }
        bool IsFinished() const noexcept { return initialized_ && time_ >= kDuration; }

    private:
        static constexpr float kDuration = 3.87f;
        float time_ = 0.0f;
        bool skip_requested_ = false;
        bool initialized_ = false;
    };
}
