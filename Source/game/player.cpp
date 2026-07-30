#include "player.h"
#include "camera.h"
#include "game_input.h"
#include <cmath>

using DirectX::XMFLOAT3;
using DirectX::XMMATRIX;

void Player::ResetPlacement()
{
    position = { 0.0f, 0.0f, 0.0f };
    angle = { 0.0f, 0.0f, 0.0f };
    scale = { 0.01f, 0.01f, 0.01f };
    visual_pitch_deg = 90.0f;
    visual_yaw_offset_deg = 188.5f;
    visual_roll_deg = 180.0f;
    velocity = { 0.0f, 0.0f, 0.0f };
    on_ground = true;
    if (clip_idle >= 0)
    {
        clip_index = clip_idle;
    }
    else
    {
        clip_index = -1;
    }
    anim_tick = 0.0f;
    UpdateTransform();
}

void Player::ResetMovementSettings()
{
    gravity = 18.0f;
    move_speed = 6.0f;
    acceleration = 30.0f;
    deceleration = 20.0f;
    turn_speed = DirectX::XMConvertToRadians(720.0f);
    jump_speed = 8.0f;
    velocity = { 0.0f, 0.0f, 0.0f };
}

void Player::Update(float dt, const Camera& camera)
{
    float ax = GameInput::AxisX();
    float ay = GameInput::AxisY();
    if (vertical_physics_enabled_ && GameInput::JumpPressed() && on_ground)
    {
        velocity.y = jump_speed;
        on_ground = false;
    }

    // カメラ基準の前方・右方向をXZ平面へ射影して正規化する。
    const XMFLOAT3& cf = camera.GetFront();
    const XMFLOAT3& cr = camera.GetRight();
    float fl = sqrtf(cf.x * cf.x + cf.z * cf.z);
    float rl = sqrtf(cr.x * cr.x + cr.z * cr.z);
    float fx = (fl > 0.0001f) ? cf.x / fl : 0.0f;
    float fz = (fl > 0.0001f) ? cf.z / fl : 1.0f;
    float rx = (rl > 0.0001f) ? cr.x / rl : 1.0f;
    float rz = (rl > 0.0001f) ? cr.z / rl : 0.0f;

    float vx = fx * ay + rx * ax;
    float vz = fz * ay + rz * ax;
    float vlen = sqrtf(vx * vx + vz * vz);

    if (vlen > 0.0001f)
    {
        vx /= vlen; vz /= vlen;
        float acc = acceleration * dt;
        velocity.x += vx * acc;
        velocity.z += vz * acc;
        // 水平速度を上限に収める。
        float hs = sqrtf(velocity.x * velocity.x + velocity.z * velocity.z);
        if (hs > move_speed)
        {
            velocity.x = velocity.x / hs * move_speed;
            velocity.z = velocity.z / hs * move_speed;
        }

        float target_yaw = atan2f(vx, vz);
        float delta_yaw = target_yaw - angle.y;
        while (delta_yaw > DirectX::XM_PI) delta_yaw -= DirectX::XM_2PI;
        while (delta_yaw < -DirectX::XM_PI) delta_yaw += DirectX::XM_2PI;
        float rot = turn_speed * dt;
        if (delta_yaw > rot) delta_yaw = rot;
        if (delta_yaw < -rot) delta_yaw = -rot;
        angle.y += delta_yaw;
    }
    else
    {
        // 入力がないときは滑らかに減速する。
        float dec = deceleration * dt;
        float hs = sqrtf(velocity.x * velocity.x + velocity.z * velocity.z);
        if (hs > dec)
        {
            velocity.x -= velocity.x / hs * dec;
            velocity.z -= velocity.z / hs * dec;
        }
        else
        {
            velocity.x = 0.0f;
            velocity.z = 0.0f;
        }
    }

    // 垂直物理は正式な接地システムとGravityComponentを接続するまで実行しない。
    if (vertical_physics_enabled_)
    {
        velocity.y -= gravity * dt;
        if (position.y + velocity.y * dt <= ground_y)
        {
            position.y = ground_y;
            velocity.y = 0.0f;
            on_ground = true;
        }
        else
        {
            on_ground = false;
        }
    }
    else
    {
        velocity.y = 0.0f;
        on_ground = true;
    }

    // 求めた速度を位置へ反映する。
    position.x += velocity.x * dt;
    if (vertical_physics_enabled_) position.y += velocity.y * dt;
    position.z += velocity.z * dt;

    // 状態に対応するアニメーションクリップを選ぶ。
    // スロットが-1ならフレームワーク側の手動選択を維持する。
    float planar = sqrtf(velocity.x * velocity.x + velocity.z * velocity.z);
    int desired_clip = -1;
    if (!on_ground)
    {
        if (clip_jump >= 0) desired_clip = clip_jump;
    }
    else if (planar > 0.2f)
    {
        if (clip_walk >= 0) desired_clip = clip_walk;
    }
    else
    {
        if (clip_idle >= 0) desired_clip = clip_idle;
    }
    clip_index = desired_clip;
    anim_tick += dt;

    UpdateTransform();
}

void Player::SnapToGround(float y)
{
    ground_y = y;
    if (position.y <= ground_y && velocity.y <= 0.0f)
    {
        position.y = ground_y;
        velocity.y = 0.0f;
        on_ground = true;
        UpdateTransform();
    }
}

void Player::UpdateTransform()
{
    XMMATRIX S = DirectX::XMMatrixScaling(scale.x, scale.y, scale.z);
    // FBXの基準姿勢を正立・反転補正した後、現在のヨー回転を加える。
    XMMATRIX R_rest = DirectX::XMMatrixRotationRollPitchYaw(
        DirectX::XMConvertToRadians(visual_pitch_deg) + angle.x,
        DirectX::XMConvertToRadians(visual_yaw_offset_deg) + angle.y,
        DirectX::XMConvertToRadians(visual_roll_deg) + angle.z);
    XMMATRIX T = DirectX::XMMatrixTranslation(position.x, position.y, position.z);
    DirectX::XMStoreFloat4x4(&transform, S * R_rest * T);
}
