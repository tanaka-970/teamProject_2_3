#include "LandscapeData.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>

using namespace DirectX;

namespace ReplayEngine::Landscape
{
    namespace
    {
        constexpr float epsilon = 1.0e-6f;

        XMFLOAT3 Add(const XMFLOAT3& a, const XMFLOAT3& b) noexcept
        { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
        XMFLOAT3 Sub(const XMFLOAT3& a, const XMFLOAT3& b) noexcept
        { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
        XMFLOAT3 Mul(const XMFLOAT3& a, float s) noexcept
        { return { a.x * s, a.y * s, a.z * s }; }
        float Dot(const XMFLOAT3& a, const XMFLOAT3& b) noexcept
        { return a.x * b.x + a.y * b.y + a.z * b.z; }
        XMFLOAT3 Cross(const XMFLOAT3& a, const XMFLOAT3& b) noexcept
        {
            return { a.y * b.z - a.z * b.y,
                a.z * b.x - a.x * b.z,
                a.x * b.y - a.y * b.x };
        }
        XMFLOAT3 Normalize(const XMFLOAT3& value) noexcept
        {
            const float length_sq = Dot(value, value);
            if (length_sq <= epsilon * epsilon) return { 0.0f, 1.0f, 0.0f };
            const float inverse = 1.0f / std::sqrt(length_sq);
            return Mul(value, inverse);
        }
        bool Finite3(const XMFLOAT3& v) noexcept
        { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

        bool RayTriangle(const XMFLOAT3& origin, const XMFLOAT3& direction,
            const XMFLOAT3& a, const XMFLOAT3& b, const XMFLOAT3& c,
            float& distance) noexcept
        {
            // Moller-Trumbore。両面を拾うので determinant の符号では弾かない。
            const XMFLOAT3 edge1 = Sub(b, a);
            const XMFLOAT3 edge2 = Sub(c, a);
            const XMFLOAT3 p = Cross(direction, edge2);
            const float determinant = Dot(edge1, p);
            if (std::fabs(determinant) <= epsilon) return false;
            const float inv = 1.0f / determinant;
            const XMFLOAT3 t = Sub(origin, a);
            const float u = Dot(t, p) * inv;
            if (u < 0.0f || u > 1.0f) return false;
            const XMFLOAT3 q = Cross(t, edge1);
            const float v = Dot(direction, q) * inv;
            if (v < 0.0f || u + v > 1.0f) return false;
            const float result = Dot(edge2, q) * inv;
            if (result < 0.0f) return false;
            distance = result;
            return true;
        }
    }

    bool LandscapeData::IsFinite(const LandscapeVertex& vertex) noexcept
    {
        return Finite3(vertex.position) && Finite3(vertex.normal) &&
            std::isfinite(vertex.uv.x) && std::isfinite(vertex.uv.y);
    }

    bool LandscapeData::Initialize(int width, int height, float cell_size,
        float initial_height)
    {
        if (width < 2 || height < 2 || width > maximum_resolution ||
            height > maximum_resolution || !std::isfinite(cell_size) ||
            cell_size <= 0.0f || !std::isfinite(initial_height)) return false;

        const std::size_t vertex_count = static_cast<std::size_t>(width) * height;
        const std::size_t index_count = static_cast<std::size_t>(width - 1) *
            static_cast<std::size_t>(height - 1) * 6u;
        if (vertex_count > maximum_vertices || index_count > maximum_indices) return false;

        width_ = width;
        height_ = height;
        cell_size_ = cell_size;
        vertices_.clear();
        indices_.clear();
        vertices_.reserve(vertex_count);
        indices_.reserve(index_count);

        const float u_denominator = static_cast<float>(width - 1);
        const float v_denominator = static_cast<float>(height - 1);
        for (int z = 0; z < height; ++z)
        {
            for (int x = 0; x < width; ++x)
            {
                LandscapeVertex vertex;
                vertex.position = { x * cell_size, initial_height, z * cell_size };
                vertex.normal = { 0.0f, 1.0f, 0.0f };
                vertex.uv = { x / u_denominator, z / v_denominator };
                vertices_.push_back(vertex);
            }
        }

        const auto grid_index = [width](int x, int z)
        { return static_cast<std::uint32_t>(z * width + x); };
        for (int z = 0; z + 1 < height; ++z)
        {
            for (int x = 0; x + 1 < width; ++x)
            {
                const std::uint32_t a = grid_index(x, z);
                const std::uint32_t b = grid_index(x + 1, z);
                const std::uint32_t c = grid_index(x, z + 1);
                const std::uint32_t d = grid_index(x + 1, z + 1);
                // +Y normal になる winding。
                indices_.insert(indices_.end(), { a, c, b, b, c, d });
            }
        }

        revision_ = 1;
        RecalculateNormals();
        RecalculateBounds();
        BuildChunks();
        MarkAllDirty();
        return true;
    }

    bool LandscapeData::InitializeMesh(std::vector<LandscapeVertex> vertices,
        std::vector<std::uint32_t> indices, float grid_cell_hint,
        int grid_width_hint, int grid_height_hint)
    {
        if (vertices.size() < 3 || vertices.size() > maximum_vertices ||
            indices.size() < 3 || indices.size() > maximum_indices ||
            indices.size() % 3 != 0 || !std::isfinite(grid_cell_hint) ||
            grid_cell_hint <= 0.0f) return false;
        for (const LandscapeVertex& vertex : vertices) if (!IsFinite(vertex)) return false;
        for (std::uint32_t index : indices) if (index >= vertices.size()) return false;

        width_ = (std::max)(0, grid_width_hint);
        height_ = (std::max)(0, grid_height_hint);
        cell_size_ = grid_cell_hint;
        vertices_ = std::move(vertices);
        indices_ = std::move(indices);
        revision_ = 1;
        RecalculateNormals();
        RecalculateBounds();
        BuildChunks();
        MarkAllDirty();
        return true;
    }

    bool LandscapeData::Valid() const noexcept
    {
        if (vertices_.size() < 3 || indices_.size() < 3 || indices_.size() % 3 != 0 ||
            vertices_.size() > maximum_vertices || indices_.size() > maximum_indices ||
            !std::isfinite(cell_size_) || cell_size_ <= 0.0f) return false;
        for (std::uint32_t index : indices_) if (index >= vertices_.size()) return false;
        return true;
    }

    std::size_t LandscapeData::SampleCount() const noexcept
    {
        if (width_ <= 0 || height_ <= 0) return 0;
        return (std::min)(vertices_.size(),
            static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_));
    }

    std::size_t LandscapeData::Index(int x, int z) const noexcept
    {
        if (!Contains(x, z)) return static_cast<std::size_t>(-1);
        return static_cast<std::size_t>(z) * static_cast<std::size_t>(width_) +
            static_cast<std::size_t>(x);
    }

    bool LandscapeData::Contains(int x, int z) const noexcept
    { return x >= 0 && z >= 0 && x < width_ && z < height_; }

    float LandscapeData::HeightAt(int x, int z) const noexcept
    {
        const std::size_t index = Index(x, z);
        return index < vertices_.size() ? vertices_[index].position.y : 0.0f;
    }

    float LandscapeData::HeightByIndex(std::size_t index) const noexcept
    { return index < SampleCount() ? vertices_[index].position.y : 0.0f; }

    bool LandscapeData::SetHeight(int x, int z, float value) noexcept
    { return SetHeightByIndex(Index(x, z), value); }

    bool LandscapeData::SetHeightByIndex(std::size_t index, float value) noexcept
    {
        if (index >= SampleCount() || !std::isfinite(value)) return false;
        XMFLOAT3 position = vertices_[index].position;
        if (std::fabs(position.y - value) <= epsilon) return false;
        position.y = value;
        return SetVertexPosition(index, position, true);
    }

    XMFLOAT3 LandscapeData::VertexPosition(std::size_t index) const noexcept
    { return index < vertices_.size() ? vertices_[index].position : XMFLOAT3{}; }

    bool LandscapeData::SetVertexPosition(std::size_t index,
        const XMFLOAT3& position, bool finalize) noexcept
    {
        if (index >= vertices_.size() || !Finite3(position)) return false;
        const XMFLOAT3 before = vertices_[index].position;
        if (std::fabs(before.x - position.x) <= epsilon &&
            std::fabs(before.y - position.y) <= epsilon &&
            std::fabs(before.z - position.z) <= epsilon) return false;
        vertices_[index].position = position;
        if (finalize) FinalizeGeometryEdit();
        return true;
    }

    XMFLOAT3 LandscapeData::FaceNormal(std::size_t face_index) const noexcept
    {
        const std::size_t offset = face_index * 3;
        if (offset + 2 >= indices_.size()) return { 0.0f, 1.0f, 0.0f };
        const XMFLOAT3& a = vertices_[indices_[offset]].position;
        const XMFLOAT3& b = vertices_[indices_[offset + 1]].position;
        const XMFLOAT3& c = vertices_[indices_[offset + 2]].position;
        return Normalize(Cross(Sub(b, a), Sub(c, a)));
    }

    XMFLOAT3 LandscapeData::FaceCenter(std::size_t face_index) const noexcept
    {
        const std::size_t offset = face_index * 3;
        if (offset + 2 >= indices_.size()) return {};
        const XMFLOAT3& a = vertices_[indices_[offset]].position;
        const XMFLOAT3& b = vertices_[indices_[offset + 1]].position;
        const XMFLOAT3& c = vertices_[indices_[offset + 2]].position;
        return Mul(Add(Add(a, b), c), 1.0f / 3.0f);
    }

    void LandscapeData::TouchGeometry() noexcept
    {
        ++revision_;
        if (revision_ == 0) revision_ = 1;
        MarkAllDirty();
    }

    void LandscapeData::FinalizeGeometryEdit() noexcept
    {
        RecalculateNormals();
        RecalculateBounds();
        TouchGeometry();
    }

    void LandscapeData::RecalculateNormals() noexcept
    {
        for (LandscapeVertex& vertex : vertices_) vertex.normal = { 0.0f, 0.0f, 0.0f };
        for (std::size_t offset = 0; offset + 2 < indices_.size(); offset += 3)
        {
            const std::uint32_t ia = indices_[offset];
            const std::uint32_t ib = indices_[offset + 1];
            const std::uint32_t ic = indices_[offset + 2];
            if (ia >= vertices_.size() || ib >= vertices_.size() || ic >= vertices_.size()) continue;
            const XMFLOAT3 edge1 = Sub(vertices_[ib].position, vertices_[ia].position);
            const XMFLOAT3 edge2 = Sub(vertices_[ic].position, vertices_[ia].position);
            const XMFLOAT3 face = Cross(edge1, edge2);
            vertices_[ia].normal = Add(vertices_[ia].normal, face);
            vertices_[ib].normal = Add(vertices_[ib].normal, face);
            vertices_[ic].normal = Add(vertices_[ic].normal, face);
        }
        for (LandscapeVertex& vertex : vertices_) vertex.normal = Normalize(vertex.normal);
    }

    void LandscapeData::RecalculateBounds() noexcept
    {
        if (vertices_.empty()) { bounds_min_ = {}; bounds_max_ = {}; return; }
        bounds_min_ = vertices_.front().position;
        bounds_max_ = vertices_.front().position;
        for (const LandscapeVertex& vertex : vertices_)
        {
            bounds_min_.x = (std::min)(bounds_min_.x, vertex.position.x);
            bounds_min_.y = (std::min)(bounds_min_.y, vertex.position.y);
            bounds_min_.z = (std::min)(bounds_min_.z, vertex.position.z);
            bounds_max_.x = (std::max)(bounds_max_.x, vertex.position.x);
            bounds_max_.y = (std::max)(bounds_max_.y, vertex.position.y);
            bounds_max_.z = (std::max)(bounds_max_.z, vertex.position.z);
        }
        for (LandscapeChunk& chunk : chunks_)
        {
            chunk.bounds_min = bounds_min_;
            chunk.bounds_max = bounds_max_;
        }
    }

    LandscapeVertex LandscapeData::Midpoint(std::uint32_t a, std::uint32_t b) const noexcept
    {
        LandscapeVertex result;
        if (a >= vertices_.size() || b >= vertices_.size()) return result;
        const LandscapeVertex& va = vertices_[a];
        const LandscapeVertex& vb = vertices_[b];
        result.position = Mul(Add(va.position, vb.position), 0.5f);
        result.normal = Normalize(Add(va.normal, vb.normal));
        result.uv = { (va.uv.x + vb.uv.x) * 0.5f, (va.uv.y + vb.uv.y) * 0.5f };
        return result;
    }

    bool LandscapeData::SubdivideFace(std::size_t face_index)
    {
        const std::size_t offset = face_index * 3;
        if (offset + 2 >= indices_.size() || vertices_.size() + 3 > maximum_vertices ||
            indices_.size() + 9 > maximum_indices) return false;

        const std::uint32_t a = indices_[offset];
        const std::uint32_t b = indices_[offset + 1];
        const std::uint32_t c = indices_[offset + 2];
        const std::uint32_t ab = static_cast<std::uint32_t>(vertices_.size());
        vertices_.push_back(Midpoint(a, b));
        const std::uint32_t bc = static_cast<std::uint32_t>(vertices_.size());
        vertices_.push_back(Midpoint(b, c));
        const std::uint32_t ca = static_cast<std::uint32_t>(vertices_.size());
        vertices_.push_back(Midpoint(c, a));

        indices_[offset] = a; indices_[offset + 1] = ab; indices_[offset + 2] = ca;
        indices_.insert(indices_.end(), {
            ab, b, bc,
            ca, bc, c,
            ab, bc, ca });
        FinalizeGeometryEdit();
        return true;
    }

    bool LandscapeData::DeleteFace(std::size_t face_index)
    {
        const std::size_t offset = face_index * 3;
        if (offset + 2 >= indices_.size() || indices_.size() <= 3) return false;
        indices_.erase(indices_.begin() + static_cast<std::ptrdiff_t>(offset),
            indices_.begin() + static_cast<std::ptrdiff_t>(offset + 3));
        FinalizeGeometryEdit();
        return true;
    }

    bool LandscapeData::ExtrudeFace(std::size_t face_index, float distance)
    {
        const std::size_t offset = face_index * 3;
        if (offset + 2 >= indices_.size() || !std::isfinite(distance) ||
            std::fabs(distance) <= epsilon || vertices_.size() + 3 > maximum_vertices ||
            indices_.size() + 21 > maximum_indices) return false;

        const std::uint32_t a = indices_[offset];
        const std::uint32_t b = indices_[offset + 1];
        const std::uint32_t c = indices_[offset + 2];
        const XMFLOAT3 normal = FaceNormal(face_index);
        const XMFLOAT3 delta = Mul(normal, distance);
        const std::uint32_t a2 = static_cast<std::uint32_t>(vertices_.size());
        LandscapeVertex va = vertices_[a]; va.position = Add(va.position, delta); vertices_.push_back(va);
        const std::uint32_t b2 = static_cast<std::uint32_t>(vertices_.size());
        LandscapeVertex vb = vertices_[b]; vb.position = Add(vb.position, delta); vertices_.push_back(vb);
        const std::uint32_t c2 = static_cast<std::uint32_t>(vertices_.size());
        LandscapeVertex vc = vertices_[c]; vc.position = Add(vc.position, delta); vertices_.push_back(vc);

        // 元の面を底として残し、上面 + 3 側面を追加する。
        indices_.insert(indices_.end(), {
            a2, b2, c2,
            a, a2, b, b, a2, b2,
            b, b2, c, c, b2, c2,
            c, c2, a, a, c2, a2 });
        FinalizeGeometryEdit();
        return true;
    }

    bool LandscapeData::InsetFace(std::size_t face_index, float amount)
    {
        const std::size_t offset = face_index * 3;
        if (offset + 2 >= indices_.size() || !std::isfinite(amount)) return false;
        amount = (std::max)(0.0f, (std::min)(0.95f, amount));
        if (amount <= epsilon || vertices_.size() + 3 > maximum_vertices ||
            indices_.size() + 18 > maximum_indices) return false;

        const std::uint32_t a = indices_[offset];
        const std::uint32_t b = indices_[offset + 1];
        const std::uint32_t c = indices_[offset + 2];
        const XMFLOAT3 center = FaceCenter(face_index);
        const auto inset_vertex = [this, &center, amount](std::uint32_t source)
        {
            LandscapeVertex vertex = vertices_[source];
            vertex.position = Add(vertex.position, Mul(Sub(center, vertex.position), amount));
            return vertex;
        };
        const std::uint32_t ai = static_cast<std::uint32_t>(vertices_.size());
        vertices_.push_back(inset_vertex(a));
        const std::uint32_t bi = static_cast<std::uint32_t>(vertices_.size());
        vertices_.push_back(inset_vertex(b));
        const std::uint32_t ci = static_cast<std::uint32_t>(vertices_.size());
        vertices_.push_back(inset_vertex(c));

        // 元の face を中央 inset face に差し替え、外周 ring を足す。
        indices_[offset] = ai; indices_[offset + 1] = bi; indices_[offset + 2] = ci;
        indices_.insert(indices_.end(), {
            a, b, ai, b, bi, ai,
            b, c, bi, c, ci, bi,
            c, a, ci, a, ai, ci });
        FinalizeGeometryEdit();
        return true;
    }

    bool LandscapeData::CreateTunnelFromFace(std::size_t face_index, float depth,
        int segments, float end_scale)
    {
        const std::size_t offset = face_index * 3;
        if (offset + 2 >= indices_.size() || !std::isfinite(depth) || depth <= epsilon ||
            !std::isfinite(end_scale)) return false;
        segments = (std::max)(1, (std::min)(64, segments));
        end_scale = (std::max)(0.05f, (std::min)(4.0f, end_scale));
        if (vertices_.size() + static_cast<std::size_t>(segments) * 3 > maximum_vertices ||
            indices_.size() + static_cast<std::size_t>(segments) * 18 + 3 > maximum_indices)
            return false;

        const std::uint32_t ring0[3] = {
            indices_[offset], indices_[offset + 1], indices_[offset + 2] };
        const XMFLOAT3 normal = FaceNormal(face_index);
        const XMFLOAT3 center = FaceCenter(face_index);
        LandscapeVertex base[3] = {
            vertices_[ring0[0]], vertices_[ring0[1]], vertices_[ring0[2]] };

        // 入口の face を消す。これが Cut Hole になる。
        indices_.erase(indices_.begin() + static_cast<std::ptrdiff_t>(offset),
            indices_.begin() + static_cast<std::ptrdiff_t>(offset + 3));

        std::uint32_t previous[3] = { ring0[0], ring0[1], ring0[2] };
        for (int segment = 1; segment <= segments; ++segment)
        {
            const float t = static_cast<float>(segment) / static_cast<float>(segments);
            const float scale = 1.0f + (end_scale - 1.0f) * t;
            const XMFLOAT3 step = Mul(normal, -depth * t);
            std::uint32_t current[3]{};
            for (int corner = 0; corner < 3; ++corner)
            {
                LandscapeVertex vertex = base[corner];
                const XMFLOAT3 radial = Sub(vertex.position, center);
                vertex.position = Add(Add(center, Mul(radial, scale)), step);
                current[corner] = static_cast<std::uint32_t>(vertices_.size());
                vertices_.push_back(vertex);
            }

            // ring 間の 3 quad。入口側から見た winding を維持する。
            for (int edge = 0; edge < 3; ++edge)
            {
                const int next = (edge + 1) % 3;
                indices_.insert(indices_.end(), {
                    previous[edge], current[edge], previous[next],
                    previous[next], current[edge], current[next] });
            }
            previous[0] = current[0]; previous[1] = current[1]; previous[2] = current[2];
        }

        // 終端 cap。入口は開いたままなので Scene View から内部へ入れる。
        indices_.insert(indices_.end(), { previous[2], previous[1], previous[0] });
        FinalizeGeometryEdit();
        return true;
    }

    bool LandscapeData::BridgeEdges(std::uint32_t a0, std::uint32_t a1,
        std::uint32_t b0, std::uint32_t b1)
    {
        if (a0 >= vertices_.size() || a1 >= vertices_.size() ||
            b0 >= vertices_.size() || b1 >= vertices_.size() ||
            a0 == a1 || b0 == b1 ||
            a0 == b0 || a0 == b1 || a1 == b0 || a1 == b1 ||
            indices_.size() + 6 > maximum_indices) return false;

        // 2 edge の端点対応は、交差方向ではなく距離の短い組み合わせを選ぶ。
        // Scene View で edge をどちら向きにクリックしても Bridge が捻れにくい。
        const auto distance_sq = [this](std::uint32_t lhs, std::uint32_t rhs)
        {
            const XMFLOAT3 delta = Sub(vertices_[lhs].position, vertices_[rhs].position);
            return Dot(delta, delta);
        };
        const float straight = distance_sq(a0, b0) + distance_sq(a1, b1);
        const float crossed = distance_sq(a0, b1) + distance_sq(a1, b0);
        if (crossed < straight) std::swap(b0, b1);

        // quad = 2 triangles。4点すべて別頂点に限定して degenerate face を防ぐ。
        indices_.insert(indices_.end(), { a0, b0, a1, a1, b0, b1 });
        FinalizeGeometryEdit();
        return true;
    }

    bool LandscapeData::Raycast(const XMFLOAT3& origin, const XMFLOAT3& direction,
        float max_distance, LandscapeRayHit& hit) const noexcept
    {
        hit = LandscapeRayHit{};
        if (!Valid() || !Finite3(origin) || !Finite3(direction) ||
            !std::isfinite(max_distance) || max_distance <= 0.0f) return false;
        const XMFLOAT3 ray_direction = Normalize(direction);
        float best = max_distance;
        std::size_t best_face = static_cast<std::size_t>(-1);
        for (std::size_t face = 0; face < FaceCount(); ++face)
        {
            const std::size_t offset = face * 3;
            float distance = 0.0f;
            if (!RayTriangle(origin, ray_direction,
                vertices_[indices_[offset]].position,
                vertices_[indices_[offset + 1]].position,
                vertices_[indices_[offset + 2]].position, distance)) continue;
            if (distance > best) continue;
            best = distance;
            best_face = face;
        }
        if (best_face == static_cast<std::size_t>(-1)) return false;
        hit.hit = true;
        hit.distance = best;
        hit.face_index = best_face;
        hit.position = Add(origin, Mul(ray_direction, best));
        hit.normal = FaceNormal(best_face);
        return true;
    }

    LandscapeChunk* LandscapeData::FindChunk(LandscapeChunkCoord coord) noexcept
    {
        for (LandscapeChunk& chunk : chunks_) if (chunk.coord == coord) return &chunk;
        return nullptr;
    }
    const LandscapeChunk* LandscapeData::FindChunk(LandscapeChunkCoord coord) const noexcept
    {
        for (const LandscapeChunk& chunk : chunks_) if (chunk.coord == coord) return &chunk;
        return nullptr;
    }

    void LandscapeData::BuildChunks()
    {
        chunks_.clear();
        LandscapeChunk chunk;
        chunk.coord = { 0, 0 };
        chunk.bounds_min = bounds_min_;
        chunk.bounds_max = bounds_max_;
        chunk.revision = revision_;
        chunk.render_dirty = true;
        chunk.collision_dirty = true;
        chunks_.push_back(chunk);
    }

    void LandscapeData::MarkAllDirty() noexcept
    {
        if (chunks_.empty()) BuildChunks();
        for (LandscapeChunk& chunk : chunks_)
        {
            chunk.revision = revision_;
            chunk.render_dirty = true;
            chunk.collision_dirty = true;
            chunk.bounds_min = bounds_min_;
            chunk.bounds_max = bounds_max_;
        }
    }

    void LandscapeData::MarkSampleDirty(int, int) noexcept
    {
        // 任意 topology では格子 sample -> chunk の 1:1 対応を前提にしない。
        MarkAllDirty();
    }

    void LandscapeData::RecalculateChunkBounds(LandscapeChunk& chunk) noexcept
    {
        chunk.bounds_min = bounds_min_;
        chunk.bounds_max = bounds_max_;
    }

    std::string LandscapeData::SerializeInline() const
    {
        if (!Valid()) return {};
        std::ostringstream stream;
        stream << std::setprecision(9);
        stream << "RPLM2 " << width_ << ' ' << height_ << ' ' << cell_size_ << ' '
            << vertices_.size() << ' ' << indices_.size();
        for (const LandscapeVertex& vertex : vertices_)
        {
            // normal は load 後に再計算できるので position + uv のみ保存。
            stream << ' ' << vertex.position.x << ' ' << vertex.position.y << ' '
                << vertex.position.z << ' ' << vertex.uv.x << ' ' << vertex.uv.y;
        }
        for (std::uint32_t index : indices_) stream << ' ' << index;
        return stream.str();
    }

    bool LandscapeData::DeserializeInline(const std::string& text, std::string& error)
    {
        std::istringstream stream(text);
        std::string signature;
        int width = 0, height = 0;
        float cell = 1.0f;
        std::size_t vertex_count = 0, index_count = 0;
        if (!(stream >> signature >> width >> height >> cell >> vertex_count >> index_count) ||
            signature != "RPLM2" || vertex_count < 3 || vertex_count > maximum_vertices ||
            index_count < 3 || index_count > maximum_indices || index_count % 3 != 0 ||
            !std::isfinite(cell) || cell <= 0.0f)
        {
            error = "Landscape inline data header が不正です。";
            return false;
        }

        std::vector<LandscapeVertex> vertices(vertex_count);
        for (LandscapeVertex& vertex : vertices)
        {
            if (!(stream >> vertex.position.x >> vertex.position.y >> vertex.position.z >>
                vertex.uv.x >> vertex.uv.y) || !IsFinite(vertex))
            {
                error = "Landscape inline vertex を読み取れません。";
                return false;
            }
        }
        std::vector<std::uint32_t> indices(index_count);
        for (std::uint32_t& index : indices)
        {
            std::uint64_t raw = 0;
            if (!(stream >> raw) || raw >= vertex_count)
            {
                error = "Landscape inline index を読み取れません。";
                return false;
            }
            index = static_cast<std::uint32_t>(raw);
        }
        if (!InitializeMesh(std::move(vertices), std::move(indices), cell, width, height))
        {
            error = "Landscape inline mesh を初期化できません。";
            return false;
        }
        return true;
    }

    bool LandscapeData::Save(const std::filesystem::path& path, std::string& error) const
    {
        if (!Valid()) { error = "LandscapeDataが無効です。"; return false; }
        std::error_code filesystem_error;
        if (!path.parent_path().empty())
            std::filesystem::create_directories(path.parent_path(), filesystem_error);
        if (filesystem_error) { error = "Landscape保存Folderを作成できません。"; return false; }

        const std::filesystem::path temporary = path.string() + ".tmp";
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        if (!stream) { error = "Landscape temporary fileを作成できません。"; return false; }
        stream << "REPLAY_LANDSCAPE " << current_version << '\n';
        stream << SerializeInline() << '\n';
        stream.flush();
        if (!stream) { error = "Landscape書き込みに失敗しました。"; return false; }
        stream.close();

        LandscapeData verification;
        if (!Load(temporary, verification, error)) return false;
        const std::filesystem::path backup = path.string() + ".bak";
        const bool existing = std::filesystem::exists(path, filesystem_error) && !filesystem_error;
        if (existing)
        {
            std::error_code ignored;
            std::filesystem::remove(backup, ignored);
            std::filesystem::rename(path, backup, filesystem_error);
            if (filesystem_error) { error = "Landscape backupを作成できません。"; return false; }
        }
        filesystem_error.clear();
        std::filesystem::rename(temporary, path, filesystem_error);
        if (filesystem_error)
        {
            if (existing)
            {
                std::error_code ignored;
                std::filesystem::rename(backup, path, ignored);
            }
            error = "Landscapeを安全に差し替えられません。";
            return false;
        }
        return true;
    }

    bool LandscapeData::Load(const std::filesystem::path& path, LandscapeData& output,
        std::string& error)
    {
        std::ifstream stream(path, std::ios::binary);
        std::string magic;
        int version = 0;
        if (!(stream >> magic >> version) || magic != "REPLAY_LANDSCAPE")
        {
            error = "Landscape file signature が不正です。";
            return false;
        }

        if (version == 1)
        {
            // v1: width height cell + height value x width*height。
            int width = 0, height = 0;
            float cell = 0.0f;
            if (!(stream >> width >> height >> cell) || !output.Initialize(width, height, cell, 0.0f))
            {
                error = "Landscape v1 header が不正です。";
                return false;
            }
            for (std::size_t index = 0; index < output.SampleCount(); ++index)
            {
                float value = 0.0f;
                if (!(stream >> value) || !std::isfinite(value))
                {
                    error = "Landscape v1 height gridを読み取れません。";
                    return false;
                }
                output.vertices_[index].position.y = value;
            }
            // 読み込んだ時点で v2 arbitrary mesh へ変換済み。
            output.FinalizeGeometryEdit();
            return true;
        }

        if (version != current_version)
        {
            error = "未対応の Landscape version です: " + std::to_string(version);
            return false;
        }

        std::string inline_data;
        std::getline(stream >> std::ws, inline_data);
        if (!output.DeserializeInline(inline_data, error)) return false;
        output.MarkAllDirty();
        return true;
    }
}
