#include "LandscapeEditorTool.h"

#include "LandscapeData.h"

#include <algorithm>
#include <cmath>

namespace ReplayEngine::Landscape
{
    bool LandscapeEditorTool::BeginStroke(LandscapeData& data,
        LandscapeBrushMode mode, const LandscapeBrush& brush)
    {
        if (StrokeActive() || !data.Valid() || brush.radius <= 0.0f || brush.strength < 0.0f)
            return false;
        data_ = &data;
        mode_ = mode;
        brush_ = brush;
        command_ = std::make_unique<LandscapeUndoCommand>();
        return true;
    }

    bool LandscapeEditorTool::ApplySample(float world_x, float world_z, float delta_time)
    {
        if (data_ == nullptr || command_ == nullptr || delta_time <= 0.0f) return false;
        const float cell = data_->CellSize();
        const float center_x = world_x / cell;
        const float center_z = world_z / cell;
        const float grid_radius = brush_.radius / cell;
        const int minimum_x = (std::max)(0, static_cast<int>(std::floor(center_x - grid_radius)));
        const int maximum_x = (std::min)(data_->Width() - 1,
            static_cast<int>(std::ceil(center_x + grid_radius)));
        const int minimum_z = (std::max)(0, static_cast<int>(std::floor(center_z - grid_radius)));
        const int maximum_z = (std::min)(data_->Height() - 1,
            static_cast<int>(std::ceil(center_z + grid_radius)));
        bool changed = false;

        for (int z = minimum_z; z <= maximum_z; ++z)
        {
            for (int x = minimum_x; x <= maximum_x; ++x)
            {
                const float dx = (x - center_x) * cell;
                const float dz = (z - center_z) * cell;
                const float distance = std::sqrt(dx * dx + dz * dz);
                if (distance > brush_.radius) continue;
                const float normalized = 1.0f - distance / brush_.radius;
                const float exponent = 1.0f + (std::max)(0.0f, brush_.falloff) * 4.0f;
                const float weight = std::pow(normalized, exponent);
                const std::size_t index = data_->Index(x, z);
                const float before = data_->HeightByIndex(index);
                float after = before;
                const float amount = brush_.strength * delta_time * weight;

                switch (mode_)
                {
                case LandscapeBrushMode::Raise: after = before + amount; break;
                case LandscapeBrushMode::Lower: after = before - amount; break;
                case LandscapeBrushMode::Flatten:
                    after = before + (brush_.flatten_height - before) * (std::min)(1.0f, amount);
                    break;
                case LandscapeBrushMode::Smooth:
                {
                    float sum = 0.0f;
                    int count = 0;
                    for (int nz = (std::max)(0, z - 1); nz <= (std::min)(data_->Height() - 1, z + 1); ++nz)
                        for (int nx = (std::max)(0, x - 1); nx <= (std::min)(data_->Width() - 1, x + 1); ++nx)
                        { sum += data_->HeightAt(nx, nz); ++count; }
                    const float average = count > 0 ? sum / count : before;
                    after = before + (average - before) * (std::min)(1.0f, amount);
                    break;
                }
                }

                if (data_->SetHeightByIndex(index, after))
                {
                    command_->Record(index, before, after);
                    changed = true;
                }
            }
        }
        return changed;
    }

    std::unique_ptr<LandscapeUndoCommand> LandscapeEditorTool::EndStroke()
    {
        data_ = nullptr;
        if (command_ != nullptr && command_->Empty()) command_.reset();
        return std::move(command_);
    }

    void LandscapeEditorTool::CancelStroke()
    {
        if (data_ != nullptr && command_ != nullptr) command_->Undo(*data_);
        data_ = nullptr;
        command_.reset();
    }
}
