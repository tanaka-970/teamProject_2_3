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
}
