#include "scene_game.h"
#include "game_input.h"

#include <algorithm>
#include <cmath>

#ifdef USE_IMGUI
#include "imgui/imgui.h"
#endif

namespace
{
    constexpr float kDefaultFieldOfViewDegrees = 50.0f;
    constexpr float kDefaultNearClip = 0.1f;
    constexpr float kDefaultFarClip = 10000.0f;

    float ClampFieldOfView(float degrees) noexcept
    {
        if (!std::isfinite(degrees)) return kDefaultFieldOfViewDegrees;
        return (std::max)(1.0f, (std::min)(179.0f, degrees));
    }

    float ClampNearClip(float near_clip) noexcept
    {
        if (!std::isfinite(near_clip)) return kDefaultNearClip;
        return (std::max)(1.0e-3f, near_clip);
    }

    float ClampFarClip(float far_clip, float near_clip) noexcept
    {
        if (!std::isfinite(far_clip)) return (std::max)(kDefaultFarClip, near_clip * 10.0f);
        return (std::max)(far_clip, near_clip * 10.0f);
    }
}

void SceneGame::Initialize(float aspect)
{
    if (aspect > 0.0f) aspect_ = aspect;
    ApplyDefaultCameraSettings();
    ResetGameplay();
}

void SceneGame::ResetGameplay()
{
    camera_yaw_offset = 0.0f;
    camera_pitch_offset = 0.0f;
    ApplyDefaultCameraSettings();

    camera.SetLookAt({ 0.0f, 2.25f, -6.5f }, { 0.0f, 1.0f, 0.0f }, { 0, 1, 0 });
    controller.SyncCameraToController(camera);
}

void SceneGame::SetAspect(float aspect)
{
    if (aspect > 0.0f)
    {
        aspect_ = aspect;
        RefreshProjection();
    }
}

void SceneGame::ApplyCameraSettings(float field_of_view_degrees, float near_clip, float far_clip)
{
    camera_field_of_view_degrees_ = ClampFieldOfView(field_of_view_degrees);
    camera_near_clip_ = ClampNearClip(near_clip);
    camera_far_clip_ = ClampFarClip(far_clip, camera_near_clip_);
    RefreshProjection();
}

void SceneGame::ApplyDefaultCameraSettings()
{
    ApplyCameraSettings(kDefaultFieldOfViewDegrees, kDefaultNearClip, kDefaultFarClip);
}

void SceneGame::RefreshProjection()
{
    camera.SetPerspectiveFov(DirectX::XMConvertToRadians(camera_field_of_view_degrees_),
        aspect_, camera_near_clip_, camera_far_clip_);
}

void SceneGame::FollowCameraTarget(const DirectX::XMFLOAT3& target_position,
    const DirectX::XMFLOAT3& look_at_offset,
    float distance, float height, float lag,
    float field_of_view_degrees, float near_clip, float far_clip,
    float delta_time, const GameInput::InputState& input)
{
    ApplyCameraSettings(field_of_view_degrees, near_clip, far_clip);

    // カメラの回転入力はここで受ける。
    // 追従対象の位置と設定値だけを外から受け取り、対象の具象型には触れない。
    UpdateCameraRotationInput(delta_time, input);

    const DirectX::XMFLOAT3 focus{
        target_position.x + look_at_offset.x,
        target_position.y + look_at_offset.y,
        target_position.z + look_at_offset.z };

    const float yaw = camera_yaw_offset;
    const float pitch = camera_pitch_offset;
    const float cp = cosf(pitch);
    const DirectX::XMFLOAT3 desired{
        focus.x - sinf(yaw) * cp * distance,
        focus.y + height + sinf(pitch) * distance,
        focus.z - cosf(yaw) * cp * distance
    };

    const auto& current_eye = camera.GetEye();
    const float t = 1.0f - expf(-lag * delta_time);
    const DirectX::XMFLOAT3 new_eye{
        current_eye.x + (desired.x - current_eye.x) * t,
        current_eye.y + (desired.y - current_eye.y) * t,
        current_eye.z + (desired.z - current_eye.z) * t
    };
    camera.SetLookAt(new_eye, focus, { 0, 1, 0 });

    // 自由移動コントローラーの内部状態を今のカメラへ合わせておく。
    // 追従対象が消えて自由カメラへ切り替わった瞬間に、
    // カメラが古い位置へ飛ぶのを防ぐ。
    controller.SyncCameraToController(camera);
}

void SceneGame::UpdateFreeCamera(float delta_time, const GameInput::InputState& input)
{
    // 追従対象が居ない場合のカメラ。
    // 「対象が居ないから何かを追う」という代替経路は持たない。
    ApplyDefaultCameraSettings();
    controller.Update(delta_time, input);
    controller.SyncControllerToCamera(camera);
}

void SceneGame::UpdateCameraRotationInput(float dt, const GameInput::InputState& input)
{
    if (input.Held("CameraRotate"))
    {
        const float k = 0.006f;
        camera_yaw_offset += input.PointerDeltaX() * k;
        camera_pitch_offset += -input.PointerDeltaY() * k;
    }

    const float key_rotate_speed = 1.8f * dt;
    if (input.Held("CameraYawLeft")) camera_yaw_offset -= key_rotate_speed;
    if (input.Held("CameraYawRight")) camera_yaw_offset += key_rotate_speed;
    if (input.Held("CameraPitchUp")) camera_pitch_offset += key_rotate_speed;
    if (input.Held("CameraPitchDown")) camera_pitch_offset -= key_rotate_speed;

    const float lim = 1.40f;
    if (camera_pitch_offset > lim) camera_pitch_offset = lim;
    if (camera_pitch_offset < -lim) camera_pitch_offset = -lim;
}

void SceneGame::DrawCameraGUI()
{
#ifdef USE_IMGUI
    ImGui::TextDisabled("追従対象は CameraTargetComponent が決めます");
    ImGui::SliderFloat("水平回転オフセット", &camera_yaw_offset, -3.14f, 3.14f);
    ImGui::SliderFloat("垂直回転オフセット", &camera_pitch_offset, -1.40f, 1.40f);
#endif
}

