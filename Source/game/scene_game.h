#pragma once

#include "camera.h"
#include "free_camera_controller.h"

#include <DirectXMath.h>

namespace GameInput { class InputState; }

// ゲーム側が持つカメラ操作を束ねる入れ物。
//
// 【プレイヤーは持たない】
//   以前はここが Player の実体を値メンバとして所有し、Update / 接地解決 /
//   カメラ追従までを行っていた。その経路は完全に撤去した。
//   操作対象は Scene 内の通常の GameObject であり、その更新も描画も
//   Component 側（PlayerInput / PlayerController / CharacterMotor /
//   SkinnedMeshRenderer / Animator）が担当する。
//
//   このクラスは操作対象の具象型を一切知らない。
//   カメラを動かすときも、外から「追従点」と「設定値」を渡してもらうだけで、
//   誰を追っているのかは知らない。
class SceneGame
{
public:
    void Initialize(float aspect);

    void Finalize() {}

    void DrawCameraGUI();
    void ResetGameplay();
    void SetAspect(float aspect);
    void ApplyCameraSettings(float field_of_view_degrees, float near_clip, float far_clip);

    // CameraTargetComponent の設定でカメラを追従させる。
    // 呼び出し側は GameObject のワールド位置と設定値だけを渡す。
    // カメラ側は操作対象の具象型を知らない。
    void FollowCameraTarget(const DirectX::XMFLOAT3& target_position,
        const DirectX::XMFLOAT3& look_at_offset,
        float distance, float height, float lag,
        float field_of_view_degrees, float near_clip, float far_clip,
        float delta_time, const GameInput::InputState& input);

    // 追従対象が無いときのカメラ。自由移動コントローラーへ委ねる。
    // 「対象が居ないから旧 Player を追う」という経路は存在しない。
    void UpdateFreeCamera(float delta_time, const GameInput::InputState& input);

    Camera& GetCamera() { return camera; }

    const Camera& GetCamera() const { return camera; }

    // Camera-only rotation offsets (mouse right-drag / IJKL).
    float camera_yaw_offset    = 0.0f;
    float camera_pitch_offset  = 0.0f;

private:
    // カメラ回転入力（右ドラッグ / IJKL）。
    void UpdateCameraRotationInput(float delta_time, const GameInput::InputState& input);
    void ApplyDefaultCameraSettings();
    void RefreshProjection();

    Camera               camera;
    FreeCameraController controller;
    float aspect_ = 16.0f / 9.0f;
    float camera_field_of_view_degrees_ = 50.0f;
    float camera_near_clip_ = 0.1f;
    float camera_far_clip_ = 10000.0f;
};
