#include "CharacterMotorComponent.h"

#include "../Physics/SphereColliderComponent.h"
#include "../../Object/GameObject/GameObject.h"
#include "../../Scene/Runtime/Scene.h"

#include <cmath>

namespace ReplayEngine::Components
{
    namespace
    {
        float Length2D(const DirectX::XMFLOAT3& value) noexcept
        {
            return std::sqrt(value.x * value.x + value.z * value.z);
        }
    }

    void CharacterMotorComponent::OnStart()
    {
        // Play 開始時は速度を必ず 0 から始める。
        // Edit Mode で触った値や前回の Play の残りを持ち越さないため。
        velocity_ = { 0.0f, 0.0f, 0.0f };
        pending_move_ = { 0.0f, 0.0f, 0.0f };
        jump_requested_ = false;
        grounded_ = true;
    }

    void CharacterMotorComponent::OnDisable()
    {
        // 無効化された瞬間に動きを止める。
        // 再有効化したときに古い速度で飛び出さないようにする。
        velocity_ = { 0.0f, 0.0f, 0.0f };
        pending_move_ = { 0.0f, 0.0f, 0.0f };
        jump_requested_ = false;
    }

    void CharacterMotorComponent::Move(const DirectX::XMFLOAT3& world_direction) noexcept
    {
        // 同じフレームに複数回呼ばれた場合は最後の要求を採用する。
        pending_move_ = world_direction;
    }

    void CharacterMotorComponent::Teleport(const DirectX::XMFLOAT3& world_position)
    {
        if (Core::GameObject* owner = Owner())
        {
            owner->GetTransform().SetWorldPosition(world_position);
        }
    }

    float CharacterMotorComponent::PlanarSpeed() const noexcept
    {
        return Length2D(velocity_);
    }

    SphereColliderComponent* CharacterMotorComponent::FindCollider() const
    {
        Core::GameObject* owner = Owner();
        return owner != nullptr ? owner->GetComponent<SphereColliderComponent>() : nullptr;
    }

    void CharacterMotorComponent::OnFixedUpdate(float fixed_delta_time)
    {
        Core::GameObject* owner = Owner();
        if (owner == nullptr || fixed_delta_time <= 0.0f) return;

        Core::Transform& transform = owner->GetTransform();
        const DirectX::XMFLOAT3 previous_position = transform.LocalPosition();

        // ---- 水平方向 -------------------------------------------------------

        DirectX::XMFLOAT3 direction = pending_move_;
        pending_move_ = { 0.0f, 0.0f, 0.0f };   // 要求は 1 回で消費する

        const float direction_length = Length2D(direction);
        const float control = grounded_ ? 1.0f : air_control;

        if (direction_length > 0.0001f)
        {
            direction.x /= direction_length;
            direction.z /= direction_length;
            last_move_direction_ = DirectX::XMFLOAT3{ direction.x, 0.0f, direction.z };

            const float step = acceleration * control * fixed_delta_time;
            velocity_.x += direction.x * step;
            velocity_.z += direction.z * step;

            // 水平速度を上限へ収める。
            const float speed = Length2D(velocity_);
            if (speed > move_speed && speed > 0.0001f)
            {
                velocity_.x = velocity_.x / speed * move_speed;
                velocity_.z = velocity_.z / speed * move_speed;
            }
        }
        else
        {
            last_move_direction_ = { 0.0f, 0.0f, 0.0f };

            // 入力が無ければ滑らかに減速する。
            const float step = deceleration * control * fixed_delta_time;
            const float speed = Length2D(velocity_);
            if (speed > step && speed > 0.0001f)
            {
                velocity_.x -= velocity_.x / speed * step;
                velocity_.z -= velocity_.z / speed * step;
            }
            else
            {
                velocity_.x = 0.0f;
                velocity_.z = 0.0f;
            }
        }

        // ---- 垂直方向 -------------------------------------------------------

        const SphereColliderComponent* collider = FindCollider();
        const float radius = collider != nullptr ? collider->radius : 0.38f;
        const float walkable = collider != nullptr ? collider->walkable_normal_y : 0.25f;

        if (vertical_physics)
        {
            // ジャンプ要求は接地している時だけ消費する。
            // フラグ方式なので、可変フレームで何度押されても 1 回、
            // FixedUpdate が複数回走っても 1 回しか実行されない。
            if (jump_requested_ && grounded_)
            {
                velocity_.y = jump_power;
                grounded_ = false;
            }
            jump_requested_ = false;

            velocity_.y -= gravity * fixed_delta_time;
            if (velocity_.y < -maximum_fall_speed) velocity_.y = -maximum_fall_speed;
        }
        else
        {
            // 旧 Player と同じく、垂直物理を切っている間は常に接地扱い。
            velocity_.y = 0.0f;
            jump_requested_ = false;
            grounded_ = true;
        }

        // ---- 位置へ反映 -----------------------------------------------------

        DirectX::XMFLOAT3 position = previous_position;
        position.x += velocity_.x * fixed_delta_time;
        position.z += velocity_.z * fixed_delta_time;
        if (vertical_physics) position.y += velocity_.y * fixed_delta_time;
        transform.SetLocalPosition(position);

        // ---- 地形との解決 ---------------------------------------------------
        // 順序は旧 SceneGame と同じ「壁 -> 接地」。
        ResolveWalls(previous_position);
        ResolveGround(transform.LocalPosition(), radius, walkable);
    }

    void CharacterMotorComponent::ResolveWalls(const DirectX::XMFLOAT3& previous_position)
    {
        Core::GameObject* owner = Owner();
        if (owner == nullptr) return;

        const SphereColliderComponent* collider = FindCollider();
        if (collider == nullptr || !collider->ActiveInHierarchy()) return;

        Scene::Scene* scene = GetScene();
        if (scene == nullptr) return;
        const Scene::IPhysicsQueryService* physics = scene->Services().Physics();
        if (physics == nullptr || !physics->CollisionAvailable()) return;

        Core::Transform& transform = owner->GetTransform();
        const DirectX::XMFLOAT3 current = transform.LocalPosition();

        const DirectX::XMFLOAT3 start{
            previous_position.x + collider->center_offset.x,
            previous_position.y + collider->center_offset.y,
            previous_position.z + collider->center_offset.z };
        const DirectX::XMFLOAT3 end{
            current.x + collider->center_offset.x,
            current.y + collider->center_offset.y,
            current.z + collider->center_offset.z };

        // 床は下向きキャストが扱うので、ここでは壁だけを対象にする。
        Scene::SphereSweepHit hit{};
        if (!physics->SweepSphere(start, end, collider->radius,
            collider->walkable_normal_y - 0.001f, hit))
        {
            return;
        }

        // 面から skin_width だけ離した位置へ押し戻す。
        transform.SetLocalPosition(DirectX::XMFLOAT3{
            hit.center.x + hit.normal.x * collider->skin_width - collider->center_offset.x,
            hit.center.y + hit.normal.y * collider->skin_width - collider->center_offset.y,
            hit.center.z + hit.normal.z * collider->skin_width - collider->center_offset.z });
    }

    void CharacterMotorComponent::ResolveGround(const DirectX::XMFLOAT3& position,
        float radius, float walkable_normal_y)
    {
        Core::GameObject* owner = Owner();
        if (owner == nullptr) return;

        const SphereColliderComponent* collider = FindCollider();
        const Scene::Scene* scene = GetScene();
        const Scene::IPhysicsQueryService* physics =
            scene != nullptr ? scene->Services().Physics() : nullptr;

        has_ground_ = false;
        ground_height_ = fallback_ground_y;
        ground_normal_ = { 0.0f, 1.0f, 0.0f };

        if (collider != nullptr && collider->ActiveInHierarchy() &&
            physics != nullptr && physics->CollisionAvailable())
        {
            Scene::GroundHit hit{};
            if (physics->QueryGround(position, radius, 80.0f, 300.0f, walkable_normal_y, hit))
            {
                has_ground_ = true;
                ground_height_ = hit.position.y;
                ground_normal_ = hit.normal;
            }
        }

        if (!vertical_physics)
        {
            // 垂直物理が無効な間は、旧 Player の SnapToGround と同じく
            // 地面の高さへ吸着させるだけにする。
            if (has_ground_)
            {
                DirectX::XMFLOAT3 snapped = owner->GetTransform().LocalPosition();
                snapped.y = ground_height_;
                owner->GetTransform().SetLocalPosition(snapped);
            }
            grounded_ = true;
            return;
        }

        DirectX::XMFLOAT3 current = owner->GetTransform().LocalPosition();
        if (current.y <= ground_height_ && velocity_.y <= 0.0f)
        {
            current.y = ground_height_;
            owner->GetTransform().SetLocalPosition(current);
            velocity_.y = 0.0f;
            grounded_ = true;
        }
        else
        {
            grounded_ = false;
        }
    }
}
