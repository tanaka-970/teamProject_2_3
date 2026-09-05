// LandscapeData のうち「初期化・形状情報・Chunk 管理」だけを持つ。
//
//   LandscapeData.cpp               … 初期化・形状情報・Raycast・Chunk 管理（このファイル）
//   LandscapeDataInternal.h         … 分割内部で共有するベクトル演算
//   LandscapeDataTopology.cpp       … Face 単位のトポロジ編集
//   LandscapeDataSerialization.cpp  … Inline 形式とファイルの読み書き

#include "LandscapeData.h"
#include "LandscapeDataInternal.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>

using namespace DirectX;

namespace ReplayEngine::Landscape
{
    using namespace Detail;

    namespace
    {
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
        MarkVertexDirty(index);
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

    void LandscapeData::MarkVertexDirty(std::size_t index) noexcept
    {
        if (index < vertices_.size()) touched_vertices_.push_back(index);
    }

    void LandscapeData::TouchGeometry() noexcept
    {
        ++revision_;
        if (revision_ == 0) revision_ = 1;

        // どこを触ったか分からないときだけ全部を上げる。
        if (touched_vertices_.empty() || chunks_.empty())
        {
            MarkAllDirty();
            return;
        }

        const int divisions = chunk_divisions_;
        const float span_x = (std::max)(0.0001f, bounds_max_.x - bounds_min_.x);
        const float span_z = (std::max)(0.0001f, bounds_max_.z - bounds_min_.z);
        for (const std::size_t index : touched_vertices_)
        {
            if (index >= vertices_.size()) continue;
            const DirectX::XMFLOAT3& p = vertices_[index].position;
            const int cx = static_cast<int>((p.x - bounds_min_.x) / span_x * divisions);
            const int cz = static_cast<int>((p.z - bounds_min_.z) / span_z * divisions);

            // 三角形は重心で振り分けているので、隣接チャンクもまとめて上げる。
            for (int oz = -1; oz <= 1; ++oz)
            {
                for (int ox = -1; ox <= 1; ++ox)
                {
                    const int nx = cx + ox;
                    const int nz = cz + oz;
                    if (nx < 0 || nz < 0 || nx >= divisions || nz >= divisions) continue;
                    LandscapeChunk& chunk =
                        chunks_[static_cast<std::size_t>(nz) * divisions + nx];
                    chunk.revision = revision_;
                    chunk.render_dirty = true;
                    chunk.collision_dirty = true;
                    RecalculateChunkBounds(chunk);
                }
            }
        }
        touched_vertices_.clear();
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

    // 三角形を XZ で区切って束ねる。任意 topology なので格子は前提にせず、重心で振り分ける。
    void LandscapeData::BuildChunks()
    {
        chunks_.clear();
        chunk_divisions_ = 1;
        if (vertices_.empty() || indices_.size() < 3) return;

        // 1 チャンクがおよそ chunk_target_vertices 頂点になる分割数を選ぶ。
        const double estimated = static_cast<double>(vertices_.size()) /
            static_cast<double>(chunk_target_vertices);
        int divisions = static_cast<int>(std::ceil(std::sqrt((std::max)(1.0, estimated))));
        divisions = (std::max)(1, (std::min)(divisions, chunk_maximum_divisions));
        chunk_divisions_ = divisions;

        const float span_x = (std::max)(0.0001f, bounds_max_.x - bounds_min_.x);
        const float span_z = (std::max)(0.0001f, bounds_max_.z - bounds_min_.z);

        chunks_.resize(static_cast<std::size_t>(divisions) * divisions);
        for (int z = 0; z < divisions; ++z)
        {
            for (int x = 0; x < divisions; ++x)
            {
                LandscapeChunk& chunk = chunks_[static_cast<std::size_t>(z) * divisions + x];
                chunk.coord = { x, z };
                chunk.revision = revision_;
                chunk.render_dirty = true;
                chunk.collision_dirty = true;
            }
        }

        // 三角形の重心が入るチャンクへ配る。境界の頂点は複数チャンクへ複製される。
        for (std::size_t offset = 0; offset + 2 < indices_.size(); offset += 3)
        {
            const DirectX::XMFLOAT3& a = vertices_[indices_[offset]].position;
            const DirectX::XMFLOAT3& b = vertices_[indices_[offset + 1]].position;
            const DirectX::XMFLOAT3& c = vertices_[indices_[offset + 2]].position;
            const float center_x = (a.x + b.x + c.x) / 3.0f;
            const float center_z = (a.z + b.z + c.z) / 3.0f;

            int cx = static_cast<int>((center_x - bounds_min_.x) / span_x * divisions);
            int cz = static_cast<int>((center_z - bounds_min_.z) / span_z * divisions);
            cx = (std::max)(0, (std::min)(cx, divisions - 1));
            cz = (std::max)(0, (std::min)(cz, divisions - 1));

            LandscapeChunk& chunk = chunks_[static_cast<std::size_t>(cz) * divisions + cx];
            chunk.indices.push_back(indices_[offset]);
            chunk.indices.push_back(indices_[offset + 1]);
            chunk.indices.push_back(indices_[offset + 2]);
        }

        for (LandscapeChunk& chunk : chunks_) RecalculateChunkBounds(chunk);
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

    // カリングに使うので、そのチャンクが実際に持つ三角形だけから求める。
    void LandscapeData::RecalculateChunkBounds(LandscapeChunk& chunk) noexcept
    {
        if (chunk.indices.empty())
        {
            chunk.bounds_min = bounds_min_;
            chunk.bounds_max = bounds_min_;
            return;
        }

        constexpr float huge_value = (std::numeric_limits<float>::max)();
        DirectX::XMFLOAT3 minimum{ huge_value, huge_value, huge_value };
        DirectX::XMFLOAT3 maximum{ -huge_value, -huge_value, -huge_value };
        for (const std::uint32_t index : chunk.indices)
        {
            if (index >= vertices_.size()) continue;
            const DirectX::XMFLOAT3& p = vertices_[index].position;
            minimum.x = (std::min)(minimum.x, p.x);
            minimum.y = (std::min)(minimum.y, p.y);
            minimum.z = (std::min)(minimum.z, p.z);
            maximum.x = (std::max)(maximum.x, p.x);
            maximum.y = (std::max)(maximum.y, p.y);
            maximum.z = (std::max)(maximum.z, p.z);
        }
        chunk.bounds_min = minimum;
        chunk.bounds_max = maximum;
    }
}
