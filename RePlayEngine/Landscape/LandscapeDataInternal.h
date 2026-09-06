#pragma once

// これは LandscapeData の分割内部で共有する実装であり、外部から使うものではない。

#include <DirectXMath.h>

#include <cmath>
#include <algorithm>

namespace ReplayEngine::Landscape::Detail
{
    using namespace DirectX;

    inline constexpr float epsilon = 1.0e-6f;

    inline XMFLOAT3 Add(const XMFLOAT3& a, const XMFLOAT3& b) noexcept
        { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
    inline XMFLOAT3 Sub(const XMFLOAT3& a, const XMFLOAT3& b) noexcept
        { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
    inline XMFLOAT3 Mul(const XMFLOAT3& a, float s) noexcept
        { return { a.x * s, a.y * s, a.z * s }; }
    inline float Dot(const XMFLOAT3& a, const XMFLOAT3& b) noexcept
        { return a.x * b.x + a.y * b.y + a.z * b.z; }
    inline XMFLOAT3 Cross(const XMFLOAT3& a, const XMFLOAT3& b) noexcept
        {
            return { a.y * b.z - a.z * b.y,
                a.z * b.x - a.x * b.z,
                a.x * b.y - a.y * b.x };
        }
    inline XMFLOAT3 Normalize(const XMFLOAT3& value) noexcept
        {
            const float scale = (std::max)({std::fabs(value.x),std::fabs(value.y),std::fabs(value.z)});
            if (!(scale>0) || !std::isfinite(scale)) return {0,1,0};
            const XMFLOAT3 scaled{value.x/scale,value.y/scale,value.z/scale};
            return Mul(scaled,1.0f/std::sqrt(Dot(scaled,scaled)));
        }
    inline bool Finite3(const XMFLOAT3& v) noexcept
        { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
}
