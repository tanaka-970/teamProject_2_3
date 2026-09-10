// LandscapeData のうち「初期化・形状情報・Chunk 管理」だけを持つ。
//
//   LandscapeData.cpp               … 初期化・形状情報・Raycast・Chunk 管理（このファイル）
//   LandscapeDataInternal.h         … 分割内部で共有するベクトル演算
//   LandscapeDataTopology.cpp       … Face 単位のトポロジ編集
//   LandscapeDataSerialization.cpp  … Inline 形式とファイルの読み書き

#include "LandscapeData.h"
#include "LandscapeDataInternal.h"

#include <DirectXCollision.h>
#include <algorithm>
#include <atomic>
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
        std::uint64_t NextGeometryRevision() noexcept
        {
            static std::atomic<std::uint64_t> revision{1};
            return revision.fetch_add(1, std::memory_order_relaxed);
        }

        bool RayTriangle(const XMFLOAT3& origin, const XMFLOAT3& direction,
            const XMFLOAT3& a, const XMFLOAT3& b, const XMFLOAT3& c,
            float& distance) noexcept
        {
            // Scale-relative, double-precision Moller-Trumbore. A fixed area
            // epsilon turns valid subdivided triangles into unpickable holes.
            const double e1x=double(b.x)-a.x, e1y=double(b.y)-a.y, e1z=double(b.z)-a.z;
            const double e2x=double(c.x)-a.x, e2y=double(c.y)-a.y, e2z=double(c.z)-a.z;
            const double px=direction.y*e2z-direction.z*e2y;
            const double py=direction.z*e2x-direction.x*e2z;
            const double pz=direction.x*e2y-direction.y*e2x;
            const double determinant=e1x*px+e1y*py+e1z*pz;
            const double scale=std::sqrt((e1x*e1x+e1y*e1y+e1z*e1z)*(e2x*e2x+e2y*e2y+e2z*e2z));
            if (scale==0 || std::fabs(determinant)<=scale*1.0e-12) return false;
            const double tx=double(origin.x)-a.x, ty=double(origin.y)-a.y, tz=double(origin.z)-a.z;
            const double u=(tx*px+ty*py+tz*pz)/determinant;
            constexpr double barycentric_tolerance=1.0e-7;
            if (u < -barycentric_tolerance || u > 1+barycentric_tolerance) return false;
            const double qx=ty*e1z-tz*e1y, qy=tz*e1x-tx*e1z, qz=tx*e1y-ty*e1x;
            const double v=(direction.x*qx+direction.y*qy+direction.z*qz)/determinant;
            if (v < -barycentric_tolerance || u+v > 1+barycentric_tolerance) return false;
            const double result=(e2x*qx+e2y*qy+e2z*qz)/determinant;
            if (result < 0 || !std::isfinite(result)) return false;
            distance=static_cast<float>(result);
            return std::isfinite(distance);
        }

        bool RayBounds(const XMFLOAT3& origin, const XMFLOAT3& direction,
            float max_distance, const XMFLOAT3& minimum,
            const XMFLOAT3& maximum) noexcept
        {
            float near_distance = 0.0f;
            float far_distance = max_distance;
            const float origins[3] = { origin.x, origin.y, origin.z };
            const float directions[3] = { direction.x, direction.y, direction.z };
            const float minimums[3] = {
                minimum.x - epsilon, minimum.y - epsilon, minimum.z - epsilon };
            const float maximums[3] = {
                maximum.x + epsilon, maximum.y + epsilon, maximum.z + epsilon };
            for (int axis = 0; axis < 3; ++axis)
            {
                if (directions[axis] == 0.0f)
                {
                    if (origins[axis] < minimums[axis] || origins[axis] > maximums[axis])
                        return false;
                    continue;
                }
                float first = (minimums[axis] - origins[axis]) / directions[axis];
                float second = (maximums[axis] - origins[axis]) / directions[axis];
                if (first > second) std::swap(first, second);
                near_distance = (std::max)(near_distance, first);
                far_distance = (std::min)(far_distance, second);
                if (near_distance > far_distance) return false;
            }
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

        revision_ = NextGeometryRevision();
        geometry_snapshot_.reset();
        touched_vertices_.clear();
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
        revision_ = NextGeometryRevision();
        geometry_snapshot_.reset();
        touched_vertices_.clear();
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
        // Indices are private and validated at import; topology mutators preserve validity.
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
        horizontal_travel_ = (std::max)(horizontal_travel_,
            (std::max)(std::fabs(before.x - position.x), std::fabs(before.z - position.z)));
        geometry_snapshot_.reset();
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
        revision_ = NextGeometryRevision();
        if (revision_ == 0) revision_ = 1;

        // どこを触ったか分からないときだけ全部を上げる。
        if (touched_vertices_.empty() || chunks_.empty())
        {
            MarkAllDirty();
            return;
        }

        // Recompute affected normals from every incident face, including other chunks.
        chunk_dirty_marks_.assign(chunks_.size(), 0);
        normal_vertices_.clear();
        normal_marks_.resize(vertices_.size(), 0);
        if (++normal_generation_ == 0)
        {
            std::fill(normal_marks_.begin(), normal_marks_.end(), 0);
            normal_generation_ = 1;
        }
        for (const auto moved : touched_vertices_)
        {
            for (const auto face : AdjacentFaces(moved))
                for (int corner = 0; corner < 3; ++corner)
                {
                    const auto vertex = indices_[face * 3 + corner];
                    if (normal_marks_[vertex] == normal_generation_) continue;
                    normal_marks_[vertex] = normal_generation_;
                    normal_vertices_.push_back(vertex);
                }
            if (moved < vertex_chunks_.size())
                for (const auto chunk : vertex_chunks_[moved]) chunk_dirty_marks_[chunk] = 1;
        }
        for (const auto vertex : normal_vertices_)
        {
            XMFLOAT3 normal{};
            for (const auto face : AdjacentFaces(vertex))
            {
                const auto* triangle = indices_.data() + face * 3;
                normal = Add(normal, Cross(
                    Sub(vertices_[triangle[1]].position, vertices_[triangle[0]].position),
                    Sub(vertices_[triangle[2]].position, vertices_[triangle[0]].position)));
            }
            vertices_[vertex].normal = Normalize(normal);
            for (const auto chunk_index : vertex_chunks_[vertex])
            {
                chunks_[chunk_index].render_dirty = true;
                chunks_[chunk_index].revision = revision_;
            }
        }

        for (std::size_t i = 0; i < chunks_.size(); ++i)
        {
            if (chunk_dirty_marks_[i] == 0) continue;
            LandscapeChunk& chunk = chunks_[i];
            chunk.revision = revision_;
            chunk.render_dirty = true;
            chunk.collision_dirty = true;
            RecalculateChunkBounds(chunk);
        }
        touched_vertices_.clear();
    }

    void LandscapeData::FinalizeGeometryEdit() noexcept
    {
        geometry_snapshot_.reset();
        if (topology_batch_depth_ > 0)
        {
            topology_batch_dirty_ = true;
            return;
        }

        std::size_t chunk_index_count = 0;
        for (const LandscapeChunk& chunk : chunks_) chunk_index_count += chunk.indices.size();
        if (chunk_index_count != indices_.size())
        {
            RecalculateNormals();
            RecalculateBounds();
            touched_vertices_.clear();
            TouchGeometry();
            BuildChunks();
            return;
        }

        bool topology_changed = false;
        for (std::size_t chunk=0; chunk<chunk_topology_dirty_.size(); ++chunk)
            if (chunk_topology_dirty_[chunk])
            {
                RebuildChunkLayout(chunk);
                chunk_topology_dirty_[chunk] = 0;
                topology_changed = true;
            }
        if (topology_changed) topology_revision_ = NextGeometryRevision();

        // どこを触ったか分かっているときは、法線も境界もそのチャンクだけで済ませる。
        // 全走査すると 1 ストロークごとに全頂点を 3 周することになる。
        const bool partial = !touched_vertices_.empty() && !chunks_.empty();
        if (!partial) RecalculateNormals();
        RecalculateBoundsFromTouched(partial);
        TouchGeometry();
    }

    void LandscapeData::EndTopologyBatch() noexcept
    {
        if (topology_batch_depth_ <= 0) return;
        --topology_batch_depth_;
        if (topology_batch_depth_ == 0 && topology_batch_dirty_)
        {
            topology_batch_dirty_ = false;
            FinalizeGeometryEdit();
        }
    }

    // 触った頂点だけで全体の境界を広げる。縮む方向は塗り終わりに任せる。
    void LandscapeData::RecalculateBoundsFromTouched(bool partial) noexcept
    {
        if (!partial) { RecalculateBounds(); return; }
        for (const std::size_t index : touched_vertices_)
        {
            if (index >= vertices_.size()) continue;
            const DirectX::XMFLOAT3& p = vertices_[index].position;
            bounds_min_.x = (std::min)(bounds_min_.x, p.x);
            bounds_min_.y = (std::min)(bounds_min_.y, p.y);
            bounds_min_.z = (std::min)(bounds_min_.z, p.z);
            bounds_max_.x = (std::max)(bounds_max_.x, p.x);
            bounds_max_.y = (std::max)(bounds_max_.y, p.y);
            bounds_max_.z = (std::max)(bounds_max_.z, p.z);
        }
    }

    void LandscapeData::RecalculateChunkNormals(const LandscapeChunk& chunk) noexcept
    {
        for (const auto vertex : chunk.vertex_map)
        {
            XMFLOAT3 normal{};
            for (const auto face : AdjacentFaces(vertex))
            {
                const auto* t = indices_.data() + face * 3;
                normal = Add(normal, Cross(Sub(vertices_[t[1]].position, vertices_[t[0]].position),
                    Sub(vertices_[t[2]].position, vertices_[t[0]].position)));
            }
            vertices_[vertex].normal = Normalize(normal);
        }
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
        for (LandscapeChunk& chunk : chunks_) RecalculateChunkBounds(chunk);
    }
    bool LandscapeData::Raycast(const XMFLOAT3& origin, const XMFLOAT3& direction,
        float max_distance, LandscapeRayHit& hit) const noexcept
    {
        hit = LandscapeRayHit{};
        last_raycast_triangle_test_count_ = 0;
        if (!Valid() || !Finite3(origin) || !Finite3(direction) ||
            !std::isfinite(max_distance) || max_distance <= 0.0f) return false;
        const XMFLOAT3 ray_direction = Normalize(direction);
        float best = max_distance;
        std::size_t best_face = static_cast<std::size_t>(-1);
        const auto test_face = [&](std::size_t face, const std::uint32_t* triangle)
        {
            ++last_raycast_triangle_test_count_;
            float distance = 0.0f;
            if (!RayTriangle(origin, ray_direction,
                vertices_[triangle[0]].position,
                vertices_[triangle[1]].position,
                vertices_[triangle[2]].position, distance)) return;
            if (distance > best || (distance == best && best_face !=
                static_cast<std::size_t>(-1) && face < best_face)) return;
            best = distance;
            best_face = face;
        };

        std::size_t chunk_face_count = 0;
        bool chunks_valid = !chunks_.empty() && chunk_faces_.size() == chunks_.size();
        if (chunks_valid)
        {
            for (std::size_t chunk_index = 0; chunk_index < chunks_.size(); ++chunk_index)
            {
                const LandscapeChunk& chunk = chunks_[chunk_index];
                if (chunk.indices.size() != chunk_faces_[chunk_index].size() * 3u)
                {
                    chunks_valid = false;
                    break;
                }
                chunk_face_count += chunk_faces_[chunk_index].size();
            }
        }
        chunks_valid = chunks_valid && chunk_face_count == FaceCount();

        if (chunks_valid)
        {
            for (std::size_t chunk_index = 0; chunk_index < chunks_.size(); ++chunk_index)
            {
                const LandscapeChunk& chunk = chunks_[chunk_index];
                if (chunk.indices.empty() || !RayBounds(origin, ray_direction, max_distance,
                    chunk.bounds_min, chunk.bounds_max)) continue;
                for (std::size_t face = 0; face < chunk_faces_[chunk_index].size(); ++face)
                    test_face(chunk_faces_[chunk_index][face], chunk.indices.data() + face * 3u);
            }
        }
        else
        {
            for (std::size_t face = 0; face < FaceCount(); ++face)
                test_face(face, indices_.data() + face * 3u);
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
        topology_revision_ = NextGeometryRevision();
        chunks_.clear();
        chunk_faces_.clear();
        vertex_chunks_.clear();
        vertex_faces_.clear();
        vertex_neighbors_.clear();
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
        chunk_faces_.resize(chunks_.size());
        face_chunks_.resize(FaceCount());
        face_chunk_offsets_.resize(FaceCount());
        chunk_topology_dirty_.assign(chunks_.size(),0);
        subdivision_repartition_pending_ = false;
        vertex_chunks_.resize(vertices_.size());
        vertex_faces_.resize(vertices_.size());
        vertex_neighbors_.resize(vertices_.size());
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

            const std::uint32_t chunk_index = static_cast<std::uint32_t>(cz * divisions + cx);
            LandscapeChunk& chunk = chunks_[chunk_index];
            face_chunks_[offset/3] = chunk_index;
            face_chunk_offsets_[offset/3] = static_cast<std::uint32_t>(chunk.indices.size());
            chunk.indices.push_back(indices_[offset]);
            chunk.indices.push_back(indices_[offset + 1]);
            chunk.indices.push_back(indices_[offset + 2]);
            chunk_faces_[chunk_index].push_back(static_cast<std::uint32_t>(offset / 3u));
            for (int corner = 0; corner < 3; ++corner)
            {
                vertex_chunks_[indices_[offset + corner]].push_back(chunk_index);
                vertex_faces_[indices_[offset + corner]].push_back(static_cast<std::uint32_t>(offset / 3));
                for (int other=0; other<3; ++other)
                    if (indices_[offset+corner] != indices_[offset+other])
                        vertex_neighbors_[indices_[offset+corner]].push_back(indices_[offset+other]);
            }
        }

        for (auto& memberships : vertex_chunks_)
        {
            std::sort(memberships.begin(), memberships.end());
            memberships.erase(std::unique(memberships.begin(), memberships.end()), memberships.end());
        }
        for (auto& neighbors : vertex_neighbors_)
        {
            std::sort(neighbors.begin(), neighbors.end());
            neighbors.erase(std::unique(neighbors.begin(), neighbors.end()), neighbors.end());
        }
        chunk_remap_.assign(vertices_.size(),UINT32_MAX);
        for (std::size_t chunk=0;chunk<chunks_.size();++chunk)
        {
            RebuildChunkLayout(chunk);
            RecalculateChunkBounds(chunks_[chunk]);
        }
        horizontal_travel_ = 0.0f;
    }

    void LandscapeData::RebuildChunkLayout(std::size_t index)
    {
        if (index >= chunks_.size()) return;
        auto& chunk=chunks_[index];
        chunk_remap_.resize(vertices_.size(),UINT32_MAX);
        chunk.vertex_map.clear(); chunk.local_indices.clear();
        for(const auto vertex:chunk.indices)
        {
            if(chunk_remap_[vertex]==UINT32_MAX)
            {
                chunk_remap_[vertex]=static_cast<std::uint32_t>(chunk.vertex_map.size());
                chunk.vertex_map.push_back(vertex);
            }
            chunk.local_indices.push_back(chunk_remap_[vertex]);
        }
        for(const auto vertex:chunk.vertex_map) chunk_remap_[vertex]=UINT32_MAX;
    }

    void LandscapeData::UpdateSubdivisionAdjacency(std::size_t face, std::uint32_t a,
        std::uint32_t b, std::uint32_t c, std::uint32_t first_vertex, std::size_t first_face)
    {
        if (face >= face_chunks_.size() || chunks_.empty()) return;
        const auto chunk_index=face_chunks_[face];
        if(chunk_index>=chunks_.size()) return;
        auto& chunk=chunks_[chunk_index];
        const auto local_offset=face_chunk_offsets_[face];
        if(local_offset+2>=chunk.indices.size()) return;
        vertex_faces_.resize(vertices_.size());
        vertex_neighbors_.resize(vertices_.size());
        vertex_chunks_.resize(vertices_.size());
        face_chunks_.resize(FaceCount(),chunk_index);
        face_chunk_offsets_.resize(FaceCount());
        for(const auto vertex:{a,b,c})
        {
            auto& adjacent=vertex_faces_[vertex];
            adjacent.erase(std::remove(adjacent.begin(),adjacent.end(),face),adjacent.end());
        }
        for(std::uint32_t vertex=first_vertex;vertex<first_vertex+3;++vertex)
            vertex_chunks_[vertex].push_back(chunk_index);
        for(int i=0;i<3;++i) chunk.indices[local_offset+i]=indices_[face*3+i];
        for(std::size_t f=first_face;f<first_face+3;++f)
        {
            face_chunk_offsets_[f]=static_cast<std::uint32_t>(chunk.indices.size());
            chunk_faces_[chunk_index].push_back(static_cast<std::uint32_t>(f));
            chunk.indices.insert(chunk.indices.end(),indices_.begin()+f*3,indices_.begin()+f*3+3);
        }
        for(const std::size_t f:{face,first_face,first_face+1,first_face+2})
            for(int corner=0;corner<3;++corner)
                vertex_faces_[indices_[f*3+corner]].push_back(static_cast<std::uint32_t>(f));
        for(const auto vertex:{a,b,c,first_vertex,first_vertex+1,first_vertex+2})
        {
            auto& neighbors=vertex_neighbors_[vertex]; neighbors.clear();
            for(const auto f:vertex_faces_[vertex])
                for(int corner=0;corner<3;++corner)
                {
                    const auto neighbor=indices_[f*3+corner];
                    if(neighbor!=vertex) neighbors.push_back(neighbor);
                }
            std::sort(neighbors.begin(),neighbors.end());
            neighbors.erase(std::unique(neighbors.begin(),neighbors.end()),neighbors.end());
            MarkVertexDirty(vertex);
        }
        chunk_topology_dirty_[chunk_index]=1;
        subdivision_repartition_pending_=true;
    }

    const std::vector<std::uint32_t>& LandscapeData::AdjacentFaces(std::size_t vertex) const noexcept
    {
        static const std::vector<std::uint32_t> empty;
        return vertex < vertex_faces_.size() ? vertex_faces_[vertex] : empty;
    }

    const std::vector<std::uint32_t>& LandscapeData::AdjacentVertices(std::size_t vertex) const noexcept
    {
        static const std::vector<std::uint32_t> empty;
        return vertex < vertex_neighbors_.size() ? vertex_neighbors_[vertex] : empty;
    }

    void LandscapeData::QuerySurface(const XMFLOAT3& center, float radius,
        std::size_t seed_face, SurfaceRegion& region) const
    {
        region.faces.clear(); region.vertices.clear();
        if (seed_face >= FaceCount() || !Finite3(center) || !std::isfinite(radius) || radius <= 0) return;
        // resize initializes only newly added marks after subdivision.
        region.face_marks.resize(FaceCount(), 0);
        region.vertex_marks.resize(VertexCount(), 0);
        if (++region.generation == 0)
        {
            std::fill(region.face_marks.begin(), region.face_marks.end(), 0);
            std::fill(region.vertex_marks.begin(), region.vertex_marks.end(), 0);
            region.generation = 1;
        }
        const BoundingSphere sphere(center, radius);
        const auto visit = [&](std::uint32_t face)
        {
            if (region.face_marks[face] == region.generation) return;
            region.face_marks[face] = region.generation;
            const auto* t = indices_.data() + face * 3;
            const auto a = XMLoadFloat3(&vertices_[t[0]].position);
            const auto b = XMLoadFloat3(&vertices_[t[1]].position);
            const auto c = XMLoadFloat3(&vertices_[t[2]].position);
            const float edge_product = XMVectorGetX(XMVector3LengthSq(b-a))*XMVectorGetX(XMVector3LengthSq(c-a));
            const float area_sq = XMVectorGetX(XMVector3LengthSq(XMVector3Cross(b-a,c-a)));
            if (edge_product==0 || area_sq <= edge_product*1.0e-12f) return;
            if (sphere.Intersects(a, b, c)) region.faces.push_back(face);
        };
        visit(static_cast<std::uint32_t>(seed_face));
        for (std::size_t cursor = 0; cursor < region.faces.size(); ++cursor)
        {
            const auto face = region.faces[cursor];
            for (int corner = 0; corner < 3; ++corner)
            {
                const auto vertex = indices_[face * 3 + corner];
                if (region.vertex_marks[vertex] == region.generation) continue;
                region.vertex_marks[vertex] = region.generation;
                const auto offset = Sub(vertices_[vertex].position, center);
                if (Dot(offset, offset) <= radius * radius) region.vertices.push_back(vertex);
                for (const auto adjacent : AdjacentFaces(vertex)) visit(adjacent);
            }
        }
    }

    bool LandscapeData::ProjectSurface(const XMFLOAT3& point, const XMFLOAT3& normal,
        float distance, const SurfaceRegion& region, LandscapeRayHit& hit) const
    {
        hit = {};
        float nearest = distance;
        const auto direction = Normalize(normal);
        for (const auto face : region.faces)
        {
            if (face >= FaceCount()) continue;
            const auto* t = indices_.data() + face * 3;
            for (const float sign : { -1.0f, 1.0f })
            {
                const auto ray = Mul(direction, sign);
                float d = 0.0f;
                if (!RayTriangle(point, ray, vertices_[t[0]].position,
                    vertices_[t[1]].position, vertices_[t[2]].position, d) || d > nearest) continue;
                nearest = d;
                hit.hit = true; hit.distance = d; hit.face_index = face;
                hit.position = Add(point, Mul(ray, d)); hit.normal = FaceNormal(face);
            }
        }
        return hit.hit;
    }

    std::shared_ptr<const LandscapeGeometry> LandscapeData::CaptureGeometry() const
    {
        if (!geometry_snapshot_)
        {
            auto snapshot = std::make_shared<LandscapeGeometry>();
            snapshot->width = width_; snapshot->height = height_; snapshot->cell_size = cell_size_;
            snapshot->vertices = vertices_; snapshot->indices = indices_;
            geometry_snapshot_ = std::move(snapshot);
        }
        return geometry_snapshot_;
    }

    void LandscapeData::RestoreGeometry(const std::shared_ptr<const LandscapeGeometry>& geometry)
    {
        if (!geometry) return;
        if (InitializeMesh(geometry->vertices, geometry->indices, geometry->cell_size,
            geometry->width, geometry->height)) geometry_snapshot_ = geometry;
    }

    void LandscapeData::FinishSculpt()
    {
        RecalculateBounds();
        // Repartition only when horizontal deformation has actually occurred.
        if (horizontal_travel_ > 1.0e-5f || subdivision_repartition_pending_)
        {
            revision_ = NextGeometryRevision();
            BuildChunks();
        }
    }

    void LandscapeData::MarkAllDirty() noexcept
    {
        if (chunks_.empty()) BuildChunks();
        for (LandscapeChunk& chunk : chunks_)
        {
            chunk.revision = revision_;
            chunk.render_dirty = true;
            chunk.collision_dirty = true;
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
