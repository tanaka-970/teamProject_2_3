#pragma once

// PhysicsDynamicsWorld の分割実装だけが共有する内部数値ヘルパ。
// 外部の Physics コードから include するものではない。

#include <DirectXMath.h>
#include <algorithm>
#include <cmath>
#include "CollisionLayers.h"

using namespace DirectX;

namespace ReplayEngine::Scene::PhysicsDynamicsDetail
{
        namespace Layers = Physics::CollisionLayers;

        constexpr float gravity_y = -9.81f;
        constexpr float sleep_linear_speed = 0.05f;
        constexpr float sleep_angular_speed = 0.05f;
        constexpr float sleep_delay = 0.5f;
        constexpr float position_slop = 0.005f;
        constexpr float position_correction_factor = 0.2f;
        constexpr float epsilon = 1.0e-6f;

        inline float Clamp01(float value) noexcept
        {
            return (std::max)(0.0f, (std::min)(1.0f, value));
        }

        inline float SafeFloat(float value, float fallback) noexcept
        {
            return std::isfinite(value) ? value : fallback;
        }

        inline XMFLOAT3 Negate(const XMFLOAT3& value) noexcept
        {
            return XMFLOAT3{ -value.x, -value.y, -value.z };
        }

        inline XMFLOAT3 ClampPointToBox(const XMFLOAT3& point,
            const XMFLOAT3& center, const XMFLOAT3& half) noexcept
        {
            return XMFLOAT3{
                (std::max)(center.x - half.x, (std::min)(center.x + half.x, point.x)),
                (std::max)(center.y - half.y, (std::min)(center.y + half.y, point.y)),
                (std::max)(center.z - half.z, (std::min)(center.z + half.z, point.z)) };
        }

        inline float PointBoxDistanceSquared(const XMFLOAT3& point,
            const XMFLOAT3& center, const XMFLOAT3& half,
            XMFLOAT3& closest) noexcept
        {
            closest = ClampPointToBox(point, center, half);
            const XMFLOAT3 difference{
                point.x - closest.x, point.y - closest.y, point.z - closest.z };
            return difference.x * difference.x + difference.y * difference.y +
                difference.z * difference.z;
        }

        inline XMFLOAT3 ClosestPointOnSegment(const XMFLOAT3& point,
            const XMFLOAT3& a, const XMFLOAT3& b) noexcept
        {
            const XMFLOAT3 edge{ b.x - a.x, b.y - a.y, b.z - a.z };
            const float denominator = edge.x * edge.x + edge.y * edge.y + edge.z * edge.z;
            if (denominator <= epsilon) return a;

            const XMFLOAT3 from_a{ point.x - a.x, point.y - a.y, point.z - a.z };
            const float parameter = Clamp01((from_a.x * edge.x + from_a.y * edge.y +
                from_a.z * edge.z) / denominator);
            return XMFLOAT3{ a.x + edge.x * parameter, a.y + edge.y * parameter,
                a.z + edge.z * parameter };
        }

        inline float DistanceSquared(const XMFLOAT3& a, const XMFLOAT3& b) noexcept
        {
            const XMFLOAT3 difference{ a.x - b.x, a.y - b.y, a.z - b.z };
            return difference.x * difference.x + difference.y * difference.y +
                difference.z * difference.z;
        }

        inline void SegmentClosestPoints(const XMFLOAT3& a0, const XMFLOAT3& a1,
            const XMFLOAT3& b0, const XMFLOAT3& b1,
            XMFLOAT3& point_a, XMFLOAT3& point_b) noexcept
        {
            const XMFLOAT3 d1{ a1.x - a0.x, a1.y - a0.y, a1.z - a0.z };
            const XMFLOAT3 d2{ b1.x - b0.x, b1.y - b0.y, b1.z - b0.z };
            const XMFLOAT3 r{ a0.x - b0.x, a0.y - b0.y, a0.z - b0.z };
            const float a = d1.x * d1.x + d1.y * d1.y + d1.z * d1.z;
            const float e = d2.x * d2.x + d2.y * d2.y + d2.z * d2.z;
            const float f = d2.x * r.x + d2.y * r.y + d2.z * r.z;

            float s = 0.0f;
            float t = 0.0f;
            if (a <= epsilon && e <= epsilon)
            {
                point_a = a0;
                point_b = b0;
                return;
            }

            if (a <= epsilon)
            {
                s = 0.0f;
                t = Clamp01(f / e);
            }
            else
            {
                const float c = d1.x * r.x + d1.y * r.y + d1.z * r.z;
                if (e <= epsilon)
                {
                    t = 0.0f;
                    s = Clamp01(-c / a);
                }
                else
                {
                    const float b = d1.x * d2.x + d1.y * d2.y + d1.z * d2.z;
                    const float denominator = a * e - b * b;
                    s = denominator > epsilon ? Clamp01((b * f - c * e) / denominator) : 0.0f;
                    t = (b * s + f) / e;
                    if (t < 0.0f)
                    {
                        t = 0.0f;
                        s = Clamp01(-c / a);
                    }
                    else if (t > 1.0f)
                    {
                        t = 1.0f;
                        s = Clamp01((b - c) / a);
                    }
                }
            }

            point_a = XMFLOAT3{ a0.x + d1.x * s, a0.y + d1.y * s, a0.z + d1.z * s };
            point_b = XMFLOAT3{ b0.x + d2.x * t, b0.y + d2.y * t, b0.z + d2.z * t };
        }

        inline XMFLOAT3 ClosestPointOnBoxForSegment(const XMFLOAT3& a,
            const XMFLOAT3& b, const XMFLOAT3& center,
            const XMFLOAT3& half, XMFLOAT3& segment_point) noexcept
        {
            // 点と AABB の距離は凸関数なので、三分探索で線分上の最近接点を
            // 求める。回転箱は Collider のワールド AABB として安全側に扱う。
            float low = 0.0f;
            float high = 1.0f;
            const XMFLOAT3 edge{ b.x - a.x, b.y - a.y, b.z - a.z };
            for (int iteration = 0; iteration < 12; ++iteration)
            {
                const float first = low + (high - low) / 3.0f;
                const float second = high - (high - low) / 3.0f;
                const XMFLOAT3 first_point{ a.x + edge.x * first, a.y + edge.y * first,
                    a.z + edge.z * first };
                const XMFLOAT3 second_point{ a.x + edge.x * second, a.y + edge.y * second,
                    a.z + edge.z * second };
                XMFLOAT3 first_closest{};
                XMFLOAT3 second_closest{};
                const float first_distance = PointBoxDistanceSquared(first_point, center, half,
                    first_closest);
                const float second_distance = PointBoxDistanceSquared(second_point, center, half,
                    second_closest);
                if (first_distance < second_distance) high = second;
                else low = first;
            }

            const float parameter = (low + high) * 0.5f;
            segment_point = XMFLOAT3{ a.x + edge.x * parameter,
                a.y + edge.y * parameter, a.z + edge.z * parameter };
            return ClampPointToBox(segment_point, center, half);
        }

}
