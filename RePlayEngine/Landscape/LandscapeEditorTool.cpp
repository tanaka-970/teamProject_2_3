#include "LandscapeEditorTool.h"
#include "LandscapeData.h"

#include <DirectXCollision.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>
#include <utility>
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
        if (StrokeActive() ||
            !data.Valid() || brush.radius <= 0.0f ||
            brush.strength < 0.0f || !std::isfinite(brush.radius) ||
            !std::isfinite(brush.strength)) return false;
        previous_hit_valid_ = false;
        data_ = &data;
        mode_ = mode;
        brush_ = brush;
        command_ = std::make_unique<LandscapeUndoCommand>();
        if (mode == LandscapeBrushMode::Subdivide) command_->BeginTopology(data);
        return true;
    }

    bool LandscapeEditorTool::ApplySample(const DirectX::XMFLOAT3& center, float delta_time)
    {
        if (!data_) return false;
        // Compatibility callers supply only a point. Resolve the nearest surface using bounded rays.
        LandscapeRayHit best{};
        float nearest = brush_.radius;
        for (const DirectX::XMFLOAT3 direction : { DirectX::XMFLOAT3{0,1,0}, {0,-1,0},
            {1,0,0}, {-1,0,0}, {0,0,1}, {0,0,-1} })
        {
            LandscapeRayHit hit;
            if (data_->Raycast(center, direction, nearest, hit)) { best = hit; nearest = hit.distance; }
        }
        if (!best.hit) return false;
        best.position = center;
        return ApplySurfaceSample(best, delta_time);
    }

    bool LandscapeEditorTool::ApplyStrokeSample(const LandscapeRayHit& hit, float delta_time)
    {
        if (!data_ || !hit.hit || !std::isfinite(delta_time) || delta_time <= 0) return false;
        if (mode_ == LandscapeBrushMode::Subdivide)
            return ApplySubdivideSample(*data_, hit.position, hit.face_index, brush_);
        bool changed = false;
        const auto previous = previous_hit_;
        if (!previous_hit_valid_) changed = ApplySurfaceSample(hit, delta_time);
        else
        {
            const auto delta = DirectX::XMVectorSubtract(DirectX::XMLoadFloat3(&hit.position),
                DirectX::XMLoadFloat3(&previous.position));
            const float distance = DirectX::XMVectorGetX(DirectX::XMVector3Length(delta));
            const float spacing = (std::max)(0.001f, brush_.radius * 0.15f);
            const int steps = (std::max)(1, static_cast<int>(std::ceil(distance / spacing)));
            data_->QuerySurface(previous.position, distance + brush_.radius,
                previous.face_index, interpolation_region_);
            const bool connected = hit.face_index < interpolation_region_.face_marks.size() &&
                interpolation_region_.face_marks[hit.face_index] == interpolation_region_.generation &&
                std::find(interpolation_region_.faces.begin(), interpolation_region_.faces.end(),
                    hit.face_index) != interpolation_region_.faces.end();
            if (!connected) changed = ApplySurfaceSample(hit, delta_time);
            else for (int sample = 1; sample <= steps; ++sample)
            {
                const float t = static_cast<float>(sample) / steps;
                DirectX::XMFLOAT3 point{}, normal{};
                DirectX::XMStoreFloat3(&point, DirectX::XMVectorLerp(
                    DirectX::XMLoadFloat3(&previous.position), DirectX::XMLoadFloat3(&hit.position), t));
                DirectX::XMStoreFloat3(&normal, DirectX::XMVector3Normalize(DirectX::XMVectorLerp(
                    DirectX::XMLoadFloat3(&previous.normal), DirectX::XMLoadFloat3(&hit.normal), t)));
                LandscapeRayHit projected;
                if (sample == steps) projected = hit;
                else if (!data_->ProjectSurface(point, normal, brush_.radius,
                    interpolation_region_, projected)) continue;
                changed = ApplySurfaceSample(projected, delta_time / steps) || changed;
            }
        }
        previous_hit_ = hit; previous_hit_valid_ = true;
        return changed;
    }

    bool LandscapeEditorTool::ApplySurfaceSample(const LandscapeRayHit& hit, float delta_time)
    {
        if (!data_ || !command_ || !std::isfinite(delta_time) || delta_time <= 0) return false;
        const auto& center = hit.position;
        const auto& vertices = data_->Vertices();
        data_->QuerySurface(center, brush_.radius, hit.face_index, region_);
        auto& changes = changes_;
        changes.clear();
        for (const std::uint32_t index : region_.vertices)
        {
            const LandscapeVertex& vertex = vertices[index];
            const float dx = vertex.position.x - center.x;
            const float dy = vertex.position.y - center.y;
            const float dz = vertex.position.z - center.z;
            const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
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
                const auto& neighbors = data_->AdjacentVertices(index);
                if (neighbors.empty()) break;
                DirectX::XMFLOAT3 average = before;
                int count = 1;
                for (std::uint32_t neighbor : neighbors)
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
            case LandscapeBrushMode::Subdivide:
                break;
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

    bool LandscapeEditorTool::ApplySubdivideSample(LandscapeData& data,
        const DirectX::XMFLOAT3& center, std::size_t hit_face,
        const LandscapeBrush& brush)
    {
        if (data.FaceCount() == 0 || brush.radius <= 0.0f || brush.target_edge_length <= 0.0f ||
            !std::isfinite(brush.radius) || !std::isfinite(brush.target_edge_length)) return false;

        constexpr std::size_t maximum_faces_per_sample = 256;
        const std::size_t capacity = (std::min)({
            maximum_faces_per_sample,
            (LandscapeData::maximum_vertices - data.VertexCount()) / 3,
            (LandscapeData::maximum_indices - data.Indices().size()) / 9 });
        if (capacity == 0) return false;

        const auto& vertices = data.Vertices();
        const auto& indices = data.Indices();
        static thread_local LandscapeData::SurfaceRegion subdivide_region;
        data.QuerySurface(center, brush.radius, hit_face, subdivide_region);
        const float target_sq = brush.target_edge_length * brush.target_edge_length;
        std::vector<std::pair<float, std::size_t>> faces;
        for (const std::size_t face : subdivide_region.faces)
        {
            const std::size_t offset = face * 3;
            const std::uint32_t a = indices[offset];
            const std::uint32_t b = indices[offset + 1];
            const std::uint32_t c = indices[offset + 2];
            if (a >= vertices.size() || b >= vertices.size() || c >= vertices.size()) continue;

            const DirectX::XMFLOAT3 positions[3]{
                vertices[a].position, vertices[b].position, vertices[c].position };
            float longest_sq = 0.0f;
            float nearest_sq = (std::numeric_limits<float>::max)();
            for (int corner = 0; corner < 3; ++corner)
            {
                const auto& position = positions[corner];
                const auto& next = positions[(corner + 1) % 3];
                const float edge_x = next.x - position.x;
                const float edge_y = next.y - position.y;
                const float edge_z = next.z - position.z;
                longest_sq = (std::max)(longest_sq,
                    edge_x * edge_x + edge_y * edge_y + edge_z * edge_z);

                const float dx = position.x - center.x;
                const float dy = position.y - center.y;
                const float dz = position.z - center.z;
                const float distance_sq = dx * dx + dy * dy + dz * dz;
                nearest_sq = (std::min)(nearest_sq, distance_sq);
            }
            // 最長辺が目標以下の面は再分割せず、塗り続けてもここで収束させる。
            if (longest_sq <= target_sq) continue;
            if (face == hit_face) nearest_sq = -1.0f;
            faces.emplace_back(nearest_sq, face);
        }

        if (faces.size() > capacity)
        {
            std::nth_element(faces.begin(), faces.begin() + capacity, faces.end());
            faces.resize(capacity);
        }
        if (faces.empty()) return false;
        std::sort(faces.begin(), faces.end(), [](const auto& left, const auto& right)
        { return left.second < right.second; });

        data.BeginTopologyBatch();
        bool changed = false;
        for (const auto& face : faces)
            changed = data.SubdivideFace(face.second) || changed;
        data.EndTopologyBatch();
        return changed;
    }

    std::unique_ptr<LandscapeUndoCommand> LandscapeEditorTool::EndStroke()
    {
        if (data_ != nullptr) data_->FinishSculpt();
        if (data_ && command_ && mode_ == LandscapeBrushMode::Subdivide) command_->EndTopology(*data_);
        previous_hit_valid_ = false;
        data_ = nullptr;
        if (command_ != nullptr && command_->Empty()) command_.reset();
        if (command_ != nullptr) command_->Seal();
        return std::move(command_);
    }

    void LandscapeEditorTool::CancelStroke()
    {
        if (data_ != nullptr && command_ != nullptr) command_->Undo(*data_);
        if (data_ != nullptr) data_->FinishSculpt();
        previous_hit_valid_ = false;
        data_ = nullptr;
        command_.reset();
    }
}
