#pragma once

#include "../../Object/Component/Component.h"
#include "../../Reflection/Property/PropertyDesc.h"

#include <DirectXMath.h>

#include <vector>

namespace ReplayEngine::Components
{
    // UIImage のクアッドを格子へ細分化し、Pin の移動量を重み付きで頂点へ伝える。
    // Motion Runtime は pin[n].position を普通の Vector2 Property として駆動する。
    class UIPuppetDeformComponent final : public Core::Component
    {
        REPLAY_COMPONENT_BODY(UIPuppetDeformComponent)

    public:
        UIPuppetDeformComponent() = default;

        const std::vector<Reflection::PropertyDesc>*
            DynamicProperties() const noexcept override;
        void OnPropertyChanged(const char* property_name) override;

        void SetPinCount(int count);
        int PinCount() const noexcept
        {
            return static_cast<int>(pin_positions.size());
        }

        DirectX::XMFLOAT2 DeformNormalizedPoint(
            const DirectX::XMFLOAT2& normalized) const noexcept;

        bool enabled_deform = true;
        int grid_columns = 6;
        int grid_rows = 6;
        float global_strength = 1.0f;
        std::vector<DirectX::XMFLOAT2> pin_bind_positions;
        std::vector<DirectX::XMFLOAT2> pin_positions;
        std::vector<float> pin_radii;

    private:
        void NormalizePinArrays();
        void RebuildDynamicProperties() const;
        mutable std::vector<Reflection::PropertyDesc> dynamic_properties_;
    };
}
