#include "VertexColorPaint.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <unordered_map>

namespace ReplayEngine::VertexPaint
{
    using namespace DirectX;

    namespace
    {
        bool Finite(const XMFLOAT3& p)
        {
            return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
        }

        struct PositionHash final
        {
            std::size_t operator()(const std::array<float, 3>& p) const noexcept
            {
                std::size_t h = 0;
                for (float v : p) h ^= std::hash<float>{}(v) + 0x9e3779b9u + (h << 6) + (h >> 2);
                return h;
            }
        };

        std::uint8_t& Channel(Assets::VertexColorRgba8& c, int channel)
        {
            switch (channel) { case 1: return c.g; case 2: return c.b; case 3: return c.a; default: return c.r; }
        }
    }

    RayHit MeshSurface::RaycastMesh(const std::vector<XMFLOAT3>& positions,
        const std::vector<std::uint32_t>& indices, const XMFLOAT4X4& world,
        const XMFLOAT3& origin, const XMFLOAT3& direction, float max_distance)
    {
        if (positions.empty() || indices.empty() || indices.size() % 3 != 0) return {};
        for (auto index : indices) if (index >= positions.size()) return {};
        MeshSurface surface;
        surface.positions_ = positions;
        surface.indices_ = indices;
        if (!surface.SetPositions(positions, world)) return {};
        return surface.Raycast(origin, direction, max_distance);
    }
    bool MeshSurface::Initialize(const std::vector<XMFLOAT3>& positions,
        const std::vector<std::uint32_t>& indices)
    {
        positioned_ = false;
        if (positions.empty() || positions.size() > UINT32_MAX || indices.empty() ||
            indices.size() % 3 != 0 || indices.size() > UINT32_MAX) return false;
        for (const auto& p : positions) if (!Finite(p)) return false;
        for (auto i : indices) if (i >= positions.size()) return false;
        positions_ = positions;
        indices_ = indices;
        groups_.resize(positions.size());
        welded_.clear();
        std::unordered_map<std::array<float, 3>, std::uint32_t, PositionHash> lookup;
        for (std::size_t i = 0; i < positions.size(); ++i)
        {
            const auto& p = positions[i];
            const auto result = lookup.emplace(std::array<float, 3>{ p.x, p.y, p.z },
                static_cast<std::uint32_t>(welded_.size()));
            if (result.second) welded_.emplace_back();
            groups_[i] = result.first->second;
            welded_[groups_[i]].push_back(static_cast<std::uint32_t>(i));
        }
        faces_.assign(welded_.size(), {});
        neighbors_.assign(welded_.size(), {});
        for (std::size_t f = 0; f < indices.size() / 3; ++f)
            for (int c = 0; c < 3; ++c)
            {
                const auto group = groups_[indices[f * 3 + c]];
                faces_[group].push_back(static_cast<std::uint32_t>(f));
                for (int n = 0; n < 3; ++n)
                {
                    const auto other = groups_[indices[f * 3 + n]];
                    if (other != group) neighbors_[group].push_back(other);
                }
            }
        for (auto& adjacent : neighbors_)
        {
            std::sort(adjacent.begin(), adjacent.end());
            adjacent.erase(std::unique(adjacent.begin(), adjacent.end()), adjacent.end());
        }
        XMFLOAT4X4 identity;
        XMStoreFloat4x4(&identity, XMMatrixIdentity());
        return SetPositions(positions, identity);
    }

    bool MeshSurface::SetPositions(const std::vector<XMFLOAT3>& positions, const XMFLOAT4X4& world)
    {
        positioned_ = false;
        if (positions.empty() || positions.size() != positions_.size()) return false;
        positions_.resize(positions.size());
        const auto matrix = XMLoadFloat4x4(&world);
        for (std::size_t i = 0; i < positions.size(); ++i)
        {
            XMStoreFloat3(&positions_[i], XMVector3TransformCoord(XMLoadFloat3(&positions[i]), matrix));
            if (!Finite(positions_[i])) return false;
        }
        BoundingBox::CreateFromPoints(bounds_, positions_.size(), positions_.data(), sizeof(XMFLOAT3));
        positioned_ = true;
        return true;
    }

    RayHit MeshSurface::Raycast(const XMFLOAT3& origin, const XMFLOAT3& direction, float max_distance) const
    {
        RayHit hit;
        if (!positioned_ || !Finite(origin) || !Finite(direction) ||
            !std::isfinite(max_distance) || max_distance <= 0) return hit;
        const auto o = XMLoadFloat3(&origin);
        const auto d0 = XMLoadFloat3(&direction);
        if (XMVectorGetX(XMVector3LengthSq(d0)) < 1.0e-20f) return hit;
        const auto d = XMVector3Normalize(d0);
        float distance;
        if (!bounds_.Intersects(o, d, distance) || distance > max_distance) return hit;
        float nearest = max_distance;
        for (std::size_t f = 0; f < indices_.size() / 3; ++f)
        {
            const auto a = XMLoadFloat3(&positions_[indices_[f * 3]]);
            const auto b = XMLoadFloat3(&positions_[indices_[f * 3 + 1]]);
            const auto c = XMLoadFloat3(&positions_[indices_[f * 3 + 2]]);
            if (!TriangleTests::Intersects(o, d, a, b, c, distance) || distance > nearest) continue;
            const auto e0 = b - a, e1 = c - a, v = o + d * distance - a;
            const float aa = XMVectorGetX(XMVector3Dot(e0, e0));
            const float ab = XMVectorGetX(XMVector3Dot(e0, e1));
            const float bb = XMVectorGetX(XMVector3Dot(e1, e1));
            const float va = XMVectorGetX(XMVector3Dot(v, e0));
            const float vb = XMVectorGetX(XMVector3Dot(v, e1));
            const float denom = aa * bb - ab * ab;
            if (denom <= aa * bb * 1.0e-12f) continue;
            const float u = (bb * va - ab * vb) / denom;
            const float w = (aa * vb - ab * va) / denom;
            nearest = distance;
            hit.hit = true; hit.distance = distance; hit.face = f;
            hit.barycentric = { 1.0f - u - w, u, w };
            XMStoreFloat3(&hit.position, o + d * distance);
        }
        return hit;
    }

    SurfaceRegion MeshSurface::QuerySurface(const XMFLOAT3& center, float radius, std::size_t seed_face) const
    {
        SurfaceRegion region;
        if (!positioned_ || !Finite(center) || !std::isfinite(radius) || radius <= 0 ||
            seed_face >= indices_.size() / 3) return region;
        const BoundingSphere sphere(center, radius);
        std::vector<bool> seen(indices_.size() / 3), seen_group(welded_.size());
        const auto visit = [&](std::uint32_t f)
        {
            if (seen[f]) return;
            seen[f] = true;
            const auto a = XMLoadFloat3(&positions_[indices_[f * 3]]);
            const auto b = XMLoadFloat3(&positions_[indices_[f * 3 + 1]]);
            const auto c = XMLoadFloat3(&positions_[indices_[f * 3 + 2]]);
            const float product = XMVectorGetX(XMVector3LengthSq(b - a)) * XMVectorGetX(XMVector3LengthSq(c - a));
            if (product == 0 || XMVectorGetX(XMVector3LengthSq(XMVector3Cross(b - a, c - a))) <= product * 1.0e-12f) return;
            if (sphere.Intersects(a, b, c)) region.faces.push_back(f);
        };
        visit(static_cast<std::uint32_t>(seed_face));
        for (std::size_t cursor = 0; cursor < region.faces.size(); ++cursor)
            for (int c = 0; c < 3; ++c)
            {
                const auto group = groups_[indices_[region.faces[cursor] * 3 + c]];
                if (seen_group[group]) continue;
                seen_group[group] = true;
                for (auto vertex : welded_[group])
                    if (XMVectorGetX(XMVector3LengthSq(XMLoadFloat3(&positions_[vertex]) - XMLoadFloat3(&center))) < radius * radius)
                        region.vertices.push_back(vertex);
                for (auto face : faces_[group]) visit(face);
            }
        return region;
    }

    bool MeshSurface::Paint(std::vector<Assets::VertexColorRgba8>& colors, const SurfaceRegion& region,
        const XMFLOAT3& center, const Brush& brush, float seconds, std::vector<float>& channel_values) const
    {
        if (!positioned_ || colors.size() != positions_.size() || brush.channel < 0 || brush.channel > 3 ||
            !Finite(center) || !std::isfinite(brush.radius) || brush.radius <= 0 ||
            !std::isfinite(brush.strength) || !std::isfinite(brush.falloff) || !std::isfinite(brush.value) ||
            !std::isfinite(seconds) || seconds <= 0) return false;
        if (channel_values.size() != colors.size())
        {
            channel_values.resize(colors.size());
            for (std::size_t i = 0; i < colors.size(); ++i) channel_values[i] = Channel(colors[i], brush.channel) / 255.0f;
        }
        const auto previous = channel_values;
        std::vector<bool> touched(welded_.size()), inside(colors.size());
        for (auto v : region.vertices) if (v < inside.size()) inside[v] = true;
        bool changed = false;
        for (auto vertex : region.vertices)
        {
            if (vertex >= colors.size()) continue;
            const auto group = groups_[vertex];
            if (touched[group]) continue;
            touched[group] = true;
            float distance = brush.radius, current = 0, count = 0;
            for (auto v : welded_[group]) if (inside[v])
            {
                distance = (std::min)(distance, XMVectorGetX(XMVector3Length(XMLoadFloat3(&positions_[v]) - XMLoadFloat3(&center))));
                current += previous[v]; ++count;
            }
            if (count == 0) continue;
            current /= count;
            const float weight = std::pow((std::max)(0.0f, 1.0f - distance / brush.radius),
                (std::max)(0.01f, brush.falloff));
            const float amount = (std::max)(0.0f, brush.strength) * seconds * weight;
            if (amount <= 0.0f) continue;
            float target = std::clamp(brush.value, 0.0f, 1.0f);
            if (brush.mode == PaintMode::Smooth)
            {
                float sum = 0, n = 0;
                for (auto neighbor : neighbors_[group])
                {
                    float value = 0, members = 0;
                    for (auto v : welded_[neighbor]) if (inside[v]) { value += previous[v]; ++members; }
                    if (members > 0) { sum += value / members; ++n; }
                }
                target = n > 0 ? sum / n : current;
            }
            float value = current;
            if (brush.mode == PaintMode::Add) value += amount;
            else if (brush.mode == PaintMode::Subtract) value -= amount;
            else value += (target - current) * (1.0f - std::exp(-amount));
            value = std::clamp(value, 0.0f, 1.0f);
            for (auto v : welded_[group]) if (inside[v])
            {
                channel_values[v] = value;
                const auto packed = static_cast<std::uint8_t>(std::lround(value * 255.0f));
                auto& channel = Channel(colors[v], brush.channel);
                changed |= channel != packed;
                channel = packed;
            }
        }
        return changed;
    }

    void ColorEdit::Apply(bool redo) const
    {
        if (!target) return;
        target->colors = redo ? after : before;
        ++target->revision;
        target->dirty = true;
    }
}
