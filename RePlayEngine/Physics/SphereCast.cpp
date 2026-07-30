#include "SphereCast.h"

#include <algorithm>
#include <cmath>
#include <limits>

using namespace DirectX;

namespace
{
    constexpr float kEpsilon = 1.0e-6f;

    float Dot3(FXMVECTOR a, FXMVECTOR b) noexcept
    {
        return XMVectorGetX(XMVector3Dot(a, b));
    }

    float LengthSq3(FXMVECTOR value) noexcept
    {
        return Dot3(value, value);
    }

    XMVECTOR ClosestPointOnSegment(FXMVECTOR point, FXMVECTOR a, FXMVECTOR b) noexcept
    {
        const XMVECTOR edge = b - a;
        const float denominator = LengthSq3(edge);
        if (denominator <= kEpsilon) return a;
        const float t = (std::clamp)(Dot3(point - a, edge) / denominator, 0.0f, 1.0f);
        return a + edge * t;
    }

    XMVECTOR ClosestPointOnTriangle(FXMVECTOR point, FXMVECTOR a,
        FXMVECTOR b, FXMVECTOR c) noexcept
    {
        const XMVECTOR ab = b - a;
        const XMVECTOR ac = c - a;
        const XMVECTOR ap = point - a;
        const float d1 = Dot3(ab, ap);
        const float d2 = Dot3(ac, ap);
        if (d1 <= 0.0f && d2 <= 0.0f) return a;

        const XMVECTOR bp = point - b;
        const float d3 = Dot3(ab, bp);
        const float d4 = Dot3(ac, bp);
        if (d3 >= 0.0f && d4 <= d3) return b;

        const float vc = d1 * d4 - d3 * d2;
        if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f)
            return a + ab * (d1 / (d1 - d3));

        const XMVECTOR cp = point - c;
        const float d5 = Dot3(ab, cp);
        const float d6 = Dot3(ac, cp);
        if (d6 >= 0.0f && d5 <= d6) return c;

        const float vb = d5 * d2 - d1 * d6;
        if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f)
            return a + ac * (d2 / (d2 - d6));

        const float va = d3 * d6 - d5 * d4;
        if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f)
            return b + (c - b) * ((d4 - d3) / ((d4 - d3) + (d5 - d6)));

        const float denominator = 1.0f / (va + vb + vc);
        return a + ab * (vb * denominator) + ac * (vc * denominator);
    }

    bool PointInTriangle(FXMVECTOR point, FXMVECTOR a, FXMVECTOR b,
        FXMVECTOR c, FXMVECTOR normal) noexcept
    {
        constexpr float tolerance = -1.0e-5f;
        return Dot3(XMVector3Cross(b - a, point - a), normal) >= tolerance &&
               Dot3(XMVector3Cross(c - b, point - b), normal) >= tolerance &&
               Dot3(XMVector3Cross(a - c, point - c), normal) >= tolerance;
    }

    bool RaySphere(FXMVECTOR origin, FXMVECTOR direction, float max_distance,
        FXMVECTOR sphere_center, float radius, float& distance) noexcept
    {
        const XMVECTOR m = origin - sphere_center;
        const float b = Dot3(m, direction);
        const float c = Dot3(m, m) - radius * radius;
        if (c <= 0.0f) { distance = 0.0f; return true; }
        if (b > 0.0f) return false;
        const float discriminant = b * b - c;
        if (discriminant < 0.0f) return false;
        distance = -b - std::sqrt(discriminant);
        return distance >= 0.0f && distance <= max_distance;
    }

    bool RayCapsuleBody(FXMVECTOR origin, FXMVECTOR direction, float max_distance,
        FXMVECTOR a, FXMVECTOR b, float radius, float& distance,
        XMVECTOR& point_on_edge) noexcept
    {
        const XMVECTOR ba = b - a;
        const XMVECTOR oa = origin - a;
        const float baba = Dot3(ba, ba);
        if (baba <= kEpsilon) return false;
        const float bard = Dot3(ba, direction);
        const float baoa = Dot3(ba, oa);
        const float rdoa = Dot3(direction, oa);
        const float oaoa = Dot3(oa, oa);
        const float qa = baba - bard * bard;
        const float qb = baba * rdoa - baoa * bard;
        const float qc = baba * oaoa - baoa * baoa - radius * radius * baba;
        if (std::fabs(qa) <= kEpsilon) return false;
        const float discriminant = qb * qb - qa * qc;
        if (discriminant < 0.0f) return false;
        const float t = (-qb - std::sqrt(discriminant)) / qa;
        const float y = baoa + t * bard;
        if (t < 0.0f || t > max_distance || y <= 0.0f || y >= baba) return false;
        distance = t;
        point_on_edge = a + ba * (y / baba);
        return true;
    }

    bool NormalAllowed(FXMVECTOR normal,
        const ReplayEngine::Physics::SphereCastQuery& query) noexcept
    {
        const float y = XMVectorGetY(normal);
        return y >= query.minimum_normal_y && y <= query.maximum_normal_y;
    }

    void StoreCandidate(float distance, float cast_length, FXMVECTOR center,
        FXMVECTOR contact, FXMVECTOR normal, bool overlapping,
        ReplayEngine::Physics::SphereCastHit& hit) noexcept
    {
        hit.distance = distance;
        hit.fraction = cast_length > kEpsilon ? distance / cast_length : 0.0f;
        XMStoreFloat3(&hit.center, center);
        XMStoreFloat3(&hit.position, contact);
        XMStoreFloat3(&hit.normal, XMVector3Normalize(normal));
        hit.started_overlapping = overlapping;
    }
}

namespace ReplayEngine::Physics
{
    bool CastSphereAgainstTriangle(const SphereCastQuery& query,
        const Triangle& triangle, SphereCastHit& hit) noexcept
    {
        if (query.radius <= 0.0f) return false;
        const XMVECTOR start = XMLoadFloat3(&query.start);
        const XMVECTOR end = XMLoadFloat3(&query.end);
        const XMVECTOR movement = end - start;
        const float cast_length = XMVectorGetX(XMVector3Length(movement));
        const XMVECTOR direction = cast_length > kEpsilon
            ? movement / cast_length : XMVectorZero();
        const XMVECTOR a = XMLoadFloat3(&triangle.vertices[0]);
        const XMVECTOR b = XMLoadFloat3(&triangle.vertices[1]);
        const XMVECTOR c = XMLoadFloat3(&triangle.vertices[2]);
        const XMVECTOR raw_normal = XMVector3Cross(b - a, c - a);
        if (LengthSq3(raw_normal) <= kEpsilon) return false;
        const XMVECTOR face_normal = XMVector3Normalize(raw_normal);

        float best_distance = (std::numeric_limits<float>::max)();
        SphereCastHit best{};

    // 出現位置の重なりや押し出し処理のため、初期重なりも判定する。
        const XMVECTOR initial_contact = ClosestPointOnTriangle(start, a, b, c);
        XMVECTOR initial_delta = start - initial_contact;
        if (LengthSq3(initial_delta) <= query.radius * query.radius)
        {
            XMVECTOR normal = LengthSq3(initial_delta) > kEpsilon
                ? XMVector3Normalize(initial_delta)
                : (Dot3(direction, face_normal) <= 0.0f ? face_normal : -face_normal);
            if (NormalAllowed(normal, query))
            {
                StoreCandidate(0.0f, cast_length, start, initial_contact, normal, true, best);
                best_distance = 0.0f;
            }
        }

        if (cast_length > kEpsilon && best_distance > 0.0f)
        {
    // 三角形平面の両側について面接触を調べる。
            const float start_plane_distance = Dot3(start - a, face_normal);
            const float plane_velocity = Dot3(direction, face_normal);
            if (std::fabs(plane_velocity) > kEpsilon)
            {
                for (float side : { -1.0f, 1.0f })
                {
                    const XMVECTOR normal = face_normal * side;
                    if (Dot3(normal, direction) >= -kEpsilon || !NormalAllowed(normal, query)) continue;
                    const float distance = (side * query.radius - start_plane_distance) / plane_velocity;
                    if (distance < 0.0f || distance > cast_length || distance >= best_distance) continue;
                    const XMVECTOR center = start + direction * distance;
                    const XMVECTOR contact = center - normal * query.radius;
                    if (!PointInTriangle(contact, a, b, c, face_normal)) continue;
                    StoreCandidate(distance, cast_length, center, contact, normal, false, best);
                    best_distance = distance;
                }
            }

    // 各辺を円柱として接触を調べる。
            const XMVECTOR edge_starts[3]{ a, b, c };
            const XMVECTOR edge_ends[3]{ b, c, a };
            for (int edge = 0; edge < 3; ++edge)
            {
                float distance = 0.0f;
                XMVECTOR contact{};
                if (!RayCapsuleBody(start, direction, cast_length,
                    edge_starts[edge], edge_ends[edge], query.radius, distance, contact) ||
                    distance >= best_distance) continue;
                const XMVECTOR center = start + direction * distance;
                const XMVECTOR normal = XMVector3Normalize(center - contact);
                if (Dot3(normal, direction) >= -kEpsilon || !NormalAllowed(normal, query)) continue;
                StoreCandidate(distance, cast_length, center, contact, normal, false, best);
                best_distance = distance;
            }

    // 各頂点を球として接触を調べる。
            for (XMVECTOR vertex : { a, b, c })
            {
                float distance = 0.0f;
                if (!RaySphere(start, direction, cast_length, vertex, query.radius, distance) ||
                    distance >= best_distance) continue;
                const XMVECTOR center = start + direction * distance;
                const XMVECTOR normal = XMVector3Normalize(center - vertex);
                if (Dot3(normal, direction) >= -kEpsilon || !NormalAllowed(normal, query)) continue;
                StoreCandidate(distance, cast_length, center, vertex, normal, false, best);
                best_distance = distance;
            }
        }

        if (best_distance == (std::numeric_limits<float>::max)()) return false;
        best.material_index = triangle.material_index;
        hit = best;
        return true;
    }

    bool CastSphereAgainstTriangles(const SphereCastQuery& query,
        const Triangle* triangles, std::size_t triangle_count,
        SphereCastHit& hit) noexcept
    {
        if (!triangles || triangle_count == 0) return false;
        bool found = false;
        SphereCastHit closest{};
        closest.distance = (std::numeric_limits<float>::max)();
        for (std::size_t i = 0; i < triangle_count; ++i)
        {
            SphereCastHit candidate{};
            if (CastSphereAgainstTriangle(query, triangles[i], candidate) &&
                candidate.distance < closest.distance)
            {
                candidate.triangle_index = static_cast<std::uint32_t>(i);
                closest = candidate;
                found = true;
            }
        }
        if (found) hit = closest;
        return found;
    }
}
