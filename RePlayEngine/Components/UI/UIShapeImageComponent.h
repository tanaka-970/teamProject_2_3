#pragma once

#include "../../Object/Component/Component.h"
#include "../../Reflection/Property/PropertyDesc.h"

#include <DirectXMath.h>

#include <vector>

namespace ReplayEngine::Components
{
    // UIImageComponent の矩形描画とは別に、同じ GameObject の Image を
    // 自由な Bezier 輪郭でクリップするための専用コンポーネント。
    class UIShapeImageComponent final : public Core::Component
    {
        REPLAY_COMPONENT_BODY(UIShapeImageComponent)

    public:
        UIShapeImageComponent() = default;

        const std::vector<Reflection::PropertyDesc>*
            DynamicProperties() const noexcept override;
        void OnPropertyChanged(const char* property_name) override;
        void SetPathPointCount(int count);

        bool path_closed = true;
        std::vector<DirectX::XMFLOAT2> path_points;
        std::vector<DirectX::XMFLOAT2> path_in_handles;
        std::vector<DirectX::XMFLOAT2> path_out_handles;

    private:
        void NormalizePathArrays();
        void RebuildDynamicProperties() const;
        mutable std::vector<Reflection::PropertyDesc> dynamic_properties_;
    };
}
