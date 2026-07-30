#pragma once

#include <DirectXMath.h>
#include "../../RePlayEngine/Core/Components/SphereColliderComponent.h"

class Camera;
class skinned_mesh;

class Player
{
public:
    Player() = default;
    ~Player() = default;

    // フレームワークが所有する実体のスキンメッシュを関連付ける。
    void SetSkinnedMesh(skinned_mesh* mesh) { skinned_ = mesh; }

    // カメラ基準の水平移動と旋回を更新する。垂直物理は現在未接続。
    void Update(float elapsed_time, const Camera& camera);
    void ResetPlacement();
    void ResetMovementSettings();

    void UpdateTransform();

    const DirectX::XMFLOAT3&   GetPosition() const { return position; }
    const DirectX::XMFLOAT3&   GetAngle()    const { return angle; }
    const DirectX::XMFLOAT3&   GetScale()    const { return scale; }
    const DirectX::XMFLOAT4X4& GetTransform()const { return transform; }
    skinned_mesh*              GetSkinned()  const { return skinned_; }
    float GetMoveSpeed() const { return move_speed; }
    float GetAcceleration() const { return acceleration; }
    float GetDeceleration() const { return deceleration; }
    float GetTurnSpeed() const { return turn_speed; }
    float GetJumpSpeed() const { return jump_speed; }
    float GetGravity() const { return gravity; }
    float GetGroundY() const { return ground_y; }
    float GetVisualPitchDeg() const { return visual_pitch_deg; }
    float GetVisualYawOffsetDeg() const { return visual_yaw_offset_deg; }
    float GetVisualRollDeg() const { return visual_roll_deg; }
    bool IsVerticalPhysicsEnabled() const { return vertical_physics_enabled_; }

    // 再生するアニメーションクリップを選ぶ。0は待機、1は歩行。
    int  GetAnimationClipIndex() const { return clip_index; }
    float GetAnimationTick()     const { return anim_tick; }

    void SetPosition(const DirectX::XMFLOAT3& p) { position = p; UpdateTransform(); }
    void SetAngle   (const DirectX::XMFLOAT3& a) { angle    = a; UpdateTransform(); }
    void SetScale   (const DirectX::XMFLOAT3& s) { scale    = s; UpdateTransform(); }
    void SetMoveSpeed(float v) { move_speed = v; }
    void SetAcceleration(float v) { acceleration = v; }
    void SetDeceleration(float v) { deceleration = v; }
    void SetTurnSpeed(float v) { turn_speed = v; }
    void SetJumpSpeed(float v) { jump_speed = v; }
    void SetGravity(float v) { gravity = v; }
    void SetGroundY(float y) { ground_y = y; }
    void SetVisualPitchDeg(float v) { visual_pitch_deg = v; UpdateTransform(); }
    void SetVisualYawOffsetDeg(float v) { visual_yaw_offset_deg = v; UpdateTransform(); }
    void SetVisualRollDeg(float v) { visual_roll_deg = v; UpdateTransform(); }
    void SetVerticalPhysicsEnabled(bool enabled) { vertical_physics_enabled_ = enabled; }
    void SnapToGround(float y);
    ReplayEngine::Core::SphereColliderComponent& Collider() noexcept { return collider_; }
    const ReplayEngine::Core::SphereColliderComponent& Collider() const noexcept { return collider_; }

private:
    DirectX::XMFLOAT3 position{ 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT3 angle   { 0, 0, 0 };
    DirectX::XMFLOAT3 scale   { 0.01f, 0.01f, 0.01f };
    DirectX::XMFLOAT3 velocity{ 0, 0, 0 };
    DirectX::XMFLOAT4X4 transform = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
    bool on_ground = true;
    bool vertical_physics_enabled_ = false;
    ReplayEngine::Core::SphereColliderComponent collider_{};

    // FBXを正立表示する従来のビュー初期化回転に合わせる。
    // その上へ動的なヨー回転 angle.y を加える。
    float visual_pitch_deg = 90.0f;
    float visual_yaw_offset_deg = 188.5f;
    float visual_roll_deg = 180.0f;

    // GP4 LandWalkSceneの初期値を基に調整した移動パラメータ。
    float gravity      = 18.0f;
    float move_speed   = 6.0f;
    float acceleration = 30.0f;
    float deceleration = 20.0f;
    float turn_speed   = DirectX::XMConvertToRadians(720.0f);
    float jump_speed   = 8.0f;
    float ground_y     = 0.0f;

    skinned_mesh* skinned_  = nullptr;
    int   clip_index = -1;  // -1 keeps the framework's manual clip selection
    float anim_tick  = 0.0f;

public:
    // 状態ごとのクリップ番号はImGuiから割り当てる。
    // 有効な番号を設定すると状態連動の切り替えが有効になる。
    int clip_idle = -1;
    int clip_walk = -1;
    int clip_jump = -1;
};
