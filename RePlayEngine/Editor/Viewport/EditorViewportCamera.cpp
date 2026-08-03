#include "EditorViewportCamera.h"

#include <algorithm>
#include <cmath>

namespace ReplayEngine::Editor
{
    using DirectX::XMFLOAT3;
    using DirectX::XMMATRIX;
    using DirectX::XMVECTOR;

    namespace
    {
        constexpr float two_pi = 6.2831853071795864f;

        XMFLOAT3 Add(const XMFLOAT3& a, const XMFLOAT3& b) noexcept
        {
            return XMFLOAT3{ a.x + b.x, a.y + b.y, a.z + b.z };
        }

        XMFLOAT3 Scale(const XMFLOAT3& v, float s) noexcept
        {
            return XMFLOAT3{ v.x * s, v.y * s, v.z * s };
        }

        // yaw を -pi〜+pi へ畳む。連続回転しても値が際限なく増えないようにする。
        float WrapAngle(float radians) noexcept
        {
            while (radians > DirectX::XM_PI) radians -= two_pi;
            while (radians < -DirectX::XM_PI) radians += two_pi;
            return radians;
        }
    }

    EditorViewportCamera::EditorViewportCamera()
    {
        ResetToDefault();
    }

    void EditorViewportCamera::RecalculateBasis() noexcept
    {
        // ここが Forward / Right / Up を作る唯一の場所。
        // 左手系・Y が上。yaw は Y 軸回り、pitch は Right 軸回り。
        const float cos_pitch = std::cos(pitch_);
        const float sin_pitch = std::sin(pitch_);
        const float cos_yaw = std::cos(yaw_);
        const float sin_yaw = std::sin(yaw_);

        forward_ = XMFLOAT3{
            cos_pitch * sin_yaw,
            sin_pitch,
            cos_pitch * cos_yaw };

        // Right はワールドの上方向と Forward の外積。
        // pitch を ±89 度で止めているので、両者が平行になって縮退することはない。
        right_ = XMFLOAT3{ cos_yaw, 0.0f, -sin_yaw };

        // Up = Forward × Right（左手系）。
        up_ = XMFLOAT3{
            forward_.y * right_.z - forward_.z * right_.y,
            forward_.z * right_.x - forward_.x * right_.z,
            forward_.x * right_.y - forward_.y * right_.x };
    }

    void EditorViewportCamera::SetYawPitch(float yaw, float pitch) noexcept
    {
        yaw_ = WrapAngle(yaw);
        // 上下反転を防ぐ。真上・真下を越えさせない。
        pitch_ = std::clamp(pitch, -pitch_limit, pitch_limit);
        RecalculateBasis();
    }

    void EditorViewportCamera::SetOrbitPivot(const XMFLOAT3& pivot) noexcept
    {
        orbit_pivot_ = pivot;

        const XMFLOAT3 to_pivot{
            pivot.x - position_.x, pivot.y - position_.y, pivot.z - position_.z };
        const float distance = std::sqrt(
            to_pivot.x * to_pivot.x + to_pivot.y * to_pivot.y + to_pivot.z * to_pivot.z);
        orbit_distance_ = std::clamp(distance,
            minimum_orbit_distance, maximum_orbit_distance);
    }

    void EditorViewportCamera::SetOrbitPivotToViewCenter() noexcept
    {
        // 選択が無いときの既定 Pivot。カメラ前方の現在距離の点を使う。
        orbit_pivot_ = Add(position_, Scale(forward_, orbit_distance_));
    }

    void EditorViewportCamera::Fly(const MoveAxes& axes, float speed_multiplier,
        float delta_time) noexcept
    {
        if (delta_time <= 0.0f) return;

        const float speed = move_speed * speed_multiplier * delta_time;

        // ローカル軸を使う。Up はワールドの上ではなくカメラの Up。
        XMFLOAT3 movement{ 0.0f, 0.0f, 0.0f };
        movement = Add(movement, Scale(forward_, axes.forward * speed));
        movement = Add(movement, Scale(right_, axes.right * speed));
        movement = Add(movement, Scale(up_, axes.up * speed));

        position_ = Add(position_, movement);

        // 飛んだぶん Pivot も一緒に動かす。
        // そうしないと、移動後に Orbit した瞬間に画面が大きく飛ぶ。
        orbit_pivot_ = Add(orbit_pivot_, movement);
    }

    void EditorViewportCamera::Look(float mouse_delta_x, float mouse_delta_y) noexcept
    {
        constexpr float radians_per_pixel = 0.01f;
        SetYawPitch(
            yaw_ + mouse_delta_x * mouse_sensitivity * radians_per_pixel,
            pitch_ - mouse_delta_y * mouse_sensitivity * radians_per_pixel);

        // 視点だけ回したときは Pivot も前方へ付いていく。
        SetOrbitPivotToViewCenter();
    }

    void EditorViewportCamera::Pan(float mouse_delta_x, float mouse_delta_y) noexcept
    {
        // 距離に比例させる。近くを見ているときは細かく、
        // 遠くを見ているときは大きく動く。極端に速くなりすぎないよう上限も掛ける。
        const float distance = std::clamp(orbit_distance_, 0.5f, 500.0f);
        constexpr float world_per_pixel = 0.0015f;
        const float scale = distance * pan_sensitivity * world_per_pixel;

        XMFLOAT3 movement{ 0.0f, 0.0f, 0.0f };
        movement = Add(movement, Scale(right_, -mouse_delta_x * scale));
        movement = Add(movement, Scale(up_, mouse_delta_y * scale));

        position_ = Add(position_, movement);
        orbit_pivot_ = Add(orbit_pivot_, movement);
    }

    void EditorViewportCamera::Orbit(float mouse_delta_x, float mouse_delta_y) noexcept
    {
        constexpr float radians_per_pixel = 0.01f;

        // 角度だけを変え、距離は変えない。
        const float new_yaw = yaw_ + mouse_delta_x * mouse_sensitivity * radians_per_pixel;
        const float new_pitch = pitch_ - mouse_delta_y * mouse_sensitivity * radians_per_pixel;
        SetYawPitch(new_yaw, new_pitch);

        // Pivot から distance だけ手前へ戻した位置がカメラ位置。
        // これで距離が必ず保たれる。
        position_ = XMFLOAT3{
            orbit_pivot_.x - forward_.x * orbit_distance_,
            orbit_pivot_.y - forward_.y * orbit_distance_,
            orbit_pivot_.z - forward_.z * orbit_distance_ };
    }

    void EditorViewportCamera::Zoom(float wheel_delta) noexcept
    {
        if (wheel_delta == 0.0f) return;

        // 距離に比例した倍率で寄る。遠いときは大きく、近いときは細かく。
        // 減算ではなく乗算にすることで、距離が 0 や負になることが構造的に起きない。
        const float factor = std::pow(0.9f, wheel_delta * zoom_sensitivity);
        orbit_distance_ = std::clamp(orbit_distance_ * factor,
            minimum_orbit_distance, maximum_orbit_distance);

        position_ = XMFLOAT3{
            orbit_pivot_.x - forward_.x * orbit_distance_,
            orbit_pivot_.y - forward_.y * orbit_distance_,
            orbit_pivot_.z - forward_.z * orbit_distance_ };
    }

    void EditorViewportCamera::FocusOnBounds(const XMFLOAT3& minimum,
        const XMFLOAT3& maximum) noexcept
    {
        const XMFLOAT3 center{
            (minimum.x + maximum.x) * 0.5f,
            (minimum.y + maximum.y) * 0.5f,
            (minimum.z + maximum.z) * 0.5f };

        const XMFLOAT3 extents{
            (maximum.x - minimum.x) * 0.5f,
            (maximum.y - minimum.y) * 0.5f,
            (maximum.z - minimum.z) * 0.5f };

        // 外接球の半径。どの向きから見ても収まる基準になる。
        float radius = std::sqrt(
            extents.x * extents.x + extents.y * extents.y + extents.z * extents.z);
        if (!(radius > 0.0f)) radius = 0.5f;   // 大きさゼロの対象向けの下駄

        // 半径 radius の球が画角へ収まる距離。少し余白を持たせる。
        const float half_fov = DirectX::XMConvertToRadians(field_of_view_degrees) * 0.5f;
        const float tangent = std::tan(half_fov);
        float distance = (tangent > 1.0e-4f) ? (radius / tangent) : radius * 2.0f;
        distance *= 1.4f;

        // 極端な対象への保険。
        //   小さすぎる → 近づきすぎて near plane で消える
        //   大きすぎる → 遠くへ飛びすぎて何も見えない
        distance = std::clamp(distance, minimum_focus_distance, maximum_focus_distance);

        // near / far の内側に収める。far を越えると対象ごと消える。
        distance = std::min(distance, far_clip * 0.5f);
        distance = std::max(distance, near_clip * 4.0f);

        orbit_pivot_ = center;
        orbit_distance_ = std::clamp(distance,
            minimum_orbit_distance, maximum_orbit_distance);

        // 今の向きを保ったまま、中心が画面に来る位置へ下がる。
        // 視点の向きを毎回リセットすると、フォーカスのたびに方向感覚が失われる。
        position_ = XMFLOAT3{
            center.x - forward_.x * orbit_distance_,
            center.y - forward_.y * orbit_distance_,
            center.z - forward_.z * orbit_distance_ };
    }

    void EditorViewportCamera::FocusOnPoint(const XMFLOAT3& point) noexcept
    {
        // Bounds が取れない対象。点の周りに小さな箱があるものとして扱う。
        const XMFLOAT3 minimum{ point.x - 0.5f, point.y - 0.5f, point.z - 0.5f };
        const XMFLOAT3 maximum{ point.x + 0.5f, point.y + 0.5f, point.z + 0.5f };
        FocusOnBounds(minimum, maximum);
    }

    void EditorViewportCamera::LookAt(const XMFLOAT3& eye, const XMFLOAT3& target) noexcept
    {
        position_ = eye;

        const XMFLOAT3 to_target{
            target.x - eye.x, target.y - eye.y, target.z - eye.z };
        const float distance = std::sqrt(
            to_target.x * to_target.x + to_target.y * to_target.y + to_target.z * to_target.z);

        if (distance > 1.0e-4f)
        {
            const float horizontal = std::sqrt(
                to_target.x * to_target.x + to_target.z * to_target.z);
            SetYawPitch(std::atan2(to_target.x, to_target.z),
                std::atan2(to_target.y, horizontal));
        }
        else
        {
            RecalculateBasis();
        }

        orbit_pivot_ = target;
        orbit_distance_ = std::clamp(distance > 1.0e-4f ? distance : 1.0f,
            minimum_orbit_distance, maximum_orbit_distance);
    }

    XMMATRIX EditorViewportCamera::ViewMatrix() const noexcept
    {
        const XMVECTOR eye = DirectX::XMVectorSet(
            position_.x, position_.y, position_.z, 1.0f);
        const XMVECTOR focus = DirectX::XMVectorSet(
            position_.x + forward_.x,
            position_.y + forward_.y,
            position_.z + forward_.z, 1.0f);
        const XMVECTOR up = DirectX::XMVectorSet(up_.x, up_.y, up_.z, 0.0f);

        // 既存エンジンと同じ左手系。
        return DirectX::XMMatrixLookAtLH(eye, focus, up);
    }

    XMMATRIX EditorViewportCamera::ProjectionMatrix(float aspect) const noexcept
    {
        const float safe_aspect = (aspect > 1.0e-4f) ? aspect : (16.0f / 9.0f);
        const float fov = DirectX::XMConvertToRadians(
            std::clamp(field_of_view_degrees, 1.0f, 179.0f));
        const float near_plane = std::max(near_clip, 1.0e-3f);
        const float far_plane = std::max(far_clip, near_plane * 10.0f);
        return DirectX::XMMatrixPerspectiveFovLH(fov, safe_aspect, near_plane, far_plane);
    }

    EditorViewportCamera::Ray EditorViewportCamera::BuildPickingRay(
        float mouse_x, float mouse_y,
        float viewport_width, float viewport_height) const noexcept
    {
        Ray ray;
        ray.origin = position_;
        ray.direction = forward_;

        if (viewport_width <= 0.0f || viewport_height <= 0.0f) return ray;

        // 画面座標 -> NDC。
        const float ndc_x = (mouse_x / viewport_width) * 2.0f - 1.0f;
        const float ndc_y = 1.0f - (mouse_y / viewport_height) * 2.0f;

        // 描画に使うのと同じ行列から作る。
        // ここで別の行列を組み立てると「見えている位置」と「拾える位置」がずれる。
        const XMMATRIX projection = ProjectionMatrix(viewport_width / viewport_height);
        DirectX::XMFLOAT4X4 projection_values{};
        DirectX::XMStoreFloat4x4(&projection_values, projection);

        // 透視投影の _11 / _22 で NDC をビュー空間の傾きへ戻す。
        const float view_x = (projection_values._11 != 0.0f)
            ? ndc_x / projection_values._11 : ndc_x;
        const float view_y = (projection_values._22 != 0.0f)
            ? ndc_y / projection_values._22 : ndc_y;

        // ビュー空間の方向をワールドへ。基底は View と同じものを使う。
        const XMFLOAT3 direction{
            forward_.x + right_.x * view_x + up_.x * view_y,
            forward_.y + right_.y * view_x + up_.y * view_y,
            forward_.z + right_.z * view_x + up_.z * view_y };

        const float length = std::sqrt(
            direction.x * direction.x + direction.y * direction.y + direction.z * direction.z);
        if (length > 1.0e-6f)
        {
            ray.direction = XMFLOAT3{
                direction.x / length, direction.y / length, direction.z / length };
        }
        return ray;
    }

    void EditorViewportCamera::ResetSettingsToDefault() noexcept
    {
        move_speed = 5.0f;
        fast_multiplier = 4.0f;
        slow_multiplier = 0.25f;
        mouse_sensitivity = 0.15f;
        pan_sensitivity = 1.0f;
        zoom_sensitivity = 1.0f;
        field_of_view_degrees = 60.0f;
        near_clip = 0.05f;
        far_clip = 10000.0f;
    }

    void EditorViewportCamera::ResetToDefault() noexcept
    {
        ResetSettingsToDefault();

        // 新規 Scene の既定位置。原点付近を見下ろす無難な視点。
        LookAt(XMFLOAT3{ 0.0f, 3.0f, -8.0f }, XMFLOAT3{ 0.0f, 1.0f, 0.0f });
    }
}
