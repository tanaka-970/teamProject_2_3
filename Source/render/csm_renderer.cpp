#include "csm_renderer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

using namespace DirectX;

void csm_renderer::update_cascades(const XMFLOAT4& light_direction,
                                   const XMFLOAT4X4& view,
                                   const XMFLOAT4X4& projection,
                                   float scene_radius)
{
    const XMVECTOR light = XMVector3Normalize(XMLoadFloat4(&light_direction));
    const XMVECTOR up = std::fabs(XMVectorGetY(light)) > 0.99f
        ? XMVectorSet(0, 0, 1, 0)
        : XMVectorSet(0, 1, 0, 0);

    // 射影行列から実際のニア/ファーを取り出す。LH透視射影は
    // _33 = far/(far-near)、_43 = -near*far/(far-near)。
    // far の符号を落とすと負になり、分割計算の pow() が NaN を返して影が出なくなる。
    const float m33 = projection._33;
    const float m43 = projection._43;
    const float near_plane = (m33 != 0.0f) ? -m43 / m33 : 0.1f;
    float far_plane = ((m33 - 1.0f) != 0.0f) ? -m43 / (m33 - 1.0f) : 1000.0f;
    if (!(far_plane > near_plane)) far_plane = near_plane + 1000.0f;
    far_plane = (std::min)(far_plane, (std::max)(shadow_distance, near_plane + 1.0f));

    // 実用的分割スキーム(Practical Split Scheme)。対数分割と等間隔分割を
    // lambda で混ぜる。対数のみだと遠景が粗すぎ、等間隔のみだと近景が粗い。
    float splits[CASCADE_COUNT]{};
    const float range = far_plane - near_plane;
    const float ratio = far_plane / (std::max)(near_plane, 1.0e-3f);
    for (std::uint32_t c = 0; c < CASCADE_COUNT; ++c)
    {
        const float p = static_cast<float>(c + 1) / static_cast<float>(CASCADE_COUNT);
        const float logarithmic = near_plane * std::pow(ratio, p);
        const float uniform = near_plane + range * p;
        splits[c] = split_lambda * logarithmic + (1.0f - split_lambda) * uniform;
    }
    constants.split_distances = { splits[0], splits[1], splits[2], splits[3] };

    const XMMATRIX view_matrix = XMLoadFloat4x4(&view);
    const XMMATRIX projection_matrix = XMLoadFloat4x4(&projection);
    const XMMATRIX inverse_view_projection =
        XMMatrixInverse(nullptr, view_matrix * projection_matrix);

    // 全カスケードを包む球をこのループで育てる。影パスのキャスター選別に使う。
    XMStoreFloat3(&shadow_light_direction, light);
    bool shadow_volume_initialized = false;
    XMVECTOR shadow_volume_center_v = XMVectorZero();
    shadow_volume_radius = 0.0f;

    float previous_split = near_plane;
    for (std::uint32_t c = 0; c < CASCADE_COUNT; ++c)
    {
        const float split_near = previous_split;
        const float split_far = splits[c];
        previous_split = split_far;

        // 分割区間の視錐台8頂点をワールド空間で求める。NDCのzは
        // 射影行列に依存するので、深度は射影して求め直す。
        const float near_ndc_z = (split_near * m33 + m43) / (std::max)(split_near, 1.0e-4f);
        const float far_ndc_z = (split_far * m33 + m43) / (std::max)(split_far, 1.0e-4f);

        XMVECTOR corners[8];
        int corner_index = 0;
        for (int z = 0; z < 2; ++z)
        {
            const float ndc_z = z == 0 ? near_ndc_z : far_ndc_z;
            for (int y = 0; y < 2; ++y)
            {
                for (int x = 0; x < 2; ++x)
                {
                    const XMVECTOR ndc = XMVectorSet(
                        x == 0 ? -1.0f : 1.0f, y == 0 ? -1.0f : 1.0f, ndc_z, 1.0f);
                    XMVECTOR world = XMVector4Transform(ndc, inverse_view_projection);
                    world = XMVectorScale(world, 1.0f / (std::max)(XMVectorGetW(world), 1.0e-6f));
                    corners[corner_index++] = XMVectorSetW(world, 1.0f);
                }
            }
        }

        // 境界球で包む。回転に対して不変なので、カメラが回っても
        // 影の解像度が変わらず、ちらつきの原因を1つ潰せる。
        XMVECTOR center = XMVectorZero();
        for (const XMVECTOR& corner : corners) center = XMVectorAdd(center, corner);
        center = XMVectorScale(center, 1.0f / 8.0f);
        center = XMVectorSetW(center, 1.0f);

        float radius = 0.0f;
        for (const XMVECTOR& corner : corners)
        {
            const float distance = XMVectorGetX(XMVector3Length(
                XMVectorSubtract(corner, center)));
            radius = (std::max)(radius, distance);
        }
        radius = (std::max)(radius, 0.5f);
        // テクセル境界へ丸める都合上、半径も安定させておく。
        radius = std::ceil(radius * 16.0f) / 16.0f;

        // このカスケードの境界球を、全体を包む球へ併合する。
        if (!shadow_volume_initialized)
        {
            shadow_volume_center_v = center;
            shadow_volume_radius = radius;
            shadow_volume_initialized = true;
        }
        else
        {
            const XMVECTOR offset = XMVectorSubtract(center, shadow_volume_center_v);
            const float distance = XMVectorGetX(XMVector3Length(offset));
            if (distance + radius > shadow_volume_radius)
            {
                if (distance <= 1.0e-4f)
                {
                    shadow_volume_radius = (std::max)(shadow_volume_radius, radius);
                }
                else
                {
                    const float merged_radius =
                        (shadow_volume_radius + distance + radius) * 0.5f;
                    shadow_volume_center_v = XMVectorAdd(shadow_volume_center_v,
                        XMVectorScale(offset,
                            (merged_radius - shadow_volume_radius) / distance));
                    shadow_volume_radius = merged_radius;
                }
            }
        }

        const float diameter = radius * 2.0f;
        const float texels_per_unit = static_cast<float>(SHADOW_MAP_SIZE) / diameter;

        // ライト空間で中心をテクセル単位へスナップする。これが
        // シャドウシマリング(縁の毎フレームのちらつき)対策の本体。
        const XMMATRIX snap_view = XMMatrixLookAtLH(
            XMVectorZero(), light, up);
        XMVECTOR light_space_center = XMVector3TransformCoord(center, snap_view);
        light_space_center = XMVectorSet(
            std::floor(XMVectorGetX(light_space_center) * texels_per_unit) / texels_per_unit,
            std::floor(XMVectorGetY(light_space_center) * texels_per_unit) / texels_per_unit,
            XMVectorGetZ(light_space_center), 1.0f);
        center = XMVector3TransformCoord(light_space_center,
            XMMatrixInverse(nullptr, snap_view));

        const XMVECTOR eye = XMVectorSubtract(center,
            XMVectorScale(light, radius + caster_extrusion));
        const XMMATRIX cascade_view = XMMatrixLookAtLH(eye, center, up);
        // 画面外のキャスターも拾えるよう、ニアを手前へ、ファーを奥へ伸ばす。
        const XMMATRIX cascade_projection = XMMatrixOrthographicLH(
            diameter, diameter, 0.05f, radius * 2.0f + caster_extrusion * 2.0f);

        XMStoreFloat4x4(&constants.view_projection[c], cascade_view * cascade_projection);

        // 法線オフセットの基準となる1テクセルのワールド長。
        // 対角方向の最悪ケースを見込んで sqrt(2) を掛ける。
        const float texel_world_size = diameter / static_cast<float>(SHADOW_MAP_SIZE);
        reinterpret_cast<float*>(&constants.texel_world)[c] =
            texel_world_size * 1.41421356f;
    }

    XMStoreFloat3(&shadow_volume_center, shadow_volume_center_v);

    constants.params2.x = static_cast<float>(SHADOW_MAP_SIZE);
    (void)scene_radius; // 視錐台から自動で範囲を決めるため使用しない。
}
