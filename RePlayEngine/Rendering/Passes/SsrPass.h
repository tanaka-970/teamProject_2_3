#pragma once

namespace ReplayEngine::Rendering
{
    class SsrPass final
    {
    public:
        bool Initialized() const noexcept { return true; }
        void InvalidateHistory() noexcept { history_valid_ = false; }
        bool HistoryValid() const noexcept { return history_valid_; }
        void MarkHistoryValid() noexcept { history_valid_ = true; }
        float max_distance = 40.0f;
        float thickness = 0.55f;
        float stride = 3.0f;
        int max_step = 32;
        int refine_step = 4;
        float max_roughness = 0.6f;
        float intensity = 1.0f;
        float edge_fade = 0.08f;
        float ray_bias = 0.001f;
        float resolve_radius = 0.0f;
        int resolve_tap_count = 1;
        bool enabled = true;
    private:
        bool history_valid_ = false;
    };
}
