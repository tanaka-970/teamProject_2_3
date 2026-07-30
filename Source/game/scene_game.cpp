#include "scene_game.h"
#include "game_input.h"
#include "raycast.h"
#include "skinned_mesh.h"

#include <cmath>

#ifdef USE_IMGUI
#include "imgui/imgui.h"
#endif

namespace
{
    void UpdatePlayerGroundFromStage(Player& player, const Stage& stage)
    {
        GameRaycast::SphereCastHit hit{};
        const DirectX::XMFLOAT3 position = player.GetPosition();
        const auto& collider = player.Collider();
        if (collider.Enabled() && GameRaycast::SphereCastStageDown(stage, position,
            collider.radius, 80.0f, 300.0f, hit, collider.walkable_normal_y))
        {
            player.SnapToGround(hit.position.y);
        }
        else
        {
            player.SetGroundY(0.0f);
        }
    }

#ifdef USE_IMGUI
    bool DrawClipCombo(const char* label, int& clip_index, const skinned_mesh* mesh)
    {
        const char* preview = "Manual / None";
        if (mesh && clip_index >= 0 && clip_index < static_cast<int>(mesh->animation_clips.size()))
        {
            preview = mesh->animation_clips[clip_index].name.empty()
                ? "(unnamed)"
                : mesh->animation_clips[clip_index].name.c_str();
        }

        bool changed = false;
        if (ImGui::BeginCombo(label, preview))
        {
            const bool none_selected = clip_index < 0;
            if (ImGui::Selectable("Manual / None", none_selected))
            {
                clip_index = -1;
                changed = true;
            }
            if (none_selected)
            {
                ImGui::SetItemDefaultFocus();
            }

            if (mesh)
            {
                for (int i = 0; i < static_cast<int>(mesh->animation_clips.size()); ++i)
                {
                    const bool selected = clip_index == i;
                    const char* name = mesh->animation_clips[i].name.empty()
                        ? "(unnamed)"
                        : mesh->animation_clips[i].name.c_str();
                    ImGui::PushID(i);
                    if (ImGui::Selectable(name, selected))
                    {
                        clip_index = i;
                        changed = true;
                    }
                    if (selected)
                    {
                        ImGui::SetItemDefaultFocus();
                    }
                    ImGui::PopID();
                }
            }
            ImGui::EndCombo();
        }
        return changed;
    }
#endif
}

void SceneGame::Initialize(skinned_mesh* player_mesh,
                           skinned_mesh* stage_mesh,
                           float aspect)
{
    camera.SetPerspectiveFov(DirectX::XMConvertToRadians(50.0f),
                             aspect, 0.1f, 10000.0f);
    player.SetSkinnedMesh(player_mesh);
    stage.SetModel(stage_mesh);
    ResetGameplay();
}

void SceneGame::ResetGameplay()
{
    follow_player = true;
    follow_distance = 6.5f;
    follow_height = 2.25f;
    follow_lag = 12.0f;
    camera_yaw_offset = 0.0f;
    camera_pitch_offset = 0.0f;

    player.ResetPlacement();
    stage.ResetTransform();

    camera.SetLookAt({ 0.0f, 2.25f, -6.5f }, { 0.0f, 1.0f, 0.0f }, { 0, 1, 0 });
    controller.SyncCameraToController(camera);
}

void SceneGame::SetAspect(float aspect)
{
    if (aspect > 0.0f)
    {
        camera.SetPerspectiveFov(DirectX::XMConvertToRadians(50.0f), aspect, 0.1f, 10000.0f);
    }
}

void SceneGame::SetAnimationClipMapping(int idle_clip, int walk_clip, int jump_clip)
{
    default_idle_clip_ = idle_clip;
    default_walk_clip_ = walk_clip;
    default_jump_clip_ = jump_clip;
    ResetAnimationClipMapping();
}

void SceneGame::ResetAnimationClipMapping()
{
    player.clip_idle = default_idle_clip_;
    player.clip_walk = default_walk_clip_;
    player.clip_jump = default_jump_clip_;
}

void SceneGame::SetLegacyStageActive(bool active)
{
    legacy_stage_active_ = active;
    stage.GetCollisionMesh().SetEnabled(active);
}

void SceneGame::Update(float dt)
{
    // Player movement uses the previous camera orientation, then the camera
    // follows the updated player position. This matches the GP3 sample flow.
    if (legacy_stage_active_) UpdatePlayerGroundFromStage(player, stage);
    const DirectX::XMFLOAT3 previous_position = player.GetPosition();
    player.Update(dt, camera);

    // Swept-sphere movement prevents the player's center point from tunneling
    // through walls. Ground-like faces are handled by the downward cast below.
    const auto& collider = player.Collider();
    if (legacy_stage_active_ && collider.Enabled())
    {
        const DirectX::XMFLOAT3 current_position = player.GetPosition();
        const DirectX::XMFLOAT3 start{
            previous_position.x + collider.center_offset.x,
            previous_position.y + collider.center_offset.y,
            previous_position.z + collider.center_offset.z };
        const DirectX::XMFLOAT3 end{
            current_position.x + collider.center_offset.x,
            current_position.y + collider.center_offset.y,
            current_position.z + collider.center_offset.z };
        GameRaycast::SphereCastHit movement_hit{};
        if (GameRaycast::SphereCastStage(stage, start, end, collider.radius,
            movement_hit, -1.0f, collider.walkable_normal_y - 0.001f))
        {
            const DirectX::XMFLOAT3 resolved_center{
                movement_hit.center.x + movement_hit.normal.x * collider.skin_width,
                movement_hit.center.y + movement_hit.normal.y * collider.skin_width,
                movement_hit.center.z + movement_hit.normal.z * collider.skin_width };
            player.SetPosition({
                resolved_center.x - collider.center_offset.x,
                resolved_center.y - collider.center_offset.y,
                resolved_center.z - collider.center_offset.z });
        }
    }
    if (legacy_stage_active_) UpdatePlayerGroundFromStage(player, stage);

    // Camera
    if (follow_player)
    {
        // Mouse right-drag rotates the camera ONLY (player is not affected).
        {
            static POINT prevPos{};
            POINT cur; GetCursorPos(&cur);
            POINT delta{ cur.x - prevPos.x, cur.y - prevPos.y };
            prevPos = cur;
            if (GetAsyncKeyState(VK_RBUTTON) & 0x8000)
            {
                const float k = 0.006f;
                camera_yaw_offset   += delta.x * k;
                camera_pitch_offset += -delta.y * k;
            }
        }

        const float key_rotate_speed = 1.8f * dt;
        if (GetAsyncKeyState('J') & 0x8000) camera_yaw_offset -= key_rotate_speed;
        if (GetAsyncKeyState('L') & 0x8000) camera_yaw_offset += key_rotate_speed;
        if (GetAsyncKeyState('I') & 0x8000) camera_pitch_offset += key_rotate_speed;
        if (GetAsyncKeyState('K') & 0x8000) camera_pitch_offset -= key_rotate_speed;
        const float lim = 1.40f;
        if (camera_pitch_offset >  lim) camera_pitch_offset =  lim;
        if (camera_pitch_offset < -lim) camera_pitch_offset = -lim;

        // Simple chase camera: target = player position + height
        const auto& p = player.GetPosition();
        DirectX::XMFLOAT3 focus{ p.x, p.y + 1.0f, p.z };

        // Orbit the camera around the player. Camera yaw is independent from
        // player yaw so movement cannot feed back into camera rotation.
        float yaw   = camera_yaw_offset;
        float pitch = camera_pitch_offset;
        float cp = cosf(pitch);
        DirectX::XMFLOAT3 desired{
            focus.x - sinf(yaw) * cp * follow_distance,
            focus.y + follow_height + sinf(pitch) * follow_distance,
            focus.z - cosf(yaw) * cp * follow_distance
        };

        // Lerp current eye toward desired
        const auto& curEye = camera.GetEye();
        float t = 1.0f - expf(-follow_lag * dt);
        DirectX::XMFLOAT3 newEye{
            curEye.x + (desired.x - curEye.x) * t,
            curEye.y + (desired.y - curEye.y) * t,
            curEye.z + (desired.z - curEye.z) * t
        };
        camera.SetLookAt(newEye, focus, { 0, 1, 0 });
    }
    else
    {
        controller.Update(dt);
        controller.SyncControllerToCamera(camera);
    }

    // Stage update (no-op for now)
    stage.Update(dt);
}

void SceneGame::DrawCameraGUI()
{
#ifdef USE_IMGUI
    ImGui::Checkbox("プレイヤーを追従", &follow_player);
    ImGui::SliderFloat("カメラ距離", &follow_distance, 3.0f, 80.0f);
    ImGui::SliderFloat("カメラ高さ", &follow_height, 0.0f, 30.0f);
    ImGui::SliderFloat("追従の速さ", &follow_lag, 1.0f, 20.0f);
    ImGui::SliderFloat("水平回転オフセット", &camera_yaw_offset, -3.14f, 3.14f);
    ImGui::SliderFloat("垂直回転オフセット", &camera_pitch_offset, -1.40f, 1.40f);
#endif
}

void SceneGame::DrawPlayerGUI()
{
#ifdef USE_IMGUI
    if (ImGui::CollapsingHeader("移動設定", ImGuiTreeNodeFlags_DefaultOpen))
    {
        if (ImGui::Button("移動設定を初期値に戻す")) player.ResetMovementSettings();
        float move_speed = player.GetMoveSpeed();
        if (ImGui::SliderFloat("移動速度", &move_speed, 0.0f, 30.0f, "%.2f"))
        {
            player.SetMoveSpeed(move_speed);
        }
        float acceleration = player.GetAcceleration();
        if (ImGui::SliderFloat("加速度", &acceleration, 0.0f, 120.0f, "%.1f"))
        {
            player.SetAcceleration(acceleration);
        }
        float deceleration = player.GetDeceleration();
        if (ImGui::SliderFloat("減速度", &deceleration, 0.0f, 120.0f, "%.1f"))
        {
            player.SetDeceleration(deceleration);
        }
        float turn_speed_deg = DirectX::XMConvertToDegrees(player.GetTurnSpeed());
        if (ImGui::SliderFloat("旋回速度", &turn_speed_deg, 0.0f, 1440.0f, "%.0f deg/s"))
        {
            player.SetTurnSpeed(DirectX::XMConvertToRadians(turn_speed_deg));
        }
        ImGui::TextDisabled("垂直物理: 未接続（接地システム実装後に有効化）");

        DirectX::XMFLOAT3 p = player.GetPosition();
        if (ImGui::DragFloat3("プレイヤー位置", &p.x, 0.05f))
        {
            player.SetPosition(p);
        }

    }

    if (ImGui::CollapsingHeader("アニメーション割り当て", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Text("アニメーション割り当て");
        if (ImGui::Button("割り当てを初期値に戻す")) ResetAnimationClipMapping();
        skinned_mesh* player_mesh = player.GetSkinned();
        DrawClipCombo("待機##sg", player.clip_idle, player_mesh);
        DrawClipCombo("移動##sg", player.clip_walk, player_mesh);
        DrawClipCombo("ジャンプ##sg", player.clip_jump, player_mesh);
        ImGui::Text("現在のクリップ: %d", player.GetAnimationClipIndex());

    }

    if (ImGui::CollapsingHeader("表示補正"))
    {
        DirectX::XMFLOAT3 s = player.GetScale();
        if (ImGui::DragFloat3("拡大率##sg", &s.x, 0.001f, 0.001f, 1.0f, "%.4f"))
        {
            player.SetScale(s);
        }
        float visual_pitch = player.GetVisualPitchDeg();
        if (ImGui::SliderFloat("表示ピッチ", &visual_pitch, 60.0f, 120.0f, "%.1f deg"))
        {
            player.SetVisualPitchDeg(visual_pitch);
        }
        float visual_yaw = player.GetVisualYawOffsetDeg();
        if (ImGui::SliderFloat("表示ヨー補正", &visual_yaw, -360.0f, 360.0f, "%.1f deg"))
        {
            player.SetVisualYawOffsetDeg(visual_yaw);
        }
        float visual_roll = player.GetVisualRollDeg();
        if (ImGui::SliderFloat("表示ロール", &visual_roll, -360.0f, 360.0f, "%.1f deg"))
        {
            player.SetVisualRollDeg(visual_roll);
        }
    }
#endif
}

void SceneGame::DrawStageGUI()
{
#ifdef USE_IMGUI
    if (ImGui::CollapsingHeader("トランスフォーム", ImGuiTreeNodeFlags_DefaultOpen))
    {
        DirectX::XMFLOAT3 sp = stage.GetPosition();
        if (ImGui::DragFloat3("位置##stage", &sp.x, 0.05f))
        {
            stage.SetPosition(sp);
        }
        DirectX::XMFLOAT3 sr = stage.GetAngle();
        DirectX::XMFLOAT3 sr_deg{
            DirectX::XMConvertToDegrees(sr.x),
            DirectX::XMConvertToDegrees(sr.y),
            DirectX::XMConvertToDegrees(sr.z)
        };
        if (ImGui::DragFloat3("回転##stage", &sr_deg.x, 0.5f, -180.0f, 180.0f))
        {
            stage.SetAngle({
                DirectX::XMConvertToRadians(sr_deg.x),
                DirectX::XMConvertToRadians(sr_deg.y),
                DirectX::XMConvertToRadians(sr_deg.z)
            });
        }
        DirectX::XMFLOAT3 ss = stage.GetScale();
        if (ImGui::DragFloat3("拡大率##stage", &ss.x, 0.01f, 0.001f, 100.0f))
        {
            stage.SetScale(ss);
        }
    }
#endif
}
