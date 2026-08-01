#include "scene_game.h"

#include <windows.h>
#include <cmath>

#ifdef USE_IMGUI
#include "imgui/imgui.h"
#endif

void SceneGame::Initialize(skinned_mesh* stage_mesh, float aspect)
{
    camera.SetPerspectiveFov(DirectX::XMConvertToRadians(50.0f),
                             aspect, 0.1f, 10000.0f);
    stage.SetModel(stage_mesh);
    ResetGameplay();
}

void SceneGame::ResetGameplay()
{
    camera_yaw_offset = 0.0f;
    camera_pitch_offset = 0.0f;

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

void SceneGame::SetLegacyStageActive(bool active)
{
    // 旧ステージの衝突メッシュを有効・無効にするだけ。
    // 二重衝突を防ぐ判断は SceneCollisionWorld 側の「移行済み集合」が行う。
    stage.GetCollisionMesh().SetEnabled(active);
    legacy_stage_active = active;
}

void SceneGame::FollowCameraTarget(const DirectX::XMFLOAT3& target_position,
    const DirectX::XMFLOAT3& look_at_offset,
    float distance, float height, float lag, float delta_time)
{
    // カメラの回転入力はここで受ける。
    // 追従対象の位置と設定値だけを外から受け取り、対象の具象型には触れない。
    UpdateCameraRotationInput(delta_time);

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

void SceneGame::UpdateFreeCamera(float delta_time)
{
    // 追従対象が居ない場合のカメラ。
    // 「対象が居ないから何かを追う」という代替経路は持たない。
    controller.Update(delta_time);
    controller.SyncControllerToCamera(camera);
}

void SceneGame::UpdateCameraRotationInput(float dt)
{
    // Mouse right-drag rotates the camera ONLY.
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
}

void SceneGame::Update(float dt)
{
    // ここで動かすのは旧ステージだけ。
    // 操作対象の更新は Scene の Component が行い、この関数は一切関与しない。
    if (legacy_stage_active) stage.Update(dt);
}

void SceneGame::DrawCameraGUI()
{
#ifdef USE_IMGUI
    ImGui::TextDisabled("追従対象は CameraTargetComponent が決めます");
    ImGui::SliderFloat("水平回転オフセット", &camera_yaw_offset, -3.14f, 3.14f);
    ImGui::SliderFloat("垂直回転オフセット", &camera_pitch_offset, -1.40f, 1.40f);
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
