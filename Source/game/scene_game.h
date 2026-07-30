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
    Camera               camera;
    FreeCameraController controller;
    Player               player;
    Stage                stage;
    int default_idle_clip_ = -1;
    int default_walk_clip_ = -1;
    int default_jump_clip_ = -1;
    bool legacy_stage_active_ = false;
};
