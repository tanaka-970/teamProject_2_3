#include "LandscapeEditorTool.h"
#include "LandscapeData.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>
#include <vector>

namespace ReplayEngine::Landscape
{
    namespace
    {
        float Noise(const DirectX::XMFLOAT3& p, float scale) noexcept
        {
            // deterministic hash-like noise。Asset 保存不要で、同じ位置は同じ値になる。
            const float value = std::sin((p.x * 12.9898f + p.y * 37.719f +
                p.z * 78.233f) * (std::max)(0.001f, scale)) * 43758.5453f;
            const float fraction = value - std::floor(value);
            return fraction * 2.0f - 1.0f;
        }
    }

    bool LandscapeEditorTool::BeginStroke(LandscapeData& data,
        LandscapeBrushMode mode, const LandscapeBrush& brush)
    {
        if (StrokeActive() || !data.Valid() || brush.radius <= 0.0f ||
            brush.strength < 0.0f || !std::isfinite(brush.radius) ||
            !std::isfinite(brush.strength)) return false;
        data_ = &data;
        mode_ = mode;
        brush_ = brush;
        command_ = std::make_unique<LandscapeUndoCommand>();
        return true;
    }

    bool LandscapeEditorTool::ApplySample(const DirectX::XMFLOAT3& center,
        float delta_time)
    {
        if (data_ == nullptr || command_ == nullptr || delta_time <= 0.0f) return false;
        const auto& vertices = data_->Vertices();
        if (vertices.empty()) return false;

        // Smooth は「この sample の変更前」の隣接平均を使う。
        std::vector<std::vector<std::uint32_t>> adjacency;
        if (mode_ == LandscapeBrushMode::Smooth)
        {
            adjacency.resize(vertices.size());
            const auto& indices = data_->Indices();
            for (std::size_t i = 0; i + 2 < indices.size(); i += 3)
            {
                const std::uint32_t a = indices[i], b = indices[i + 1], c = indices[i + 2];
                const std::uint32_t tri[3]{ a, b, c };
                for (int e = 0; e < 3; ++e)
                {
                    const std::uint32_t from = tri[e];
                    const std::uint32_t to = tri[(e + 1) % 3];
                    if (from < adjacency.size() && to < adjacency.size())
                    {
                        adjacency[from].push_back(to);
                        adjacency[to].push_back(from);
                    }
                }
            }
        }

        struct Change { std::size_t index; DirectX::XMFLOAT3 before; DirectX::XMFLOAT3 after; };
        std::vector<Change> changes;
        changes.reserve(vertices.size() / 8 + 1);

        for (std::size_t index = 0; index < vertices.size(); ++index)
        {
            const LandscapeVertex& vertex = vertices[index];
            const float dx = vertex.position.x - center.x;
            const float dy = vertex.position.y - center.y;
            const float dz = vertex.position.z - center.z;
            // LocalY は Landscape ローカル上面の従来操作感を維持し XZ 円で拾う。
            // VertexNormal は洞窟壁を想定し 3D sphere で拾う。
            const float distance = brush_.direction == LandscapeSculptDirection::LocalY
                ? std::sqrt(dx * dx + dz * dz)
                : std::sqrt(dx * dx + dy * dy + dz * dz);
            if (distance > brush_.radius) continue;

            const float normalized = 1.0f - distance / brush_.radius;
            const float exponent = 1.0f + (std::max)(0.0f, brush_.falloff) * 4.0f;
            const float weight = std::pow((std::max)(0.0f, normalized), exponent);
            const float amount = brush_.strength * delta_time * weight;
            if (amount <= 0.0f) continue;

            const DirectX::XMFLOAT3 before = vertex.position;
            DirectX::XMFLOAT3 after = before;
            DirectX::XMFLOAT3 direction = brush_.direction == LandscapeSculptDirection::LocalY
                ? DirectX::XMFLOAT3{ 0.0f, 1.0f, 0.0f }
                : vertex.normal;

            switch (mode_)
            {
            case LandscapeBrushMode::Raise:
                after.x += direction.x * amount;
                after.y += direction.y * amount;
                after.z += direction.z * amount;
                break;
            case LandscapeBrushMode::Lower:
                after.x -= direction.x * amount;
                after.y -= direction.y * amount;
                after.z -= direction.z * amount;
                break;
            case LandscapeBrushMode::Flatten:
                if (brush_.direction == LandscapeSculptDirection::LocalY)
                {
                    const float t = (std::min)(1.0f, amount);
                    after.y = before.y + (brush_.flatten_height - before.y) * t;
                }
                else
                {
                    // 任意方向 flatten は brush center を通る接平面へ寄せる。
                    const float signed_distance = dx * direction.x + dy * direction.y + dz * direction.z;
                    const float t = (std::min)(1.0f, amount);
                    after.x -= direction.x * signed_distance * t;
                    after.y -= direction.y * signed_distance * t;
                    after.z -= direction.z * signed_distance * t;
                }
                break;
            case LandscapeBrushMode::Smooth:
            {
                if (index >= adjacency.size() || adjacency[index].empty()) break;
                DirectX::XMFLOAT3 average = before;
                int count = 1;
                for (std::uint32_t neighbor : adjacency[index])
                {
                    if (neighbor >= vertices.size()) continue;
                    average.x += vertices[neighbor].position.x;
                    average.y += vertices[neighbor].position.y;
                    average.z += vertices[neighbor].position.z;
                    ++count;
                }
                const float inverse = 1.0f / static_cast<float>(count);
                average.x *= inverse; average.y *= inverse; average.z *= inverse;
                const float t = (std::min)(1.0f, amount);
                after.x += (average.x - before.x) * t;
                after.y += (average.y - before.y) * t;
                after.z += (average.z - before.z) * t;
                break;
            }
            case LandscapeBrushMode::Noise:
            {
                const float signed_amount = Noise(before, brush_.noise_scale) * amount;
                after.x += direction.x * signed_amount;
                after.y += direction.y * signed_amount;
                after.z += direction.z * signed_amount;
                break;
            }
            }

            if (std::fabs(after.x - before.x) > 1.0e-6f ||
                std::fabs(after.y - before.y) > 1.0e-6f ||
                std::fabs(after.z - before.z) > 1.0e-6f)
                changes.push_back({ index, before, after });
        }

        if (changes.empty()) return false;
        for (const Change& change : changes)
        {
            data_->SetVertexPosition(change.index, change.after, false);
            command_->RecordPosition(change.index, change.before, change.after);
        }
        data_->FinalizeGeometryEdit();
        return true;
    }

    std::unique_ptr<LandscapeUndoCommand> LandscapeEditorTool::EndStroke()
    {
        data_ = nullptr;
        if (command_ != nullptr && command_->Empty()) command_.reset();
        if (command_ != nullptr) command_->Seal();
        return std::move(command_);
    }

    void LandscapeEditorTool::CancelStroke()
    {
        if (data_ != nullptr && command_ != nullptr) command_->Undo(*data_);
        data_ = nullptr;
        command_.reset();
    }
}
