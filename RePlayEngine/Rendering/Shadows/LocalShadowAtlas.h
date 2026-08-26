#pragma once

#include <DirectXMath.h>
#include <cstdint>

namespace ReplayEngine::Rendering
{
    class LocalShadowAtlas final
    {
    public:
        struct Slice
        {
            DirectX::XMFLOAT4X4 view_projection{
                1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
            DirectX::XMFLOAT4 params{ 0.1f, 50.0f, 0.0015f, 0.0f };
        };

        static constexpr std::uint32_t kMaxSpotShadows = 4;
        static constexpr std::uint32_t kMaxPointShadows = 2;
        static constexpr std::uint32_t kPointFaceCount = 6;
        static constexpr std::uint32_t kSliceCount = kMaxSpotShadows + kMaxPointShadows * kPointFaceCount;
        static constexpr std::uint32_t kDefaultResolution = 1024;

        void BeginFrame() noexcept;
        int AllocateSpotSlice() noexcept;
        int AllocatePointSlices() noexcept;
        void SetSlice(int slice, const DirectX::XMFLOAT4X4& view_projection,
            float near_plane, float far_plane, float depth_bias) noexcept;
        std::uint32_t UsedSliceCount() const noexcept
        {
            return next_spot_slice_ + (next_point_base_ - kMaxSpotShadows);
        }
        bool AtlasReady() const noexcept { return UsedSliceCount() != 0; }
        std::uint32_t Resolution() const noexcept { return resolution_setting; }
        bool TryGetSlice(std::uint32_t slice, Slice& out) const noexcept;

        static DirectX::XMFLOAT4X4 MakeSpotViewProjection(
            const DirectX::XMFLOAT3& position, const DirectX::XMFLOAT3& direction,
            float outer_angle_degrees, float near_plane, float far_plane) noexcept;
        static DirectX::XMFLOAT4X4 MakePointFaceViewProjection(
            const DirectX::XMFLOAT3& position, int face,
            float near_plane, float far_plane) noexcept;

        bool enabled = true;
        std::uint32_t resolution_setting = kDefaultResolution;

    private:
        Slice slices_[kSliceCount]{};
        std::uint32_t next_spot_slice_ = 0;
        std::uint32_t next_point_base_ = kMaxSpotShadows;
    };
}
