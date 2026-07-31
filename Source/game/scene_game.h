#pragma once

#include "camera.h"
#include "free_camera_controller.h"
#include "player.h"
#include "stage.h"

class skinned_mesh;

class SceneGame
{
public:
    // Inject framework-owned meshes. Stage uses skinned_mesh (FBX, no animation).
    void Initialize(skinned_mesh* player_mesh,
                    skinned_mesh* stage_mesh,
                    float aspect);

    void Finalize() {}
    void Update(float elapsed_time);
    void DrawCameraGUI();
    void DrawPlayerGUI();
    void DrawStageGUI();
    void ResetGameplay();
    void SetAspect(float aspect);
    void SetAnimationClipMapping(int idle_clip, int walk_clip, int jump_clip);
    void ResetAnimationClipMapping();
    void SetLegacyStageActive(bool active);

    // 新 Player GameObject が操作対象になっている間は false を渡す。
    // 旧 Player の Update とカメラ追従を止め、二重更新・二重描画を防ぐ。
    void SetLegacyPlayerActive(bool active) noexcept { legacy_player_active_ = active; }
    bool LegacyPlayerActive() const noexcept { return legacy_player_active_; }

    // CameraTargetComponent の設定でカメラを追従させる。
    // 呼び出し側は GameObject の位置と設定値だけを渡す。
    // カメラ側は Player 具象型を知らない。
    void FollowCameraTarget(const DirectX::XMFLOAT3& target_position,
        const DirectX::XMFLOAT3& look_at_offset,
        float distance, float height, float lag, float delta_time);

    Camera& GetCamera() { return camera; }
    Player& GetPlayer() { return player; }
    Stage&  GetStage()  { return stage;  }

    const Camera& GetCamera() const { return camera; }
    const Player& GetPlayer() const { return player; }
    const Stage&  GetStage()  const { return stage;  }

    // Camera mode
    bool  follow_player        = true;
    float follow_distance      = 10.0f;
    float follow_height        = 3.0f;
    float follow_lag           = 12.0f;   // larger = snappier

    // Camera-only rotation offsets (mouse right-drag, do NOT affect player).
    float camera_yaw_offset    = 0.0f;
    float camera_pitch_offset  = 0.0f;

private:
    // カメラ回転入力（右ドラッグ / IJKL）。旧経路と新経路で共有する。
    void UpdateCameraRotationInput(float delta_time);

    Camera               camera;
    FreeCameraController controller;
    Player               player;
    Stage                stage;
    int default_idle_clip_ = -1;
    int default_walk_clip_ = -1;
    int default_jump_clip_ = -1;
    bool legacy_stage_active_ = false;
    bool legacy_player_active_ = true;
};
