#pragma once

#include "../../Object/Component/Component.h"
#include "../../Reflection/Property/References.h"

#include <DirectXMath.h>

#include <cmath>
#include <vector>

namespace ReplayEngine::Components
{
    class SkyboxComponent final : public Core::Component
    {
        REPLAY_COMPONENT_BODY(SkyboxComponent)

    public:
        SkyboxComponent() = default;

        Reflection::AssetReference cubemap;
        std::vector<Reflection::AssetReference> keyframes;
        int priority = 0;
        bool sky_enabled = true;
        float rotation_degrees = 0.0f;
        float intensity = 1.0f;
        float time = 0.0f;
        float time_speed = 0.0f;
        bool clouds_enabled = false;
        DirectX::XMFLOAT2 cloud_layer1_speed{ 0.003f, 0.0f };
        float cloud_layer1_scale = 3.5f;
        float cloud_layer1_density = 0.45f;
        DirectX::XMFLOAT4 cloud_layer1_color{ 1.0f, 1.0f, 1.0f, 0.75f };
        DirectX::XMFLOAT2 cloud_layer2_speed{ -0.0015f, 0.001f };
        float cloud_layer2_scale = 7.0f;
        float cloud_layer2_density = 0.28f;
        DirectX::XMFLOAT4 cloud_layer2_color{ 0.82f, 0.88f, 1.0f, 0.55f };
        bool stars_enabled = false;
        float star_density = 0.35f;
        float star_intensity = 2.0f;
        DirectX::XMFLOAT4 star_color{ 1.0f, 0.84f, 0.68f, 1.0f };
        bool moon_enabled = false;
        DirectX::XMFLOAT3 moon_direction{ 0.35f, 0.25f, 0.9f };
        float moon_size = 0.04f;
        float moon_intensity = 5.0f;
        DirectX::XMFLOAT4 moon_color{ 1.0f, 0.88f, 0.7f, 1.0f };

        void OnRuntimeAwake() override
        {
            cloud_time_ = 0.0f;
            previous_cloud_time_ = 0.0f;
            previous_rotation_degrees_ = std::isfinite(rotation_degrees)
                ? rotation_degrees : 0.0f;
            last_rotation_degrees_ = previous_rotation_degrees_;
        }

        void OnUpdate(float delta_time) override
        {
            previous_cloud_time_ = cloud_time_;
            previous_rotation_degrees_ = last_rotation_degrees_;
            last_rotation_degrees_ = std::isfinite(rotation_degrees)
                ? rotation_degrees : 0.0f;
            if (!std::isfinite(delta_time) || delta_time <= 0.0f) return;
            if (std::isfinite(time_speed) && time_speed != 0.0f)
            {
                time = std::fmod(time + delta_time * time_speed, 1.0f);
                if (time < 0.0f) time += 1.0f;
            }
            cloud_time_ += delta_time;
            if (!std::isfinite(cloud_time_)) cloud_time_ = 0.0f;
        }

        float CloudTime() const noexcept { return cloud_time_; }
        float PreviousCloudTime() const noexcept { return previous_cloud_time_; }
        float PreviousRotationDegrees() const noexcept { return previous_rotation_degrees_; }

    private:
        float cloud_time_ = 0.0f;
        float previous_cloud_time_ = 0.0f;
        float previous_rotation_degrees_ = 0.0f;
        float last_rotation_degrees_ = 0.0f;
    };
}
