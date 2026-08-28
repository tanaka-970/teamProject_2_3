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
        float thickness = 0.4f;
        float stride = 3.0f;
        int max_step = 48;
        int refine_step = 5;
        float max_roughness = 0.65f;
        float intensity = 1.0f;
        float edge_fade = 0.12f;
        float ray_bias = 1.0f;
        float resolve_radius = 12.0f;
        int resolve_tap_count = 8;
        bool enabled = true;
    private:
        bool history_valid_ = false;
    };
}
