#pragma once

#include "../../Object/Component/Component.h"

#include <DirectXMath.h>

#include <string>

namespace ReplayEngine::Components
{
    class NormalAdjustComponent final : public Core::Component
    {
        REPLAY_COMPONENT_BODY(NormalAdjustComponent)

    public:
        enum TargetSlotMode : int
        {
            WholeModel = 0,
            MaterialSlot = 1,
        };

        float blend = 0.0f;
        DirectX::XMFLOAT3 center{ 0.0f, 0.0f, 0.0f };
        float radius = 0.5f;
        float falloff = 0.2f;
        std::string bone;
        int target_slot_mode = WholeModel;
        int target_slot_index = 0;
        DirectX::XMFLOAT3 resolved_center_world{};
        float resolved_radius_world = 0.0f;
        DirectX::XMFLOAT4X4 resolved_center_matrix{};
        bool resolved_center_valid = false;
    };
}
