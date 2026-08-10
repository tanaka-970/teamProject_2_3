#pragma once

#include <DirectXMath.h>

#include <string>

namespace ReplayEngine::UI
{
    enum class UIEffectKind : int
    {
        Blur = 0,
        Glow = 1,
        ColorAdjust = 2,
        Noise = 3,
        Shake = 4,
        Mask = 5,
        Wipe = 6,
        Dissolve = 7,
        Distortion = 8,
        ChromaticAberration = 9,
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
        std::string custom_shader;

        DirectX::XMFLOAT4 ExpandBounds() const noexcept;
    };
}
