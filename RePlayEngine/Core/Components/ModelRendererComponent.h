#pragma once

#include "IComponent.h"

#include <DirectXMath.h>

#include <string>

namespace ReplayEngine::Core
{
    class ModelRendererComponent final : public IComponent
    {
    public:
        const std::string& AssetGuid() const noexcept { return asset_guid_; }
        void SetAssetGuid(std::string guid) { asset_guid_ = std::move(guid); }

        DirectX::XMFLOAT4 tint{ 1.0f, 1.0f, 1.0f, 1.0f };
        int shading_model = 1;
        bool outline = false;
        bool visible = true;

    private:
        std::string asset_guid_;
    };
}
