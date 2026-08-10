#pragma once

#include <DirectXMath.h>

#include <string>

namespace ReplayEngine::UI
{
    enum class UIEffectKind : int
    {
        Blur = 0,
        Glow = 1,
        DropShadow = 2,
        ColorAdjust = 3,
        Noise = 4,
        Shake = 5,
        Mask = 6,
        Wipe = 7,
        Dissolve = 8,
        Distortion = 9,
        ChromaticAberration = 10,
    };

    class UIEffect final
    {
    public:
        bool enabled = true;
        int kind = static_cast<int>(UIEffectKind::Blur);
        float radius = 8.0f;
        float intensity = 1.0f;
        float threshold = 0.5f;
        float amount = 1.0f;
        float angle = 0.0f;
        float progress = 0.0f;
        float softness = 0.0f;
        float speed = 0.0f;
        float seed = 0.0f;
        DirectX::XMFLOAT2 direction{ 1.0f, -1.0f };
        DirectX::XMFLOAT4 color{ 1.0f, 1.0f, 1.0f, 1.0f };
        std::string mask;

        DirectX::XMFLOAT4 ExpandBounds() const noexcept;
    };
}
