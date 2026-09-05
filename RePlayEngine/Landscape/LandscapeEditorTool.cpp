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

        float PointSegmentDistanceSq(const DirectX::XMFLOAT3& point,
            const DirectX::XMFLOAT3& a, const DirectX::XMFLOAT3& b) noexcept
        {
            const float ab_x = b.x - a.x;
            const float ab_y = b.y - a.y;
            const float ab_z = b.z - a.z;
            const float ap_x = point.x - a.x;
            const float ap_y = point.y - a.y;
            const float ap_z = point.z - a.z;
            const float length_sq = ab_x * ab_x + ab_y * ab_y + ab_z * ab_z;
            float t = length_sq > 1.0e-12f
                ? (ap_x * ab_x + ap_y * ab_y + ap_z * ab_z) / length_sq : 0.0f;
            t = (std::max)(0.0f, (std::min)(1.0f, t));
            const float dx = ap_x - ab_x * t;
            const float dy = ap_y - ab_y * t;
            const float dz = ap_z - ab_z * t;
            return dx * dx + dy * dy + dz * dz;
        }

        bool BrushTouchesFace(const DirectX::XMFLOAT3& center,
            const DirectX::XMFLOAT3(&positions)[3], const LandscapeBrush& brush) noexcept
        {
            const float radius_sq = brush.radius * brush.radius;
            const bool planar = brush.direction == LandscapeSculptDirection::LocalY;
            DirectX::XMFLOAT3 brush_center = center;
            DirectX::XMFLOAT3 triangle[3]{ positions[0], positions[1], positions[2] };
            if (planar)
            {
                brush_center.y = 0.0f;
                triangle[0].y = triangle[1].y = triangle[2].y = 0.0f;
            }
            const DirectX::XMVECTOR a = DirectX::XMLoadFloat3(&triangle[0]);
            const DirectX::XMVECTOR b = DirectX::XMLoadFloat3(&triangle[1]);
            const DirectX::XMVECTOR c = DirectX::XMLoadFloat3(&triangle[2]);
            const DirectX::XMVECTOR cross = DirectX::XMVector3Cross(
                DirectX::XMVectorSubtract(b, a), DirectX::XMVectorSubtract(c, a));
            if (DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(cross)) <= 1.0e-12f)
                return PointSegmentDistanceSq(brush_center, triangle[0], triangle[1]) <= radius_sq ||
                    PointSegmentDistanceSq(brush_center, triangle[1], triangle[2]) <= radius_sq ||
                    PointSegmentDistanceSq(brush_center, triangle[2], triangle[0]) <= radius_sq;
            return DirectX::BoundingSphere(brush_center, brush.radius).Intersects(a, b, c);
        }
    }

    bool LandscapeEditorTool::BeginStroke(LandscapeData& data,
        LandscapeBrushMode mode, const LandscapeBrush& brush)
    {
        if (StrokeActive() || mode == LandscapeBrushMode::Subdivide ||
            !data.Valid() || brush.radius <= 0.0f ||
            brush.strength < 0.0f || !std::isfinite(brush.radius) ||
            !std::isfinite(brush.strength)) return false;
        previous_hit_valid_ = false;
        data_ = &data;
        mode_ = mode;
        brush_ = brush;
        command_ = std::make_unique<LandscapeUndoCommand>();
        adjacency_.clear();
        if (mode_ == LandscapeBrushMode::Smooth)
        {
            adjacency_.resize(data.VertexCount());
            for (std::size_t vertex = 0; vertex < data.VertexCount(); ++vertex)
            {
                auto& neighbors = adjacency_[vertex];
                for (const auto face : data.AdjacentFaces(vertex))
                    for (int corner = 0; corner < 3; ++corner)
                    {
                        const auto neighbor = data.Indices()[face * 3 + corner];
                        if (neighbor != vertex) neighbors.push_back(neighbor);
                    }
                std::sort(neighbors.begin(), neighbors.end());
                neighbors.erase(std::unique(neighbors.begin(), neighbors.end()), neighbors.end());
            }
        }
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
                if (index >= adjacency_.size() || adjacency_[index].empty()) break;
                DirectX::XMFLOAT3 average = before;
                int count = 1;
                for (std::uint32_t neighbor : adjacency_[index])
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
        if (!data.Valid() || brush.radius <= 0.0f || brush.target_edge_length <= 0.0f ||
            !std::isfinite(brush.radius) || !std::isfinite(brush.target_edge_length)) return false;

        constexpr std::size_t maximum_faces_per_sample = 256;
        const std::size_t capacity = (std::min)({
            maximum_faces_per_sample,
            (LandscapeData::maximum_vertices - data.VertexCount()) / 3,
            (LandscapeData::maximum_indices - data.Indices().size()) / 9 });
        if (capacity == 0) return false;

        const auto& vertices = data.Vertices();
        const auto& indices = data.Indices();
        const std::size_t face_count = data.FaceCount();
        const float target_sq = brush.target_edge_length * brush.target_edge_length;
        std::vector<std::pair<float, std::size_t>> faces;
        for (std::size_t face = 0; face < face_count; ++face)
        {
            const std::size_t offset = face * 3;
            const std::uint32_t a = indices[offset];
            const std::uint32_t b = indices[offset + 1];
            const std::uint32_t c = indices[offset + 2];
            if (a >= vertices.size() || b >= vertices.size() || c >= vertices.size()) continue;

            const DirectX::XMFLOAT3 positions[3]{
                vertices[a].position, vertices[b].position, vertices[c].position };
            if (face != hit_face && !BrushTouchesFace(center, positions, brush)) continue;
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
                const float distance_sq = brush.direction == LandscapeSculptDirection::LocalY
                    ? dx * dx + dz * dz : dx * dx + dy * dy + dz * dz;
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
        previous_hit_valid_ = false;
        data_ = nullptr;
        adjacency_.clear();
        candidate_marks_.clear();
        unindexed_vertices_.clear();
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
        adjacency_.clear();
        candidate_marks_.clear();
        unindexed_vertices_.clear();
        command_.reset();
    }
}
