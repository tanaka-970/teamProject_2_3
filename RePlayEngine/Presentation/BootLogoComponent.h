#pragma once

#include "../Core/Components/IComponent.h"

#include <d3d11.h>
#include <memory>

class sprite;

namespace ReplayEngine::Presentation
{
    class BootLogoComponent final : public Core::IComponent
    {
    public:
        BootLogoComponent();
        ~BootLogoComponent() override;

        bool Initialize(ID3D11Device* device);
        void Reset() noexcept;
        void Update(float elapsed_time) override;
        void Render(ID3D11DeviceContext* context, float screen_width, float screen_height) const;

        void RequestSkip() noexcept { skip_requested_ = true; }
        bool IsActive() const noexcept { return initialized_ && time_ < kDuration; }
        bool IsFinished() const noexcept { return initialized_ && time_ >= kDuration; }

    private:
        void DrawBar(ID3D11DeviceContext* context, float center_x, float center_y,
            float width, float height, float angle_degrees,
            float r, float g, float b, float a) const;

    // 星ワイプはロゴ演出に含め、後続ゲームシーンには別の黒フェードを設けない。
        static constexpr float kDuration = 3.87f;

        std::unique_ptr<sprite> solid_;
        std::unique_ptr<sprite> star_;
        std::unique_ptr<sprite> note_;
        std::unique_ptr<sprite> mark_;
        std::unique_ptr<sprite> word_;
        float time_ = 0.0f;
        bool skip_requested_ = false;
        bool initialized_ = false;
    };
}
