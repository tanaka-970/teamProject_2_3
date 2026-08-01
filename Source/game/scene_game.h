#pragma once

#include "camera.h"
#include "free_camera_controller.h"
#include "stage.h"

#include <DirectXMath.h>

// ゲーム側が持つ「カメラ」と「旧ステージ」だけを束ねる入れ物。
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
    // Stage は skinned_mesh (FBX, アニメーションなし) を使う。
    void Initialize(skinned_mesh* stage_mesh, float aspect);

    void Finalize() {}

    // 旧ステージの更新だけを行う。プレイヤーの更新はここには無い。
    void Update(float elapsed_time);

    void DrawCameraGUI();
    void DrawStageGUI();
    void ResetGameplay();
    void SetAspect(float aspect);
    void SetLegacyStageActive(bool active);

    // CameraTargetComponent の設定でカメラを追従させる。
    // 呼び出し側は GameObject のワールド位置と設定値だけを渡す。
    // カメラ側は操作対象の具象型を知らない。
    void FollowCameraTarget(const DirectX::XMFLOAT3& target_position,
        const DirectX::XMFLOAT3& look_at_offset,
        float distance, float height, float lag, float delta_time);

    // 追従対象が無いときのカメラ。自由移動コントローラーへ委ねる。
    // 「対象が居ないから旧 Player を追う」という経路は存在しない。
    void UpdateFreeCamera(float delta_time);

    Camera& GetCamera() { return camera; }
    Stage&  GetStage()  { return stage;  }

    const Camera& GetCamera() const { return camera; }
    const Stage&  GetStage()  const { return stage;  }

    // Camera-only rotation offsets (mouse right-drag / IJKL).
    float camera_yaw_offset    = 0.0f;
    float camera_pitch_offset  = 0.0f;

private:
    // カメラ回転入力（右ドラッグ / IJKL）。
    void UpdateCameraRotationInput(float delta_time);

    Camera               camera;
    FreeCameraController controller;
    Stage                stage;
};
