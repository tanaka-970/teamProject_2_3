#include "LocalShadowAtlas.h"

#include <algorithm>
#include <cmath>

using namespace DirectX;

namespace ReplayEngine::Rendering
{
    namespace
    {
        const XMFLOAT3 kFaceForward[LocalShadowAtlas::kPointFaceCount] = {
            {1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1} };
        const XMFLOAT3 kFaceUp[LocalShadowAtlas::kPointFaceCount] = {
            {0,1,0},{0,1,0},{0,0,-1},{0,0,1},{0,1,0},{0,1,0} };
    }

    bool LocalShadowAtlas::TryGetSlice(std::uint32_t slice, Slice& out) const noexcept
    {
        if (slice >= kSliceCount) return false;
        const bool spot_used = slice < next_spot_slice_;
        const bool point_used = slice >= kMaxSpotShadows && slice < next_point_base_;
        if (!spot_used && !point_used) return false;
        out = slices_[slice];
        return true;
    }

    void LocalShadowAtlas::BeginFrame() noexcept
    {
        next_spot_slice_ = 0;
        next_point_base_ = kMaxSpotShadows;
        for (Slice& slice : slices_)
        {
            // 使われないスライスは「範囲外＝遮蔽なし」になる行列を残す。
            slice = Slice{};
        }
    }

    int LocalShadowAtlas::AllocateSpotSlice() noexcept
    {
        // Spot は先頭の kMaxSpotShadows 枚から 1 枚ずつ取る。
        if (next_spot_slice_ >= kMaxSpotShadows) return -1;
        return static_cast<int>(next_spot_slice_++);
    }

    int LocalShadowAtlas::AllocatePointSlices() noexcept
    {
        // Point は kMaxSpotShadows 以降を 6 面ずつ取り、Spot の領域と重ねない。
        if (next_point_base_ + kPointFaceCount > kSliceCount) return -1;
        const uint32_t base = next_point_base_;
        next_point_base_ += kPointFaceCount;
        return static_cast<int>(base);
    }

    void LocalShadowAtlas::SetSlice(int slice, const XMFLOAT4X4& view_projection,
        float near_plane, float far_plane, float depth_bias) noexcept
    {
        if (slice < 0 || slice >= static_cast<int>(kSliceCount)) return;
        slices_[slice].view_projection = view_projection;
        slices_[slice].params = { near_plane, far_plane, depth_bias, 0.0f };
    }

    XMFLOAT4X4 LocalShadowAtlas::MakeSpotViewProjection(
        const XMFLOAT3& position, const XMFLOAT3& direction,
        float outer_angle_degrees, float near_plane, float far_plane) noexcept
    {
        XMVECTOR forward = XMVector3Normalize(XMLoadFloat3(&direction));
        if (XMVector3Equal(forward, XMVectorZero()))
            forward = XMVectorSet(0.0f, -1.0f, 0.0f, 0.0f);

        // 視線とほぼ平行な up を選ぶと LookAt が壊れるので切り替える。
        const float forward_y = XMVectorGetY(forward);
        const XMVECTOR up = std::fabs(forward_y) > 0.99f
            ? XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f)
            : XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

        const XMVECTOR eye = XMLoadFloat3(&position);
        const XMMATRIX view = XMMatrixLookToLH(eye, forward, up);

        // 外コーンの全角を FOV にする。縁のフィルタ分だけ余裕を持たせる。
        const float outer = (std::max)(1.0f, (std::min)(179.0f, outer_angle_degrees));
        const float fov = (std::min)(XM_PI * 0.98f,
            XMConvertToRadians(outer) * 2.0f * 1.1f);
        const XMMATRIX projection = XMMatrixPerspectiveFovLH(
            fov, 1.0f, (std::max)(near_plane, 0.01f),
            (std::max)(far_plane, near_plane + 0.1f));

        XMFLOAT4X4 result{};
        XMStoreFloat4x4(&result, view * projection);
        return result;
    }

    XMFLOAT4X4 LocalShadowAtlas::MakePointFaceViewProjection(
        const XMFLOAT3& position, int face,
        float near_plane, float far_plane) noexcept
    {
        const int index = (std::max)(0,
            (std::min)(static_cast<int>(kPointFaceCount) - 1, face));
        const XMVECTOR eye = XMLoadFloat3(&position);
        const XMMATRIX view = XMMatrixLookToLH(eye,
            XMLoadFloat3(&kFaceForward[index]), XMLoadFloat3(&kFaceUp[index]));
        // 立方体の 1 面なので FOV は必ず 90 度。変えると面の境目がずれる。
        const XMMATRIX projection = XMMatrixPerspectiveFovLH(
            XM_PIDIV2, 1.0f, (std::max)(near_plane, 0.01f),
            (std::max)(far_plane, near_plane + 0.1f));

        XMFLOAT4X4 result{};
        XMStoreFloat4x4(&result, view * projection);
        return result;
    }
}
