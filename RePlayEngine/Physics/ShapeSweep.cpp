#include "ShapeSweep.h"

#include <algorithm>
#include <cmath>

using namespace DirectX;

namespace ReplayEngine::Physics
{
    namespace
    {
        struct Vector3
        {
            float x = 0.0f;
            float y = 0.0f;
            float z = 0.0f;
        };

        Vector3 Make(const XMFLOAT3& value) noexcept { return Vector3{ value.x, value.y, value.z }; }
        XMFLOAT3 ToFloat3(const Vector3& value) noexcept { return XMFLOAT3{ value.x, value.y, value.z }; }

        Vector3 Subtract(const Vector3& a, const Vector3& b) noexcept
        {
            return Vector3{ a.x - b.x, a.y - b.y, a.z - b.z };
        }
        Vector3 Add(const Vector3& a, const Vector3& b) noexcept
        {
            return Vector3{ a.x + b.x, a.y + b.y, a.z + b.z };
        }
        Vector3 Scale(const Vector3& a, float s) noexcept
        {
            return Vector3{ a.x * s, a.y * s, a.z * s };
        }
        float Dot(const Vector3& a, const Vector3& b) noexcept
        {
            return a.x * b.x + a.y * b.y + a.z * b.z;
        }
        float LengthSquared(const Vector3& a) noexcept { return Dot(a, a); }

        Vector3 Normalize(const Vector3& a, const Vector3& fallback) noexcept
        {
            const float length_squared = LengthSquared(a);
            if (length_squared <= 1.0e-12f) return fallback;
            const float inverse = 1.0f / std::sqrt(length_squared);
            return Scale(a, inverse);
        }

        // 線分上で点に最も近い位置のパラメータ（0〜1 に丸めたもの）。
        float ClosestParameter(const Vector3& point, const Vector3& a,
            const Vector3& segment, float segment_length_squared) noexcept
        {
            if (segment_length_squared <= 1.0e-12f) return 0.0f;
            const float raw = Dot(Subtract(point, a), segment) / segment_length_squared;
            return (std::max)(0.0f, (std::min)(1.0f, raw));
        }

        // a t^2 + b t + c = 0 の、[0, 1] に入る最小の解。無ければ false。
        bool SmallestRoot(float a, float b, float c, float& out) noexcept
        {
            if (std::fabs(a) <= 1.0e-12f)
            {
                // 一次式へ退化。
                if (std::fabs(b) <= 1.0e-12f) return false;
                const float root = -c / b;
                if (root < 0.0f || root > 1.0f) return false;
                out = root;
                return true;
            }

            const float discriminant = b * b - 4.0f * a * c;
            if (discriminant < 0.0f) return false;

            const float root_of_discriminant = std::sqrt(discriminant);
            const float inverse = 1.0f / (2.0f * a);
            const float first = (-b - root_of_discriminant) * inverse;
            const float second = (-b + root_of_discriminant) * inverse;

            const float low = (std::min)(first, second);
            const float high = (std::max)(first, second);

            if (low >= 0.0f && low <= 1.0f) { out = low; return true; }
            if (high >= 0.0f && high <= 1.0f) { out = high; return true; }
            return false;
        }
    }

    bool SweepSphereAgainstCapsule(const XMFLOAT3& start, const XMFLOAT3& end, float radius,
        const XMFLOAT3& segment_a, const XMFLOAT3& segment_b,
        float capsule_radius, SphereCastHit& hit) noexcept
    {
        hit = SphereCastHit{};

        const float combined = radius + capsule_radius;
        if (combined <= 0.0f) return false;

        const Vector3 origin = Make(start);
        const Vector3 motion = Subtract(Make(end), origin);
        const Vector3 a = Make(segment_a);
        const Vector3 segment = Subtract(Make(segment_b), a);
        const float segment_length_squared = LengthSquared(segment);

        const auto finish = [&](float fraction) noexcept
        {
            const Vector3 center = Add(origin, Scale(motion, fraction));
            const float parameter = ClosestParameter(center, a, segment, segment_length_squared);
            const Vector3 closest = Add(a, Scale(segment, parameter));

            // 法線は「カプセル表面から球中心へ向かう向き」。
            // 完全に中心が一致している場合だけ、真上を既定として返す。
            const Vector3 outward = Normalize(Subtract(center, closest), Vector3{ 0.0f, 1.0f, 0.0f });

            hit.center = ToFloat3(center);
            hit.normal = ToFloat3(outward);
            hit.position = ToFloat3(Add(closest, Scale(outward, capsule_radius)));
            hit.fraction = fraction;
            hit.distance = fraction * std::sqrt(LengthSquared(motion));
            return true;
        };

        // 開始時点で既に重なっているか。
        // 重なったまま「衝突なし」を返すと、押し戻しが働かず貼り付いてしまう。
        {
            const float parameter = ClosestParameter(origin, a, segment, segment_length_squared);
            const Vector3 closest = Add(a, Scale(segment, parameter));
            if (LengthSquared(Subtract(origin, closest)) <= combined * combined)
            {
                hit.started_overlapping = true;
                return finish(0.0f);
            }
        }

        float best = 2.0f;
        bool found = false;

        // ---- 側面（無限円柱として解き、線分の範囲内かを後で確かめる）----------
        if (segment_length_squared > 1.0e-12f)
        {
            const Vector3 direction = Scale(segment, 1.0f / std::sqrt(segment_length_squared));
            const Vector3 relative = Subtract(origin, a);

            // 軸方向の成分を抜いた 2 次元問題として解く。
            const Vector3 relative_perpendicular =
                Subtract(relative, Scale(direction, Dot(relative, direction)));
            const Vector3 motion_perpendicular =
                Subtract(motion, Scale(direction, Dot(motion, direction)));

            const float quadratic = LengthSquared(motion_perpendicular);
            const float linear = 2.0f * Dot(relative_perpendicular, motion_perpendicular);
            const float constant = LengthSquared(relative_perpendicular) - combined * combined;

            float root = 0.0f;
            if (SmallestRoot(quadratic, linear, constant, root))
            {
                // 側面で当たるのは、接触点が線分の内側にあるときだけ。
                // 外側なら端の半球側の解になるので、ここでは採らない。
                const Vector3 center = Add(origin, Scale(motion, root));
                const float raw = Dot(Subtract(center, a), segment) / segment_length_squared;
                if (raw >= 0.0f && raw <= 1.0f && root < best)
                {
                    best = root;
                    found = true;
                }
            }
        }

        // ---- 両端の半球 ------------------------------------------------------
        const Vector3 endpoints[2]{ a, Add(a, segment) };
        for (const Vector3& endpoint : endpoints)
        {
            const Vector3 relative = Subtract(origin, endpoint);
            const float quadratic = LengthSquared(motion);
            const float linear = 2.0f * Dot(relative, motion);
            const float constant = LengthSquared(relative) - combined * combined;

            float root = 0.0f;
            if (SmallestRoot(quadratic, linear, constant, root) && root < best)
            {
                best = root;
                found = true;
            }
        }

        if (!found) return false;
        return finish(best);
    }

    void BuildBoxTriangles(const XMFLOAT3& half_extents, Triangle* out) noexcept
    {
        if (out == nullptr) return;

        const float x = half_extents.x;
        const float y = half_extents.y;
        const float z = half_extents.z;

        // 8 頂点。添字は (x, y, z) の符号の組み合わせ。
        const XMFLOAT3 corners[8]{
            { -x, -y, -z }, {  x, -y, -z }, {  x,  y, -z }, { -x,  y, -z },
            { -x, -y,  z }, {  x, -y,  z }, {  x,  y,  z }, { -x,  y,  z },
        };

        // 各面 2 枚 × 6 面。巻き順は外向きで揃えてある。
        static const int indices[box_triangle_count][3]{
            { 0, 2, 1 }, { 0, 3, 2 },   // -Z
            { 4, 5, 6 }, { 4, 6, 7 },   // +Z
            { 0, 4, 7 }, { 0, 7, 3 },   // -X
            { 1, 2, 6 }, { 1, 6, 5 },   // +X
            { 0, 1, 5 }, { 0, 5, 4 },   // -Y
            { 3, 7, 6 }, { 3, 6, 2 },   // +Y
        };

        for (std::size_t triangle = 0; triangle < box_triangle_count; ++triangle)
        {
            for (int vertex = 0; vertex < 3; ++vertex)
            {
                out[triangle].vertices[vertex] = corners[indices[triangle][vertex]];
            }
            out[triangle].material_index = -1;
        }
    }
}
